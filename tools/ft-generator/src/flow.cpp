/**
 * @file
 * @author Pavel Siska <siska@cesnet.cz>
 * @author Michal Sedlak <sedlakm@cesnet.cz>
 * @brief Implementation of Flow interface
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "flow.h"
#include "layer.h"
#include "layers/ethernet.h"
#include "layers/icmpecho.h"
#include "layers/icmprandom.h"
#include "layers/icmpv6echo.h"
#include "layers/icmpv6random.h"
#include "layers/ipv4.h"
#include "layers/ipv6.h"
#include "layers/mpls.h"
#include "layers/payload.h"
#include "layers/tcp.h"
#include "layers/udp.h"
#include "layers/vlan.h"
#include "packet.h"
#include "packetflowspan.h"
#include "packetsizegeneratorslow.h"
#include "randomgenerator.h"
#include "timeval.h"

#include <pcapplusplus/EthLayer.h>
#include <pcapplusplus/IPv4Layer.h>
#include <pcapplusplus/IPv6Layer.h>
#include <pcapplusplus/IcmpLayer.h>
#include <pcapplusplus/IcmpV6Layer.h>
#include <pcapplusplus/Packet.h>
#include <pcapplusplus/UdpLayer.h>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <variant>
#include <vector>

namespace generator {

static constexpr int ETHER_HDR_SIZE = sizeof(pcpp::ether_header);
static constexpr int ICMP_HDR_SIZE = sizeof(pcpp::icmphdr);
static constexpr int ICMPV6_HDR_SIZE = sizeof(pcpp::icmpv6hdr);
static constexpr int IPV4_HDR_SIZE = sizeof(pcpp::iphdr);
static constexpr int IPV6_HDR_SIZE = sizeof(pcpp::ip6_hdr);
static constexpr int UDP_HDR_SIZE = sizeof(pcpp::udphdr);
static constexpr int ICMP_UNREACH_PKT_SIZE = ICMP_HDR_SIZE + IPV4_HDR_SIZE + UDP_HDR_SIZE;
// Unreachable ICMPv6 message includes 4 reserved bytes after the header
static constexpr int ICMPV6_UNREACH_PKT_SIZE = ICMPV6_HDR_SIZE + 4 + IPV6_HDR_SIZE + UDP_HDR_SIZE;

static std::vector<IntervalInfo> AdjustPacketSizesToL3(std::vector<IntervalInfo> intervals)
{
	for (IntervalInfo& interval : intervals) {
		if (interval._from < ETHER_HDR_SIZE || interval._to < ETHER_HDR_SIZE) {
			throw std::invalid_argument("value must be at least the size of an L2 header");
		}
		interval._from -= ETHER_HDR_SIZE;
		interval._to -= ETHER_HDR_SIZE;
	}
	return intervals;
}

const static std::vector<IntervalInfo> PACKET_SIZE_PROBABILITIES = AdjustPacketSizesToL3(
	{{64, 79, 0.2824},
	 {80, 159, 0.073},
	 {160, 319, 0.0115},
	 {320, 639, 0.012},
	 {640, 1279, 0.0092},
	 {1280, 1500, 0.6119}});

static std::vector<config::EncapsulationLayer>
ChooseEncaps(const std::vector<config::EncapsulationVariant>& variants)
{
	if (variants.empty()) {
		return {};
	}

	double rand = RandomGenerator::GetInstance().RandomDouble();
	double accum = 0.0;
	for (const auto& variant : variants) {
		accum += variant.GetProbability();
		if (rand <= accum) {
			return variant.GetLayers();
		}
	}

	return {};
}

Flow::Flow(
	uint64_t id,
	const FlowProfile& profile,
	AddressGenerators& addressGenerators,
	const config::Config& config)
	: _fwdPackets(profile._packets)
	, _revPackets(profile._packetsRev)
	, _fwdBytes(profile._bytes)
	, _revBytes(profile._bytesRev)
	, _tsFirst(profile._startTime)
	, _tsLast(profile._endTime)
	, _id(id)
{
	MacAddress macSrc = addressGenerators.GenerateMac();
	MacAddress macDst = addressGenerators.GenerateMac();
	AddLayer(std::make_unique<Ethernet>(macSrc, macDst));

	const auto& encapsLayers = ChooseEncaps(config.GetEncapsulation().GetVariants());
	for (size_t i = 0; i < encapsLayers.size(); i++) {
		const auto& layer = encapsLayers[i];
		if (const auto* vlan = std::get_if<config::EncapsulationLayerVlan>(&layer)) {
			AddLayer(std::make_unique<Vlan>(vlan->GetId()));
		} else if (const auto* mpls = std::get_if<config::EncapsulationLayerMpls>(&layer)) {
			AddLayer(std::make_unique<Mpls>(mpls->GetLabel()));
		} else {
			throw std::runtime_error("Invalid encapsulation layer");
		}
	}

	switch (profile._l3Proto) {
	case L3Protocol::Unknown:
		throw std::runtime_error("Unknown L3 protocol");

	case L3Protocol::Ipv4: {
		IPv4Address ipSrc;
		if (profile._srcIp) {
			assert(profile._srcIp->getType() == IPAddress::AddressType::IPv4AddressType);
			ipSrc = profile._srcIp->getIPv4();
		} else {
			ipSrc = addressGenerators.GenerateIPv4();
		}

		IPv4Address ipDst;
		if (profile._dstIp) {
			assert(profile._dstIp->getType() == IPAddress::AddressType::IPv4AddressType);
			ipDst = profile._dstIp->getIPv4();
		} else {
			ipDst = addressGenerators.GenerateIPv4();
		}

		auto fragProb = config.GetIPv4().GetFragmentationProbability();
		auto minPktSizeToFragment = config.GetIPv4().GetMinPacketSizeToFragment();
		AddLayer(std::make_unique<IPv4>(ipSrc, ipDst, fragProb, minPktSizeToFragment));
	} break;

	case L3Protocol::Ipv6: {
		IPv6Address ipSrc;
		if (profile._srcIp) {
			assert(profile._srcIp->getType() == IPAddress::AddressType::IPv6AddressType);
			ipSrc = profile._srcIp->getIPv6();
		} else {
			ipSrc = addressGenerators.GenerateIPv6();
		}

		IPv6Address ipDst;
		if (profile._dstIp) {
			assert(profile._dstIp->getType() == IPAddress::AddressType::IPv6AddressType);
			ipDst = profile._dstIp->getIPv6();
		} else {
			ipDst = addressGenerators.GenerateIPv6();
		}

		auto fragProb = config.GetIPv6().GetFragmentationProbability();
		auto minPktSizeToFragment = config.GetIPv6().GetMinPacketSizeToFragment();
		AddLayer(std::make_unique<IPv6>(ipSrc, ipDst, fragProb, minPktSizeToFragment));
	} break;
	}

	switch (profile._l4Proto) {
	case L4Protocol::Unknown:
		throw std::runtime_error("Unknown L4 protocol");

	case L4Protocol::Tcp: {
		uint16_t portSrc = profile._srcPort;
		uint16_t portDst = profile._dstPort;
		AddLayer(std::make_unique<Tcp>(portSrc, portDst));
	} break;

	case L4Protocol::Udp: {
		uint16_t portSrc = profile._srcPort;
		uint16_t portDst = profile._dstPort;
		AddLayer(std::make_unique<Udp>(portSrc, portDst));
	} break;

	case L4Protocol::Icmp: {
		if (profile._l3Proto != L3Protocol::Ipv4) {
			throw std::runtime_error("L4 protocol is ICMP but L3 protocol is not IPv4");
		}
		AddLayer(MakeIcmpLayer(profile._l3Proto));
	} break;

	case L4Protocol::Icmpv6: {
		if (profile._l3Proto != L3Protocol::Ipv6) {
			throw std::runtime_error("L4 protocol is ICMPv6 but L3 protocol is not IPv6");
		}
		AddLayer(MakeIcmpLayer(profile._l3Proto));
	} break;
	}

	if (profile._l4Proto == L4Protocol::Tcp || profile._l4Proto == L4Protocol::Udp) {
		AddLayer(std::make_unique<Payload>());
	}

	Plan();
}

std::unique_ptr<Layer> Flow::MakeIcmpLayer(L3Protocol l3Proto)
{
	double fwdRevRatioDiff = 1.0;
	double bytesPerPkt = 0;
	if (_fwdPackets + _revPackets > 0) {
		double min = std::min(_fwdPackets, _revPackets);
		double max = std::max(_fwdPackets, _revPackets);
		fwdRevRatioDiff = 1.0 - (min / max);
		bytesPerPkt = (_fwdBytes + _revBytes) / (_fwdPackets + _revPackets);
	}

	/*
	 * A simple heuristic to choose the proper ICMP packet generation strategy based
	 * on the flow characteristics
	 *
	 * NOTE: Might need further evaluation if this is a "good enough" way to do this
	 * and/or some tweaking
	 */
	assert(l3Proto == L3Protocol::Ipv4 || l3Proto == L3Protocol::Ipv6);
	std::unique_ptr<Layer> layer;
	if (l3Proto == L3Protocol::Ipv4) {
		if ((_fwdPackets <= 3 || _revPackets <= 3)
			&& (bytesPerPkt <= 1.10 * ICMP_UNREACH_PKT_SIZE)) {
			// Low amount of small enough packets
			layer = std::make_unique<IcmpRandom>();
		} else if (fwdRevRatioDiff <= 0.2) {
			// About the same number of packets in both directions
			layer = std::make_unique<IcmpEcho>();
		} else if (bytesPerPkt <= 1.10 * ICMP_UNREACH_PKT_SIZE) {
			// Small enough packets
			layer = std::make_unique<IcmpRandom>();
		} else {
			// Enough packets and many of them
			layer = std::make_unique<IcmpEcho>();
		}
	} else {
		if ((_fwdPackets <= 3 || _revPackets <= 3)
			&& (bytesPerPkt <= 1.10 * ICMPV6_UNREACH_PKT_SIZE)) {
			// Low amount of small enough packets
			layer = std::make_unique<Icmpv6Random>();
		} else if (fwdRevRatioDiff <= 0.2) {
			// About the same number of packets in both directions
			layer = std::make_unique<Icmpv6Echo>();
		} else if (bytesPerPkt <= 1.10 * ICMPV6_UNREACH_PKT_SIZE) {
			// Small enough packets
			layer = std::make_unique<Icmpv6Random>();
		} else {
			// Enough packets and many of them
			layer = std::make_unique<Icmpv6Echo>();
		}
	}

	return layer;
}

Flow::~Flow()
{
	// NOTE: Explicit destructor is required because the class contains
	//       std::unique_ptr<Layer> where Layer is an incomplete type!
}

void Flow::AddLayer(std::unique_ptr<Layer> layer)
{
	layer->AddedToFlow(this, _layerStack.size());
	_layerStack.emplace_back(std::move(layer));
}

void Flow::Plan()
{
	_packets.resize(_fwdPackets + _revPackets);

	for (auto& layer : _layerStack) {
		layer->PlanFlow(*this);
	}

	PlanPacketsDirections();
	PlanPacketsSizes();

	for (auto& layer : _layerStack) {
		layer->PostPlanFlow(*this);
	}

	for (auto& layer : _layerStack) {
		layer->PlanExtra(*this);
	}

	PlanPacketsTimestamps();
}

PacketExtraInfo Flow::GenerateNextPacket(PcppPacket& packet)
{
	if (_packets.empty()) {
		throw std::runtime_error("no more packets to generate in flow");
	}

	packet = PcppPacket();
	PacketExtraInfo extra;

	Packet packetPlan = _packets.front();
	extra._direction = packetPlan._direction;
	extra._time = packetPlan._timestamp;

	for (auto& [layer, params] : packetPlan._layers) {
		layer->Build(packet, params, packetPlan);
	}
	/**
	 * The method computeCalculateFields needs to be called twice here.
	 * The first time before calling the PostBuild callbacks, as they need the
	 * finished packet including the computed fields.
	 * The second time after calling the PostBuild callbacks, as they might
	 * modify the packet and the fields may need to be recomputed.
	 */
	packet.computeCalculateFields();

	for (auto& [layer, params] : packetPlan._layers) {
		layer->PostBuild(packet, params, packetPlan);
	}
	packet.computeCalculateFields();

	_packets.erase(_packets.begin());

	return extra;
}

Timeval Flow::GetNextPacketTime() const
{
	return _packets.front()._timestamp;
}

bool Flow::IsFinished() const
{
	return _packets.empty();
}

void Flow::PlanPacketsDirections()
{
	PacketFlowSpan packetsSpan(this, true);
	auto [fwd, rev] = packetsSpan.GetAvailableDirections();

	std::vector<Direction> directions;
	directions.insert(directions.end(), fwd, Direction::Forward);
	directions.insert(directions.end(), rev, Direction::Reverse);

	std::shuffle(directions.begin(), directions.end(), std::default_random_engine());

	size_t id = 0;
	for (auto& packet : packetsSpan) {
		if (packet._direction == Direction::Unknown) {
			packet._direction = directions[id++];
		}
	}
}

void Flow::PlanPacketsTimestamps()
{
	PacketFlowSpan packetsSpan(this, false);

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int64_t> secDis(_tsFirst.GetSec(), _tsLast.GetSec());
	std::uniform_int_distribution<int64_t> usecDis(0, 999999);
	std::uniform_int_distribution<int64_t> firstUsecDis;
	std::uniform_int_distribution<int64_t> lastUsecDis;
	if (_tsFirst.GetSec() == _tsLast.GetSec()) {
		firstUsecDis
			= std::uniform_int_distribution<int64_t>(_tsFirst.GetUsec(), _tsLast.GetUsec());
		lastUsecDis = std::uniform_int_distribution<int64_t>(_tsFirst.GetUsec(), _tsLast.GetUsec());
	} else {
		firstUsecDis = std::uniform_int_distribution<int64_t>(_tsFirst.GetUsec(), 999999);
		lastUsecDis = std::uniform_int_distribution<int64_t>(0, _tsLast.GetUsec());
	}

	std::vector<Timeval> timestamps({_tsFirst, _tsLast});

	size_t timestampsToGen = _packets.size() > 2 ? _packets.size() - 2 : 0;

	for (size_t i = 0; i < timestampsToGen; i++) {
		timeval time;
		time.tv_sec = secDis(gen);
		if (time.tv_sec == _tsFirst.GetSec()) {
			time.tv_usec = firstUsecDis(gen);
		} else if (time.tv_sec == _tsLast.GetSec()) {
			time.tv_usec = lastUsecDis(gen);
		} else {
			time.tv_usec = usecDis(gen);
		}
		timestamps.emplace_back(time);
	}

	std::sort(timestamps.begin(), timestamps.end(), [](const Timeval& a, const Timeval& b) {
		return a < b;
	});

	size_t id = 0;
	for (auto& packet : packetsSpan) {
		packet._timestamp = timestamps[id++];
	}
}

void Flow::PlanPacketsSizes()
{
	auto fwdGen = PacketSizeGenerator::Construct(PACKET_SIZE_PROBABILITIES, _fwdPackets, _fwdBytes);
	auto revGen = PacketSizeGenerator::Construct(PACKET_SIZE_PROBABILITIES, _revPackets, _revBytes);

	for (auto& packet : _packets) {
		if (packet._isFinished) {
			auto& generator = packet._direction == Direction::Forward ? fwdGen : revGen;
			generator->GetValueExact(packet._size);
		}
	}

	fwdGen->PlanRemaining();
	revGen->PlanRemaining();

	for (auto& packet : _packets) {
		if (!packet._isFinished) {
			auto& generator = packet._direction == Direction::Forward ? fwdGen : revGen;
			packet._size = std::max(
				packet._size,
				generator->GetValue()); // NOTE: Add the option to GetValue to choose minimum size?
		}
	}

	fwdGen->PrintReport();
	revGen->PrintReport();
}

} // namespace generator
