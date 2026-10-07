// Minimal Arduino shim for the desktop (SDL) UI simulator.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using std::max;
using std::min;

#ifndef PI
#define PI 3.14159265358979f
#endif

#define log_e(...) (printf("[E] " __VA_ARGS__), printf("\n"))
#define log_w(...) (printf("[W] " __VA_ARGS__), printf("\n"))
#define log_i(...) (printf("[I] " __VA_ARGS__), printf("\n"))

inline uint32_t millis() {
  static auto t0 = std::chrono::steady_clock::now();
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - t0).count();
}
inline uint32_t micros() {
  static auto t0 = std::chrono::steady_clock::now();
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now() - t0).count();
}
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

template <class T, class L, class H>
inline T constrain(T v, L lo, H hi) { return v < (T)lo ? (T)lo : v > (T)hi ? (T)hi : v; }

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  void toLowerCase() { for (auto& c : s_) c = (char)tolower(c); }
  void toUpperCase() { for (auto& c : s_) c = (char)toupper(c); }
  bool startsWith(const char* p) const { return s_.rfind(p, 0) == 0; }
  bool equalsIgnoreCase(const String& o) const {
    if (o.s_.size() != s_.size()) return false;
    for (size_t i = 0; i < s_.size(); i++)
      if (tolower(s_[i]) != tolower(o.s_[i])) return false;
    return true;
  }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator!=(const String& o) const { return s_ != o.s_; }

 private:
  std::string s_;
};

// Serial: stdout
struct SimSerial {
  template <class... A> void printf(const char* f, A... a) { ::printf(f, a...); fflush(stdout); }
  void println(const char* s) { ::printf("%s\n", s); }
};
static SimSerial Serial;
