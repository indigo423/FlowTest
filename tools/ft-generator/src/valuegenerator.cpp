/**
 * @file
 * @author Michal Sedlak <sedlakm@cesnet.cz>
 * @brief Packet size value generator
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "valuegenerator.h"

#include <algorithm>
#include <cassert>

// Maximum number of attempts when generating
static constexpr int MAX_ATTEMPTS = 100;

// How much can the generated byte count differ from the desired one
static constexpr double MAX_DIFF_RATIO = 0.1;

// Minimal allowed difference, as we might never be able to reach the exact amount with smaller byte counts
static constexpr double MIN_DIFF = 50;

ValueGenerator::ValueGenerator(uint64_t count, uint64_t desiredSum, const std::vector<IntervalInfo>& intervals)
	: _count(count), _desiredSum(desiredSum), _intervals(intervals)
{
    Generate();
}

void ValueGenerator::Generate()
{
	uint64_t valuesSum = 0;
	_values.resize(_count);
	for (auto& value : _values) {
		value = GenerateRandomValue();
		valuesSum += value;
	}

	double maxDiff = std::max<uint64_t>(MAX_DIFF_RATIO * _desiredSum, MIN_DIFF);
	uint64_t targetMin = maxDiff < _desiredSum ? _desiredSum - maxDiff : 0;
	uint64_t targetMax = _desiredSum + maxDiff;

	_logger->trace("VALUES sum={} desired={}", valuesSum, _desiredSum);
	for (auto value : _values) {
		_logger->trace(value);
	}

	int numAttempts = MAX_ATTEMPTS;
	while ((valuesSum < targetMin || valuesSum > targetMax) && numAttempts > 0) {
		numAttempts--;

		uint64_t avgValue = valuesSum / _count;
		std::vector<IntervalInfo> origIntervals = _intervals;
		if (valuesSum < targetMin) {
			for (auto& inter : _intervals) {
				uint64_t interAvg = inter._from / 2 + inter._to / 2;
				if (interAvg < avgValue) {
					inter._probability = 0.0;
				}
			}
		} else {
			for (auto& inter : _intervals) {
				uint64_t interAvg = inter._from / 2 + inter._to / 2;
				if (interAvg < avgValue) {
					inter._probability = 0.0;
				}
			}
		}
		PostIntervalUpdate();

		for (auto& value : _values) {
			uint64_t newValue = GenerateRandomValue();
			valuesSum = valuesSum - value + newValue;
			value = newValue;

			if (valuesSum >= targetMin && valuesSum <= targetMax) {
				break;
			}
		}

		_logger->trace("VALUES sum={} desired={}", valuesSum, _desiredSum);
		for (auto value : _values) {
			_logger->trace(value);
		}

		_intervals = origIntervals;
		PostIntervalUpdate();
	}

	std::random_shuffle(_values.begin(), _values.end());
}

uint64_t ValueGenerator::GetValue()
{
	uint64_t value = _values.back();
	_values.pop_back();
	return value;
}

uint64_t ValueGenerator::GetValueExact(uint64_t value)
{
	//TODO: optimize by picking any value from corresponding interval. This is not ideal.
	size_t closest = 0;
	uint64_t closestDiff = value;
	for (size_t i = 0; i < _values.size(); i++) {
		uint64_t diff = _values[i] > value ? _values[i] - value : value - _values[i];
		if (diff < closestDiff) {
			closest = i;
			closestDiff = diff;
		}
	}

	std::swap(_values[closest], _values[_values.size() - 1]);

	uint64_t chosenValue = _values.back();
	_values.pop_back();
	return chosenValue;
}

void ValueGenerator::PostIntervalUpdate()
{
	double intervalProbSum = std::accumulate(_intervals.begin(), _intervals.end(), 0.0,
		[](double sum, const auto& inter) { return sum += inter._probability; });
	_distr = std::uniform_real_distribution<>{0.0, intervalProbSum};
}

uint64_t ValueGenerator::GenerateRandomValue()
{
	//TODO: use uniform_int_distribution which should probably be more effective?
	double probSum = 0.0f;
	double genVal = _distr(_gen);
	uint64_t value = 0;
	for (const auto& inter : _intervals) {
		probSum += inter._probability;
		if (genVal <= probSum) {
			std::uniform_int_distribution<uint64_t> interDistr{inter._from, inter._to};
			value = interDistr(_gen);
			break;
		}
	}
	return value;
}
