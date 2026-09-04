#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace supachat::fox {

constexpr std::size_t kMaxPlacePrintEntries = 12;
constexpr std::size_t kMaxPeers = 8;
constexpr uint32_t kPeerFreshMs = 45000;
constexpr uint32_t kSignalLostMs = 5000;
constexpr uint32_t kSessionExpiryMs = 10 * 60 * 1000;
constexpr uint32_t kFoxBeaconMs = 500;
constexpr std::size_t kMaxPackHunters = 6;
constexpr uint32_t kPackObservationMs = 1000;
constexpr uint32_t kPackFreshMs = 4000;

struct __attribute__((packed)) PlacePrintEntry {
  uint8_t bssid[6]{};
  int8_t rssi = -127;
  uint8_t channel = 0;
};

struct __attribute__((packed)) PlacePrint {
  uint16_t scanMs = 0;
  uint8_t count = 0;
  uint8_t truncated = 0;
  PlacePrintEntry entries[kMaxPlacePrintEntries]{};
};

struct Match {
  uint8_t overlap = 0;
  uint8_t unionCount = 0;
  uint8_t similarity = 0;
  uint8_t confidence = 0;
  int16_t meanRssiDelta = 0;
};

struct __attribute__((packed)) PackObservation {
  uint32_t sessionId = 0;
  char quarryId[6]{};
  int8_t directRssi = -127;
  int8_t trendDelta = 0;
  uint8_t placeSimilarity = 0;
  uint8_t placeConfidence = 0;
  uint8_t placeOverlap = 0;
  uint16_t sequence = 0;
};

struct PackEvidence {
  char hunterId[6]{};
  int8_t directRssi = -127;
  int8_t trendDelta = 0;
  uint8_t placeSimilarity = 0;
  uint8_t placeConfidence = 0;
  uint32_t receivedAt = 0;
};

inline int evidenceScore(const PackEvidence &evidence) {
  if (evidence.directRssi <= -127) return -1000;
  return static_cast<int>(evidence.directRssi) * 3 + evidence.placeSimilarity * evidence.placeConfidence / 3;
}

inline int bestEvidence(const PackEvidence *items, std::size_t count, uint32_t now) {
  int best = -1, bestScore = -1001;
  for (std::size_t index = 0; index < count; ++index) {
    if (now - items[index].receivedAt > kPackFreshMs) continue;
    const int score = evidenceScore(items[index]);
    if (score > bestScore) { bestScore = score; best = static_cast<int>(index); }
  }
  return best;
}

inline Match match(const PlacePrint &local, const PlacePrint &remote) {
  Match result;
  int weightedDifference = 0;
  int totalWeight = 0;
  for (uint8_t left = 0; left < local.count; ++left) {
    for (uint8_t right = 0; right < remote.count; ++right) {
      if (std::memcmp(local.entries[left].bssid, remote.entries[right].bssid, 6) != 0) continue;
      const int strength = std::max(-95, std::min(-30, std::max<int>(local.entries[left].rssi, remote.entries[right].rssi)));
      const int weight = 1 + (strength + 95) / 16;
      weightedDifference += std::abs(static_cast<int>(local.entries[left].rssi) - remote.entries[right].rssi) * weight;
      result.meanRssiDelta += static_cast<int>(local.entries[left].rssi) - remote.entries[right].rssi;
      totalWeight += weight;
      ++result.overlap;
      break;
    }
  }
  result.unionCount = local.count + remote.count - result.overlap;
  if (result.overlap) result.meanRssiDelta /= result.overlap;
  if (!result.unionCount || !result.overlap) return result;
  const int overlapScore = result.overlap * 100 / result.unionCount;
  const int difference = totalWeight ? weightedDifference / totalWeight : 65;
  result.similarity = static_cast<uint8_t>(std::max(0, std::min(100, overlapScore - difference)));
  result.confidence = result.overlap >= 6 ? 3 : result.overlap >= 3 ? 2 : 1;
  return result;
}

class RssiTrend {
 public:
  void reset() { samples_.fill(0); count_ = 0; position_ = 0; }
  void add(int8_t rssi) {
    samples_[position_] = rssi;
    position_ = (position_ + 1) % samples_.size();
    count_ = std::min(samples_.size(), count_ + 1);
  }
  int average() const {
    if (!count_) return -127;
    int sum = 0;
    for (std::size_t index = 0; index < count_; ++index) sum += samples_[index];
    return sum / static_cast<int>(count_);
  }
  int delta() const {
    if (count_ < 6) return 0;
    int older = 0, newer = 0;
    for (std::size_t offset = 0; offset < 3; ++offset) {
      older += samples_[(position_ + samples_.size() - 6 + offset) % samples_.size()];
      newer += samples_[(position_ + samples_.size() - 3 + offset) % samples_.size()];
    }
    return newer / 3 - older / 3;
  }
  const char *guidance() const {
    if (count_ < 6) return "CALIBRATING";
    const int change = delta();
    return change >= 3 ? "WARMER" : change <= -3 ? "COLDER" : "STEADY";
  }
 private:
  std::array<int8_t, 12> samples_{};
  std::size_t count_ = 0;
  std::size_t position_ = 0;
};

static_assert(sizeof(PlacePrint) <= 104, "Place print must fit the encrypted ESP-NOW payload budget");
static_assert(sizeof(PackObservation) <= 20, "Pack observation must remain cheap at one packet per second");

}  // namespace supachat::fox
