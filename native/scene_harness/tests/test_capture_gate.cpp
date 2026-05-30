#include "CaptureGate.hpp"

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testRootWindowAloneDoesNotArmCaptures()
{
    yakkai::harness::CaptureStartGate gate;

    gate.markRootWindowReady();

    check(!gate.consumeReadyToStart(), "root window alone must not arm capture timers");
}

void testFirstFrameAfterRootArmsCapturesOnce()
{
    yakkai::harness::CaptureStartGate gate;

    gate.markRootWindowReady();
    gate.markFirstFrameReady();

    check(gate.consumeReadyToStart(), "first frame after root must arm capture timers");
    check(!gate.consumeReadyToStart(), "capture timers must only arm once");
}

void testRootAfterFirstFrameArmsCaptures()
{
    yakkai::harness::CaptureStartGate gate;

    gate.markFirstFrameReady();
    gate.markRootWindowReady();

    check(gate.consumeReadyToStart(), "root arriving after first frame still arms captures");
}

}

int main()
{
    testRootWindowAloneDoesNotArmCaptures();
    testFirstFrameAfterRootArmsCapturesOnce();
    testRootAfterFirstFrameArmsCaptures();

    if (failures != 0) {
        std::cerr << failures << " capture gate test(s) failed\n";
        return 1;
    }

    return 0;
}
