/**
 * @file
 * @author Michal Sedlak <sedlakm@cesnet.cz>
 * @brief Generators of addresses used in flows
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "addressgenerators.h"

namespace generator {

AddressGenerators::AddressGenerators(uint32_t seed)
{
	if (seed <= 0 || seed >= 0x7fffffff) {
		throw std::invalid_argument("Invalid seed value, seed must be in range 1 - 2147483645");
	}
	_seedState = seed;
	NextSeed();
}

MacAddress AddressGenerators::GenerateMac()
{
	uint32_t value0 = NextValue();
	uint32_t value1 = NextValue();

	std::array<uint8_t, 6> bytes = {
		uint8_t((value0 >> 24) & 0xFF),
		uint8_t((value0 >> 16) & 0xFF),
		uint8_t((value0 >> 8) & 0xFF),
		uint8_t(value0 & 0xFF),
		uint8_t((value1 >> 24) & 0xFF),
		uint8_t((value1 >> 16) & 0xFF),
	};

	return MacAddress{bytes.data()};
}

IPv4Address AddressGenerators::GenerateIPv4()
{
	uint32_t value = NextValue();

	std::array<uint8_t, 4> bytes = {
		uint8_t((value >> 24) & 0xFF),
		uint8_t((value >> 16) & 0xFF),
		uint8_t((value >> 8) & 0xFF),
		uint8_t(value & 0xFF),
	};

	return IPv4Address{bytes.data()};
}

IPv6Address AddressGenerators::GenerateIPv6()
{
	uint32_t value0 = NextValue();
	uint32_t value1 = NextValue();
	uint32_t value2 = NextValue();
	uint32_t value3 = NextValue();

	std::array<uint8_t, 16> bytes = {
		uint8_t((value0 >> 24) & 0xFF),
		uint8_t((value0 >> 16) & 0xFF),
		uint8_t((value0 >> 8) & 0xFF),
		uint8_t(value0 & 0xFF),
		uint8_t((value1 >> 24) & 0xFF),
		uint8_t((value1 >> 16) & 0xFF),
		uint8_t((value1 >> 8) & 0xFF),
		uint8_t(value1 & 0xFF),
		uint8_t((value2 >> 24) & 0xFF),
		uint8_t((value2 >> 16) & 0xFF),
		uint8_t((value2 >> 8) & 0xFF),
		uint8_t(value2 & 0xFF),
		uint8_t((value3 >> 24) & 0xFF),
		uint8_t((value3 >> 16) & 0xFF),
		uint8_t((value3 >> 8) & 0xFF),
		uint8_t(value3 & 0xFF),
	};

	return IPv6Address{bytes.data()};
}

uint32_t AddressGenerators::NextValue()
{
	/* Based on https://en.wikipedia.org/wiki/Lehmer_random_number_generator#Parameters_in_common_use
	 *
	 * Generating values depends only on the current state.
	 *
	 * The period of the pseudorandom generator is 0x7fffffff - 1.
	 *
	 * Therefore we should be able to generate 0x7fffffff - 1 values before we can get the
	 * same value again.
	 *
	 * When that happens, a new seed is generated by another pseudorandom generator to avoid
	 * getting the same sequence of values.
	 *
	 * Since the pseudorandom generator used for generation of the seed works the same way,
	 * the generated seed is guaranteed to differ.
	 */
	if (_capacity == 0) {
		NextSeed();
	}
	_capacity--;

	_state = uint64_t(_state) * 48271 % 0x7fffffff;
	return _state;
}

void AddressGenerators::NextSeed()
{
	_capacity = 0x7fffffff - 1;
	_seedState = uint64_t(_seedState) * 48271 % 0x7fffffff;
	_state = _seedState;
}

} // namespace generator
