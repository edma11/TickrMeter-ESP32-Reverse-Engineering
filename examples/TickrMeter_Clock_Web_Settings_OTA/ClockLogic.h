#pragma once
#include <stdint.h>

namespace ClockLogic {
constexpr uint32_t windowMs = 300000UL;
constexpr bool windowExpired(uint32_t now, uint32_t started) {
  return uint32_t(now - started) >= windowMs;
}
constexpr uint64_t nextMinuteUs(int64_t seconds, uint32_t microseconds) {
  return 60000000ULL - (uint64_t(seconds % 60) * 1000000ULL + microseconds);
}
constexpr int dateStamp(int year, int dayOfYear) { return year * 366 + dayOfYear; }
constexpr int chargePercent(uint32_t mv, uint32_t empty, uint32_t full) {
  return full <= empty ? -1 : mv <= empty ? 0 : mv >= full ? 100
    : int(((mv - empty) * 100UL + (full - empty) / 2) / (full - empty));
}
constexpr int batteryBars(int percent) {
  return percent <= 0 ? 0 : percent >= 100 ? 4 : (percent + 24) / 25;
}

// Boundary checks are compiled with the firmware; no hardware needed.
static_assert(!windowExpired(299999, 0), "Window must remain open before 5 minutes");
static_assert(windowExpired(300000, 0), "Window must close at 5 minutes");
static_assert(windowExpired(299899, UINT32_MAX - 100), "millis rollover");
static_assert(nextMinuteUs(0, 0) == 60000000, "Full minute at boundary");
static_assert(nextMinuteUs(59, 999999) == 1, "Fractional boundary");
static_assert(nextMinuteUs(87, 500000) == 32500000, "Subtract awake time");
static_assert(dateStamp(2027, 0) != dateStamp(2026, 0), "Alarm must repeat next year");
static_assert(chargePercent(2900,3000,4200)==0 && chargePercent(4300,3000,4200)==100, "Clamp charge bounds");
static_assert(chargePercent(3600,3000,4200)==50, "Charge midpoint");
static_assert(chargePercent(3600,4200,3000)==-1, "Invalid charge range");
static_assert(batteryBars(0)==0 && batteryBars(25)==1 && batteryBars(26)==2
  && batteryBars(50)==2 && batteryBars(51)==3 && batteryBars(75)==3
  && batteryBars(76)==4 && batteryBars(100)==4, "Four battery bar boundaries");
}
