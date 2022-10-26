/**
 * @file
 * @author Pavel Siska <siska@cesnet.cz>
 * @author Matej Hulak <hulak@cesnet.cz>
 * @brief Packet interface
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <memory>

namespace replay {

/**
 * @brief Enum representing the packet IP type
 *
 */
enum class L3Type {
	IPv4,
	IPv6,
};

/**
 * @brief Class representing the packet L3 info
 *
 */
struct PacketInfo {
	enum L3Type l3Type;
	uint16_t l3Offset;
};

/**
 * @brief Class representing the packet interface
 *
 */
class Packet {
public:

	/**
	 * @brief Get the Packet hash value
	 */
	uint32_t GetHash() const;

	/** @brief Packet data. */
	std::unique_ptr<std::byte[]> data;
	/** @brief Packet data length. */
	uint16_t dataLen;
	/** @brief Packet timestamp. */
	uint64_t timestamp;
	/** @brief Packet L3 info. */
	PacketInfo info;

private:
	uint32_t CalculateIpv4Hash() const;
	uint32_t CalculateIpv6Hash() const;
};

} // namespace replay