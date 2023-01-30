/**
 * @file
 * @author Pavel Siska <siska@cesnet.cz>
 * @author Matej Hulak <hulak@cesnet.cz>
 * @brief Packet implementation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "packet.hpp"

#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <xxhash.h>

namespace replay {

uint32_t Packet::GetHash() const
{
	if (info.l3Type == L3Type::IPv4) {
		return CalculateIpv4Hash();
	} else {
		return CalculateIpv6Hash();
	}
}

uint32_t Packet::CalculateIpv4Hash() const
{
	iphdr* ipHeader = reinterpret_cast<iphdr*>(data.get() + info.l3Offset);
	uint32_t hashSrcIp = XXH32(&ipHeader->saddr, sizeof(ipHeader->saddr), 0);
	uint32_t hashDstIp = XXH32(&ipHeader->daddr, sizeof(ipHeader->daddr), 0);
	return hashSrcIp ^ hashDstIp;
}

uint32_t Packet::CalculateIpv6Hash() const
{
	ip6_hdr* ipHeader = reinterpret_cast<ip6_hdr*>(data.get() + info.l3Offset);
	uint32_t hashSrcIp = XXH32(&ipHeader->ip6_src, sizeof(ipHeader->ip6_src), 0);
	uint32_t hashDstIp = XXH32(&ipHeader->ip6_dst, sizeof(ipHeader->ip6_dst), 0);
	return hashSrcIp ^ hashDstIp;
}

} // namespace replay
