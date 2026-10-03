#pragma once
#include <cstdint>
#include <ctime>

// Wall time is supplied by the adapter; no ESP dependencies in the evaluator.
struct DailySchedule {
    uint32_t version = 1;
    uint8_t enabled = 0, hour = 6, minute = 0, reserved = 0;
    int32_t lastDay = 0;
    bool due(const tm &local, bool synchronized) const {
        if (!synchronized || !enabled || hour > 23 || minute > 59) return false;
        const int day = (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
        const int elapsed = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec - (hour * 3600 + minute * 60);
        // Two-minute grace, including a boot/sync at 06:01. Never replay hours
        // later, or repeat a day after an NTP correction or a DST clock rewind.
        return day > lastDay && elapsed >= 0 && elapsed < 120;
    }
    void mark(const tm &local) {
        lastDay = (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
    }
};
