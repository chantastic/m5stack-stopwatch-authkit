// Host-only behavioral checks. No device, network, or Arduino dependencies.
#include "../firmware/devices_badge/orientation_filter.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Accel { float x, y, z; };
constexpr Accel up{0, -1, 0};
constexpr Accel right{1, 0, 0};
constexpr Accel down{0, 1, 0};
constexpr Accel left{-1, 0, 0};
constexpr Accel flat{0, 0, 1};

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

bool sample(OrientationFilter& filter, Accel a, uint32_t time, bool touching = false) {
  const auto before = filter.rotation();
  const bool changed = filter.update(a.x, a.y, a.z, time, touching);
  require(filter.rotation() <= 3, "Rotation left the four cardinal choices");
  require(changed == (before != filter.rotation()), "Return value disagrees with observed rotation change");
  require(!touching || !changed, "Rotation changed during a touch");
  return changed;
}

std::vector<uint8_t> feed(OrientationFilter& filter, Accel a, uint32_t start,
                          unsigned count, uint32_t interval = 50, bool touching = false) {
  std::vector<uint8_t> changes;
  for (unsigned i = 0; i < count; ++i) {
    if (sample(filter, a, uint32_t(start + i * interval), touching))
      changes.push_back(filter.rotation());
  }
  return changes;
}

void cardinal_directions() {
  // Input is support force in display rotation-zero axes, not gravity.
  const Accel positions[] = {up, right, down, left};
  for (uint8_t target = 0; target < 4; ++target) {
    OrientationFilter filter;
    require(filter.rotation() == 0, "Initial orientation is not zero");
    const auto changes = feed(filter, positions[target], 0, 41);
    const std::vector<uint8_t> expected = target == 0 ? std::vector<uint8_t>{}
                                                   : std::vector<uint8_t>{target};
    require(changes == expected, "Stable cardinal input selected an incorrect or repeated orientation");
    require(filter.rotation() == target, "Cardinal orientation was not reached");
    require(feed(filter, positions[target], 2050, 41).empty(), "Unchanged posture emitted repeated rotation events");
  }

  // Exercise changing between warmed-up orientations, including returning to 0.
  OrientationFilter filter;
  uint32_t now = 0;
  for (uint8_t target : {uint8_t(2), uint8_t(1), uint8_t(3), uint8_t(0)}) {
    require(feed(filter, positions[target], now, 61) == std::vector<uint8_t>{target},
            "A warmed-up cardinal transition did not settle exactly once");
    now += 3050;
  }
}

void settling_delay() {
  OrientationFilter filter;
  require(feed(filter, right, 100, 14).empty(), "Rotation changed before 650 ms of settling");
  require(!sample(filter, right, 799), "Rotation changed before the 700 ms stability delay");
  require(sample(filter, right, 800) && filter.rotation() == 1,
          "Continuously stable orientation did not commit after 700 ms");
  require(!sample(filter, right, 801), "Committed orientation emitted a duplicate event");
}

void flat_and_diagonal_hold() {
  OrientationFilter filter;
  require(feed(filter, down, 0, 21) == std::vector<uint8_t>{2}, "Test did not establish a non-default orientation");
  require(feed(filter, flat, 1050, 80).empty(), "Laying the badge flat changed orientation");
  require(filter.rotation() == 2, "Flat posture lost the previous orientation");
  uint32_t now = 5050;
  for (unsigned i = 0; i < 160; ++i, now += 50) {
    const Accel a = i % 2 ? Accel{0.70f, 0.714f, 0} : Accel{0.714f, 0.70f, 0};
    require(!sample(filter, a, now), "Small diagonal jitter selected an orientation");
  }
  require(filter.rotation() == 2, "Diagonal posture lost the previous orientation");
  require(feed(filter, {0.3f, -0.25f, 0.92f}, now, 60).empty(),
          "Near-flat tilt selected an orientation");
}

void short_cardinal_excursions_do_not_rotate() {
  OrientationFilter filter;
  // Neither direction is sustained for the required duration. This also
  // exercises smoothing through opposing inputs without prescribing its math.
  uint32_t now = 0;
  for (unsigned cycle = 0; cycle < 30; ++cycle) {
    const Accel a = cycle % 2 ? left : right;
    require(feed(filter, a, now, 6).empty(), "Brief alternating turns caused a rotation");
    now += 300;
  }
  require(filter.rotation() == 0, "Jitter changed the initial orientation");
  require(feed(filter, left, now, 60) == std::vector<uint8_t>{3},
          "The filter failed to settle after jitter stopped");
}

void fresh_ambiguous_sample_cancels_pending_rotation() {
  const Accel interruptions[] = {flat, {0.707f, 0.707f, 0}, left, up};
  for (const auto interruption : interruptions) {
    OrientationFilter filter;
    require(feed(filter, right, 0, 14).empty(), "Setup rotated before 700 ms");
    // The next sample would commit the old pose. A fresh flat, diagonal, or
    // different cardinal sample must cancel it even while smoothing lags.
    require(!sample(filter, interruption, 700),
            "Stale filtered gravity committed after the current pose changed");
    require(filter.rotation() == 0, "Interrupted candidate changed the display");
    require(feed(filter, right, 750, 14).empty(),
            "An interrupted candidate retained old settling progress");
    require(feed(filter, right, 1450, 30) == std::vector<uint8_t>{1},
            "Fresh stable samples did not recover exactly once after interruption");
  }
}

void missing_samples_restart_settling() {
  OrientationFilter filter;
  require(feed(filter, right, 0, 13).empty(), "Setup rotated too early");
  // The ten-second gap represents a blocked network operation or sensor loss.
  require(!sample(filter, right, 10600), "Unsampled time counted as stable orientation");
  require(feed(filter, right, 10650, 13).empty(), "Settling did not restart after a sample gap");
  require(sample(filter, right, 11300), "Rotation did not recover after fresh stable samples");
}

void explicit_invalidation_restarts_settling() {
  OrientationFilter filter;
  require(feed(filter, right, 0, 13).empty(), "Setup rotated too early");
  filter.invalidate();
  require(filter.rotation() == 0, "Invalidation changed the visible orientation");
  require(feed(filter, right, 650, 14).empty(), "Invalidation retained old settling progress");
  require(sample(filter, right, 1350), "Filter failed to recover after invalidation");
}

void invalid_sensor_values() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();
  const float huge = std::numeric_limits<float>::max();
  const Accel bad[] = {
    {nan, 0, 1}, {0, nan, 1}, {0, 1, nan},
    {infinity, 0, 0}, {0, -infinity, 0}, {0, 0, infinity},
    {huge, huge, huge}, {0, 0, 0}, {0.6f, 0, 0}, {1.4f, 0, 0},
  };
  for (const auto invalid : bad) {
    OrientationFilter filter;
    require(feed(filter, right, 0, 13).empty(), "Setup rotated too early");
    require(!sample(filter, invalid, 650), "Invalid acceleration triggered a rotation");
    require(filter.rotation() == 0, "Invalid acceleration changed the visible orientation");
    require(feed(filter, right, 700, 14).empty(), "Invalid data retained prior stability progress");
    require(sample(filter, right, 1400), "Filter failed to recover after invalid sensor data");
  }

  OrientationFilter filter;
  require(feed(filter, left, 0, 21) == std::vector<uint8_t>{3}, "Setup did not select left");
  uint32_t now = 1050;
  for (const auto invalid : bad) {
    require(!sample(filter, invalid, now), "Invalid data rotated an established orientation");
    require(filter.rotation() == 3, "Invalid data erased the last good orientation");
    now += 50;
  }
}

void touch_blocks_rotation_until_new_settling() {
  OrientationFilter filter;
  require(feed(filter, right, 0, 13).empty(), "Setup rotated too early");
  require(feed(filter, right, 650, 28, 50, true).empty(), "Held touch allowed a pending rotation");
  require(filter.rotation() == 0, "Held touch changed the visible orientation");
  // A nearly-complete candidate before touching must not fire on release.
  require(feed(filter, right, 2050, 14).empty(), "Releasing touch retained old settling time");
  require(sample(filter, right, 2750), "Stable orientation did not settle after touch release");
  require(filter.rotation() == 1, "Touch-release transition selected the wrong orientation");
}

void millis_wraparound() {
  OrientationFilter filter;
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 300;
  require(feed(filter, right, start, 14).empty(), "Millisecond wrap caused premature rotation");
  require(sample(filter, right, uint32_t(start + 700)), "Stable candidate failed across millisecond wrap");
  require(filter.rotation() == 1, "Millisecond wrap selected an invalid orientation");
}

void sample_gap_across_millis_wrap() {
  OrientationFilter filter;
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 100;
  require(!sample(filter, left, start), "Initial sample rotated immediately");
  require(!sample(filter, left, uint32_t(start + 600)), "Wrapped sample gap counted as settling");
  require(feed(filter, left, uint32_t(start + 650), 13).empty(), "Wrapped gap did not reset candidate time");
  require(sample(filter, left, uint32_t(start + 1300)), "Filter did not recover after wrapped sample gap");
  require(filter.rotation() == 3, "Wrapped recovery selected the wrong orientation");
}
}  // namespace

int main() {
  const struct { const char* name; void (*run)(); } checks[] = {
    {"cardinal directions and stable repeated samples", cardinal_directions},
    {"700 ms settling delay", settling_delay},
    {"flat, diagonal, and near-flat hold", flat_and_diagonal_hold},
    {"short excursions and jitter recovery", short_cardinal_excursions_do_not_rotate},
    {"fresh ambiguous or changed pose cancels pending rotation", fresh_ambiguous_sample_cancels_pending_rotation},
    {"missing samples restart settling", missing_samples_restart_settling},
    {"explicit invalidation restarts settling", explicit_invalidation_restarts_settling},
    {"invalid values and acceleration magnitude", invalid_sensor_values},
    {"held touch and fresh settling after release", touch_blocks_rotation_until_new_settling},
    {"millisecond counter wraparound", millis_wraparound},
    {"sample interruption across wraparound", sample_gap_across_millis_wrap},
  };
  for (const auto& check : checks) {
    try {
      check.run();
      std::cout << "PASS " << check.name << '\n';
    } catch (const std::exception& error) {
      std::cerr << "FAIL " << check.name << ": " << error.what() << '\n';
      return EXIT_FAILURE;
    }
  }
  std::cout << "All " << sizeof(checks) / sizeof(checks[0]) << " orientation behavior checks passed.\n";
  return EXIT_SUCCESS;
}
