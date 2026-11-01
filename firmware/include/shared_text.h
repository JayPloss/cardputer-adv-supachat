#pragma once

#include <Arduino.h>

#include <atomic>
#include <cstring>

// A short string that more than one task reads and writes. An Arduino String can't be shared:
// its buffer is freed and reallocated on assignment, so a reader on another task can be left
// holding freed memory. This one has no heap, copies under a spinlock held for a few bytes, and
// truncates what doesn't fit. Reads hand back an ordinary String copy.
template <size_t Capacity>
class SharedText {
 public:
  SharedText() { text_[0] = 0; }
  SharedText(const char *value) { store(value); }

  SharedText &operator=(const char *value) { store(value); return *this; }
  SharedText &operator=(const String &value) { store(value.c_str()); return *this; }
  SharedText &operator=(const SharedText &value) { store(value.get().c_str()); return *this; }
  SharedText(const SharedText &) = delete;

  String get() const {
    char copy[Capacity];
    read(copy);
    return String(copy);
  }
  operator String() const { return get(); }
  // Copies into a caller's buffer, for the fixed fields of a radio packet.
  void copyTo(char *target, size_t size) const {
    char copy[Capacity];
    read(copy);
    strncpy(target, copy, size - 1);
    target[size - 1] = 0;
  }

  bool operator==(const char *value) const { char copy[Capacity]; read(copy); return strcmp(copy, value) == 0; }
  bool operator!=(const char *value) const { return !(*this == value); }
  bool operator==(const String &value) const { return *this == value.c_str(); }
  bool operator!=(const String &value) const { return !(*this == value.c_str()); }
  bool isEmpty() const { char copy[Capacity]; read(copy); return copy[0] == 0; }
  size_t length() const { char copy[Capacity]; read(copy); return strlen(copy); }
  bool startsWith(const char *prefix) const { char copy[Capacity]; read(copy); return strncmp(copy, prefix, strlen(prefix)) == 0; }
  String substring(unsigned int from) const { return get().substring(from); }
  String substring(unsigned int from, unsigned int to) const { return get().substring(from, to); }

 private:
  void store(const char *value) {
    Guard guard(busy_);
    strncpy(text_, value ? value : "", Capacity - 1);
    text_[Capacity - 1] = 0;
  }
  void read(char *copy) const {
    Guard guard(busy_);
    memcpy(copy, text_, Capacity);
  }
  struct Guard {
    explicit Guard(std::atomic_flag &flag) : flag_(flag) { while (flag_.test_and_set(std::memory_order_acquire)) {} }
    ~Guard() { flag_.clear(std::memory_order_release); }
    std::atomic_flag &flag_;
  };
  char text_[Capacity];
  mutable std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
};

template <size_t Capacity>
String operator+(const char *left, const SharedText<Capacity> &right) { return String(left) + right.get(); }
template <size_t Capacity>
String operator+(const String &left, const SharedText<Capacity> &right) { return left + right.get(); }
template <size_t Capacity>
bool operator==(const String &left, const SharedText<Capacity> &right) { return right == left; }
template <size_t Capacity>
bool operator!=(const String &left, const SharedText<Capacity> &right) { return right != left; }
