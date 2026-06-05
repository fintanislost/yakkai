#include "CaptureGate.hpp"
#include "PuppetSimulationOption.hpp"
#include "ScenePropertiesOption.hpp"

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

void testEmptyScenePropertiesJsonIsValid()
{
    const yakkai::harness::ScenePropertiesJsonOptionResult result =
        yakkai::harness::validateScenePropertiesJsonOption(QStringLiteral("  \n\t  "));

    check(result.valid, "empty scene properties json must be valid");
    check(result.normalized.isEmpty(), "empty scene properties json must normalize to empty string");
    check(result.error.isEmpty(), "empty scene properties json must not report an error");
}

void testObjectScenePropertiesJsonIsCompacted()
{
    const yakkai::harness::ScenePropertiesJsonOptionResult result =
        yakkai::harness::validateScenePropertiesJsonOption(QStringLiteral("{ \"timeofday\" : { \"value\" : \"1\" } }"));

    check(result.valid, "object scene properties json must be valid");
    check(result.normalized == QStringLiteral("{\"timeofday\":{\"value\":\"1\"}}"),
          "object scene properties json must normalize to compact json");
    check(result.error.isEmpty(), "object scene properties json must not report an error");
}

void testMalformedScenePropertiesJsonIsInvalid()
{
    const yakkai::harness::ScenePropertiesJsonOptionResult result =
        yakkai::harness::validateScenePropertiesJsonOption(QStringLiteral("{ \"timeofday\": "));

    check(!result.valid, "malformed scene properties json must be invalid");
    check(result.normalized.isEmpty(), "malformed scene properties json must not normalize");
    check(result.error.contains(QStringLiteral("invalid --scene-properties-json")),
          "malformed scene properties json error must name the invalid option");
}

void testNonObjectScenePropertiesJsonIsInvalid()
{
    const yakkai::harness::ScenePropertiesJsonOptionResult result =
        yakkai::harness::validateScenePropertiesJsonOption(QStringLiteral("[1, 2, 3]"));

    check(!result.valid, "non-object scene properties json must be invalid");
    check(result.normalized.isEmpty(), "non-object scene properties json must not normalize");
    check(result.error.contains(QStringLiteral("must be a JSON object")),
          "non-object scene properties json error must describe object requirement");
}

void testEmptyPuppetSimulationOptionIsValid()
{
    const yakkai::harness::PuppetSimulationOptionResult result =
        yakkai::harness::validatePuppetSimulationOption(QStringLiteral("  \n\t  "));

    check(result.valid, "empty puppet simulation option must be valid");
    check(result.normalized.isEmpty(), "empty puppet simulation option must normalize to empty string");
    check(result.error.isEmpty(), "empty puppet simulation option must not report an error");
}

void testRuntimePuppetSimulationOptionIsValid()
{
    const yakkai::harness::PuppetSimulationOptionResult result =
        yakkai::harness::validatePuppetSimulationOption(QStringLiteral("Runtime"));

    check(result.valid, "runtime puppet simulation option must be valid");
    check(result.normalized == QStringLiteral("runtime"),
          "runtime puppet simulation option must normalize lowercase");
    check(result.error.isEmpty(), "runtime puppet simulation option must not report an error");
}

void testInvalidPuppetSimulationOptionIsInvalid()
{
    const yakkai::harness::PuppetSimulationOptionResult result =
        yakkai::harness::validatePuppetSimulationOption(QStringLiteral("always"));

    check(!result.valid, "invalid puppet simulation option must be invalid");
    check(result.normalized.isEmpty(), "invalid puppet simulation option must not normalize");
    check(result.error.contains(QStringLiteral("off, diagnostic, or runtime")),
          "invalid puppet simulation option error must describe allowed values");
}

}

int main()
{
    testRootWindowAloneDoesNotArmCaptures();
    testFirstFrameAfterRootArmsCapturesOnce();
    testRootAfterFirstFrameArmsCaptures();
    testEmptyScenePropertiesJsonIsValid();
    testObjectScenePropertiesJsonIsCompacted();
    testMalformedScenePropertiesJsonIsInvalid();
    testNonObjectScenePropertiesJsonIsInvalid();
    testEmptyPuppetSimulationOptionIsValid();
    testRuntimePuppetSimulationOptionIsValid();
    testInvalidPuppetSimulationOptionIsInvalid();

    if (failures != 0) {
        std::cerr << failures << " capture gate test(s) failed\n";
        return 1;
    }

    return 0;
}
