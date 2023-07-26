/**
 * @file
 * @author Michal Sedlak <sedlakm@cesnet.cz>
 * @brief Packet size value generator (slow variant) implementation file
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "packetsizegeneratorslow.h"
#include "randomgenerator.h"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <random>

namespace generator {

// Maximum number of attempts when generating
static constexpr int MAX_ATTEMPTS = 2000;

// How much can the generated byte count differ from the desired one
static constexpr double MAX_DIFF_RATIO = 0.01;

// Minimal allowed difference, as we might never be able to reach the exact amount with smaller byte
// counts
static constexpr double MIN_DIFF = 50;

// The maximum distance to look when finding a suitable value when choosing an exact value
static constexpr int GET_EXACT_MAX_DISTANCE = 1000;

// If the generated sum differs by the desired sum by this much percent, fallback to uniform
// distribution.
static constexpr double DIFF_RATIO_FALLBACK_TO_UNIFORM = 0.2;

static double SumProbabilities(const std::vector<IntervalInfo>& intervals)
{
	return std::accumulate(
		intervals.begin(),
		intervals.end(),
		0.0,
		[](double sum, const auto& inter) { return sum += inter._probability; });
}

static uint64_t
GenerateRandomValue(const std::vector<IntervalInfo>& intervals, double intervalProbSum)
{
	double probSum = 0.0f;
	double genVal = RandomGenerator::GetInstance().RandomDouble(0.0, intervalProbSum);
	uint64_t value = 0;
	for (const auto& inter : intervals) {
		probSum += inter._probability;
		if (genVal <= probSum) {
			value = RandomGenerator::GetInstance().RandomUInt(inter._from, inter._to);
			break;
		}
	}
	return value;
}

PacketSizeGeneratorSlow::PacketSizeGeneratorSlow(
	const std::vector<IntervalInfo>& intervals,
	uint64_t numPkts,
	uint64_t numBytes)
	: _intervals(intervals)
	, _numPkts(numPkts)
	, _numBytes(numBytes)
{
	assert(!_intervals.empty());
}

void PacketSizeGeneratorSlow::PlanRemaining()
{
	uint64_t remPkts = _numPkts >= _assignedPkts ? _numPkts - _assignedPkts : 0;
	uint64_t remBytes = _numBytes >= _assignedBytes ? _numBytes - _assignedBytes : 0;
	Generate(remPkts, remBytes);
}

void PacketSizeGeneratorSlow::Generate(uint64_t desiredPkts, uint64_t desiredBytes)
{
	auto intervals = _intervals;

	double probSum = SumProbabilities(intervals);
	uint64_t valuesSum = 0;
	_values.resize(desiredPkts);

	if (desiredPkts == 0 || desiredBytes == 0) {
		return;
	}

	if (desiredPkts == 1) {
		_values[0] = desiredBytes;
		return;
	}

	for (auto& value : _values) {
		value = GenerateRandomValue(intervals, probSum);
		valuesSum += value;
	}

	double maxDiff = std::max<uint64_t>(MAX_DIFF_RATIO * desiredBytes, MIN_DIFF);
	uint64_t targetMin = maxDiff < desiredBytes ? desiredBytes - maxDiff : 0;
	uint64_t targetMax = desiredBytes + maxDiff;

	_logger->trace("VALUES sum={} desired={}", valuesSum, desiredBytes);
	for (auto value : _values) {
		_logger->trace(value);
	}

	int numAttempts = MAX_ATTEMPTS;
	uint64_t bestDiff
		= valuesSum > desiredBytes ? valuesSum - desiredBytes : desiredBytes - valuesSum;
	std::vector<uint64_t> bestValues = _values;
	while ((valuesSum < targetMin || valuesSum > targetMax) && numAttempts > 0) {
		numAttempts--;

		uint64_t avgValue = valuesSum / desiredPkts;
		std::vector<IntervalInfo> origIntervals = intervals;
		if (valuesSum < targetMin) {
			for (auto& inter : intervals) {
				uint64_t interAvg = inter._from / 2 + inter._to / 2;
				if (interAvg < avgValue) {
					inter._probability = 0.0;
				}
			}
		} else if (valuesSum > targetMax) {
			for (auto& inter : intervals) {
				uint64_t interAvg = inter._from / 2 + inter._to / 2;
				if (interAvg > avgValue) {
					inter._probability = 0.0;
				}
			}
		}
		probSum = SumProbabilities(intervals);

		for (auto& value : _values) {
			uint64_t newValue = GenerateRandomValue(intervals, probSum);
			valuesSum = valuesSum - value + newValue;
			value = newValue;

			if (valuesSum >= targetMin && valuesSum <= targetMax) {
				break;
			}

			uint64_t diff
				= valuesSum > desiredBytes ? valuesSum - desiredBytes : desiredBytes - valuesSum;
			if (diff < bestDiff) {
				bestValues = _values;
				bestDiff = diff;
			}
		}

		_logger->trace("VALUES sum={} desired={}", valuesSum, desiredBytes);
		for (auto value : _values) {
			_logger->trace(value);
		}

		intervals = origIntervals;

		uint64_t diff
			= valuesSum > desiredBytes ? valuesSum - desiredBytes : desiredBytes - valuesSum;
		if (diff < bestDiff) {
			bestValues = _values;
			bestDiff = diff;
		}
	}

	double finalDiffRatio = double(bestDiff) / double(desiredBytes);
	_logger
		->trace("Final diff: {}, ratio: {}, desired: {}", bestDiff, finalDiffRatio, desiredBytes);

	if (finalDiffRatio > DIFF_RATIO_FALLBACK_TO_UNIFORM) {
		std::fill(_values.begin(), _values.end(), desiredBytes / desiredBytes);
		_logger->info(
			"Generated values difference too large {}, fallback to uniform distribution",
			finalDiffRatio);
	} else {
		_values = std::move(bestValues);
		std::shuffle(_values.begin(), _values.end(), std::default_random_engine());
	}
}

uint64_t PacketSizeGeneratorSlow::GetValue()
{
	uint64_t value;
	if (!_values.empty()) {
		value = _values.back();
		_values.pop_back();
	} else {
		// No more values left, generate one randomly
		value = GenerateRandomValue(_intervals, SumProbabilities(_intervals));
	}

	_assignedPkts++;
	_assignedBytes += value;

	return value;
}

void PacketSizeGeneratorSlow::GetValueExact(uint64_t value)
{
	if (_values.size() == 0) {
		_assignedPkts++;
		_assignedBytes += value;
		return;
	}

	size_t start;
	size_t end;
	if (_values.size() <= GET_EXACT_MAX_DISTANCE) {
		start = 0;
		end = _values.size();
	} else {
		start
			= RandomGenerator::GetInstance().RandomUInt(0, _values.size() - GET_EXACT_MAX_DISTANCE);
		end = start + GET_EXACT_MAX_DISTANCE;
	}

	size_t closest = 0;
	uint64_t closestDiff = value;
	for (size_t i = start; i < end; i++) {
		uint64_t diff = _values[i] > value ? _values[i] - value : value - _values[i];
		if (diff < closestDiff) {
			closest = i;
			closestDiff = diff;
		}
	}

	// Remove the chosen value
	std::swap(_values[closest], _values[_values.size() - 1]);
	_values.pop_back();

	_assignedPkts++;
	_assignedBytes += value;
}

void PacketSizeGeneratorSlow::PrintReport()
{
	double dBytes = _numBytes == 0
		? 0.0
		: std::abs(int64_t(_numBytes) - int64_t(_assignedBytes)) / double(_numBytes);

	double dPkts = _numPkts == 0
		? 0.0
		: std::abs(int64_t(_numPkts) - int64_t(_assignedPkts)) / double(_numPkts);

	_logger->debug(
		"[Bytes] target={} actual={} (diff={:.2f}%)  [Pkts] target={} actual={} "
		"(diff={:.2f}%)",
		_numBytes,
		_assignedBytes,
		dBytes * 100.0,
		_numPkts,
		_assignedPkts,
		dPkts * 100.0);
}

} // namespace generator
