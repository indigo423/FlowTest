/**
 * @file
 * @author Michal Sedlak <sedlakm@cesnet.cz>
 * @brief Writing of generated packets to a PCAP output file
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <pcap/pcap.h>

#include <cstddef>
#include <memory>
#include <string>

namespace generator {

/**
 * @brief A pcap writer class
 */
class PcapWriter {
public:
	/**
	 * @brief Construct a new pcap writer
	 *
	 * @param filename  The output pcap filename
	 *
	 * @throws runtime_error  When creation of the underlying pcap_t or pcap_dumper_t object failed
	 */
	explicit PcapWriter(const std::string& filename);

	/**
	 * @brief Write a packet to the output file
	 *
	 * @param data  The packet data
	 * @param length  The packet length
	 * @param timestamp  The packet timestamp
	 */
	void WritePacket(const std::byte* data, uint32_t length, timeval timestamp);

private:
	std::unique_ptr<pcap_t, decltype(&pcap_close)> _pcap{nullptr, pcap_close};
	std::unique_ptr<pcap_dumper_t, decltype(&pcap_dump_close)> _dumper{nullptr, pcap_dump_close};
};

} // namespace generator