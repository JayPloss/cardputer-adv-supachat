#pragma once
// The outbox: every text typed on this device waits here until the server has it, across
// restarts, room switches and out-of-memory restarts of the chat task. Nothing leaves it except
// the server's answer that it stored the message. A message the server refuses for good stays as
// FAILED, shown in red, until the person retries it. No Arduino dependency, so
// firmware/tools/outbox-test.cpp runs it on the PC.
//
// Before v0.57 a queued message lived only in the chat's list of 40 recent messages, was retried
// only while its room was on screen, was never shown as unsent, and was pushed off the list (and
// lost) by 40 newer messages from other people. Those are the silent drops this replaces.
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace outbox {

constexpr size_t kMaxEntries = 16;         // About 3 KB in NVS at the 140-character limit.
constexpr uint32_t kFirstRetryMs = 5000;
constexpr uint32_t kMaxRetryMs = 300000;

enum class State : uint8_t { Sending = 0, Failed = 1 };
enum class Outcome : uint8_t { Accepted, Retry, Failed };

struct Entry {
  std::string clientId, roomId, body;
  int64_t replyToId = 0;
  int64_t createdAt = 0;
  uint16_t attempts = 0;
  State state = State::Sending;
  int lastStatus = 0;          // The last HTTP status, or a negative transport error.
  uint32_t nextAttemptAt = 0;  // millis(); not kept across restarts, so a restart retries at once.
};

// What an answer to POST /api/messages means for the message.
inline Outcome classify(int status) {
  if (status == 200 || status == 201) return Outcome::Accepted;
  if (status <= 0) return Outcome::Retry;  // No answer: DNS, TCP, TLS, timeout.
  if (status == 408 || status == 425 || status == 429 || status >= 500) return Outcome::Retry;
  return Outcome::Failed;  // 400 too long or empty, 401 device login, 403 room, 409 id conflict.
}

inline uint32_t backoffMs(uint16_t attempts) {
  uint32_t wait = kFirstRetryMs;
  for (uint16_t i = 1; i < attempts && wait < kMaxRetryMs; i++) wait *= 2;
  return wait > kMaxRetryMs ? kMaxRetryMs : wait;
}

// Rounds of modular time: true when `at` has come, across the 49-day millis() wrap.
inline bool due(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

inline void recordAttempt(Entry &entry, int status, uint32_t now) {
  entry.attempts++; entry.lastStatus = status;
  if (classify(status) == Outcome::Failed) entry.state = State::Failed;
  else entry.nextAttemptAt = now + backoffMs(entry.attempts);
}

inline void retry(Entry &entry, uint32_t now) { entry.state = State::Sending; entry.attempts = 0; entry.nextAttemptAt = now; }

// --- Storage: one line per entry, tab-separated, with \t \n \\ escaped in each field ---
inline std::string escape(const std::string &text) {
  std::string out; out.reserve(text.size());
  for (char c : text) {
    if (c == '\\') out += "\\\\"; else if (c == '\t') out += "\\t"; else if (c == '\n') out += "\\n"; else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

inline std::string unescape(const std::string &text) {
  std::string out; out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] != '\\' || i + 1 == text.size()) { out += text[i]; continue; }
    const char next = text[++i];
    out += next == 't' ? '\t' : next == 'n' ? '\n' : next == 'r' ? '\r' : next;
  }
  return out;
}

inline std::string serialize(const std::vector<Entry> &entries) {
  std::string out;
  for (const auto &entry : entries) {
    out += escape(entry.clientId) + '\t' + escape(entry.roomId) + '\t' + std::to_string(static_cast<long long>(entry.replyToId)) + '\t' +
           std::to_string(static_cast<long long>(entry.createdAt)) + '\t' + std::to_string(static_cast<int>(entry.state)) + '\t' +
           std::to_string(entry.lastStatus) + '\t' + escape(entry.body) + '\n';
  }
  return out;
}

inline std::vector<Entry> parse(const std::string &text) {
  std::vector<Entry> entries;
  size_t start = 0;
  while (start < text.size() && entries.size() < kMaxEntries) {
    size_t end = text.find('\n', start); if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(start, end - start); start = end + 1;
    std::vector<std::string> fields; size_t from = 0;
    for (size_t i = 0; i <= line.size(); i++) if (i == line.size() || line[i] == '\t') { fields.push_back(line.substr(from, i - from)); from = i + 1; }
    if (fields.size() != 7 || fields[0].empty() || fields[1].empty() || fields[6].empty()) continue;  // A torn line is skipped, not guessed.
    Entry entry;
    entry.clientId = unescape(fields[0]); entry.roomId = unescape(fields[1]);
    entry.replyToId = strtoll(fields[2].c_str(), nullptr, 10); entry.createdAt = strtoll(fields[3].c_str(), nullptr, 10);
    entry.state = fields[4] == "1" ? State::Failed : State::Sending; entry.lastStatus = atoi(fields[5].c_str());
    entry.body = unescape(fields[6]);
    entries.push_back(entry);
  }
  return entries;
}

}  // namespace outbox
