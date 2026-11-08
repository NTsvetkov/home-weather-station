#pragma once

#include <stdint.h>

/**
 * @file trend.h
 * @brief Outdoor trend calculation, free of Arduino dependencies.
 */

/** @brief Outdoor temperature and humidity at one observation time. */
struct ClimateSample {
  uint32_t epoch;  ///< gauge observation time (Unix seconds)
  float t;         ///< temperature (C)
  float h;         ///< humidity (%)
};

/** @brief Outdoor pressure at one observation time. */
struct PressureSample {
  uint32_t epoch;  ///< gauge observation time (Unix seconds)
  float p;         ///< pressure (hPa)
};

static constexpr uint8_t TREND_BUFFER_SIZE = 10;

/**
 * @brief Ring buffer of observations spread across one trend window.
 *
 * The gauge publishes at an irregular cadence, so samples are selected by
 * observation time. Spacing them by window / size lets the buffer span the
 * whole window; a slower gauge simply makes it span longer.
 */
template <typename Sample>
class TrendHistory {
 public:
  explicit constexpr TrendHistory(uint32_t windowS) : windowS_(windowS) {}

  /** @brief Store a sample unless the previous one is too recent. */
  void add(const Sample& sample) {
    if (count_ > 0) {
      const Sample& newest = items_[(head_ + TREND_BUFFER_SIZE - 1) % TREND_BUFFER_SIZE];
      if (sample.epoch - newest.epoch < windowS_ / TREND_BUFFER_SIZE) return;
    }
    items_[head_] = sample;
    head_         = (head_ + 1) % TREND_BUFFER_SIZE;
    if (count_ < TREND_BUFFER_SIZE) count_++;
  }

  /**
   * @brief Find the sample whose age is closest to the trend window.
   * @return false if no sample is between 75% and 150% of the window old.
   */
  bool findReference(uint32_t nowEpoch, Sample& out) const {
    const uint32_t minAge = windowS_ / 4 * 3;
    const uint32_t maxAge = windowS_ / 2 * 3;
    uint32_t bestDistance = UINT32_MAX;
    for (uint8_t i = 0; i < count_; i++) {
      const uint32_t age = nowEpoch - items_[i].epoch;
      if (age < minAge || age > maxAge) continue;
      const uint32_t distance = (age > windowS_) ? age - windowS_ : windowS_ - age;
      if (distance < bestDistance) {
        bestDistance = distance;
        out          = items_[i];
      }
    }
    return bestDistance != UINT32_MAX;
  }

 private:
  const uint32_t windowS_;
  Sample items_[TREND_BUFFER_SIZE] = {};
  uint8_t count_                   = 0;
  uint8_t head_                    = 0;
};

/**
 * @brief Calculate trend direction based on delta and threshold.
 * @return -1 (falling), 0 (steady), or +1 (rising).
 */
inline int8_t calcTrend(float current, float ref, float threshold) {
  const float d = current - ref;

  if (d >= threshold) return 1;
  if (d <= -threshold) return -1;

  return 0;
}

/**
 * @brief Tracks outdoor readings and derives their trends.
 *
 * A metric with no suitable reference reports steady. In practice that means
 * startup or a long gauge outage, when any older direction would be stale.
 */
class OutdoorTrends {
 public:
  OutdoorTrends(uint32_t climateWindowS, uint32_t pressureWindowS, float pressureThreshold)
      : climate_(climateWindowS), pressure_(pressureWindowS), pressureThreshold_(pressureThreshold) {}

  /**
   * @param observed       Epoch of the current gauge observation.
   * @param newObservation false for a repeated fetch of the same observation.
   */
  void update(uint32_t observed, bool newObservation, float t, float h, float p) {
    if (newObservation) {
      climate_.add({ observed, t, h });
      pressure_.add({ observed, p });
    }

    ClimateSample climateRef;
    const bool haveClimate = climate_.findReference(observed, climateRef);
    temperature_ = haveClimate ? calcTrend(t, climateRef.t, 0.3f) : 0;
    humidity_    = haveClimate ? calcTrend(h, climateRef.h, 2.0f) : 0;

    PressureSample pressureRef;
    pressureTrend_ = pressure_.findReference(observed, pressureRef)
        ? calcTrend(p, pressureRef.p, pressureThreshold_) : 0;
  }

  int8_t temperature() const { return temperature_; }
  int8_t humidity() const { return humidity_; }
  int8_t pressure() const { return pressureTrend_; }

 private:
  TrendHistory<ClimateSample> climate_;
  TrendHistory<PressureSample> pressure_;
  float pressureThreshold_;
  int8_t temperature_   = 0;
  int8_t humidity_      = 0;
  int8_t pressureTrend_ = 0;
};
