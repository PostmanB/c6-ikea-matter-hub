#include "daily_schedule.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

static tm local(int day, int hour, int minute, int second = 0) {
    tm t{}; t.tm_year = 126; t.tm_mon = 9; t.tm_mday = day;
    t.tm_hour = hour; t.tm_min = minute; t.tm_sec = second;
    return t;
}
int main() {
    DailySchedule s;
    assert(!s.due(local(3,6,0), true));
    s.enabled = 1;
    assert(!s.due(local(3,6,0), false));
    assert(!s.due(local(3,5,59,59), true));
    assert(s.due(local(3,6,0), true));
    assert(s.due(local(3,6,1,59), true));
    assert(!s.due(local(3,6,2), true));
    assert(!s.due(local(3,9,0), true));
    s.mark(local(3,6,0));
    auto restored = s;
    assert(!restored.due(local(3,6,0), true));
    assert(!restored.due(local(2,6,0), true));
    assert(restored.due(local(4,6,0), true));
    s.hour = 2; s.minute = 30;
    s.mark(local(25,2,30));
    assert(!s.due(local(25,2,30), true)); // Repeated DST hour.
    s.enabled = 0; assert(!s.due(local(26,2,30), true));
    s.enabled = 1; s.hour = 24; assert(!s.due(local(26,2,30), true));
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1); tzset();
    tm winter{}; winter.tm_year = 126; winter.tm_mon = 0; winter.tm_mday = 1; winter.tm_hour = 6; winter.tm_isdst = -1;
    auto winterEpoch = mktime(&winter);
    assert(gmtime(&winterEpoch)->tm_hour == 5);
    winter.tm_mon = 6; winter.tm_isdst = -1;
    auto summerEpoch = mktime(&winter);
    assert(gmtime(&summerEpoch)->tm_hour == 4);
    std::cout << "Schedule tests passed: sync, grace, duplicate/reboot, clock rewind, DST and disabled state\n";
}
