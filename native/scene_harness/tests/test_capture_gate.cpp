#include "CaptureGate.hpp"
#include "InteractiveMouseOption.hpp"
#include "LayerVisibilityOverrideOption.hpp"
#include "MousePositionOption.hpp"
#include "../src/MouseTimelineOption.hpp"
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

void testLayerVisibilityOverrideAcceptsSingleRule()
{
    const yakkai::harness::LayerVisibilityOverrideOptionResult result =
        yakkai::harness::validateLayerVisibilityOverrideOption(QStringLiteral("306:true"));

    check(result.valid, "single layer visibility override must be valid");
    check(result.normalized == QStringLiteral("306:true"),
          "single layer visibility override must preserve normalized rule");
    check(result.error.isEmpty(), "single layer visibility override must not report an error");
}

void testLayerVisibilityOverrideAcceptsMultipleRules()
{
    const yakkai::harness::LayerVisibilityOverrideOptionResult result =
        yakkai::harness::validateLayerVisibilityOverrideOption(QStringLiteral("306:true,240:false"));

    check(result.valid, "multiple layer visibility overrides must be valid");
    check(result.normalized == QStringLiteral("306:true,240:false"),
          "multiple layer visibility overrides must normalize as comma list");
    check(result.error.isEmpty(), "multiple layer visibility overrides must not report an error");
}

void testLayerVisibilityOverrideRejectsMissingValue()
{
    const yakkai::harness::LayerVisibilityOverrideOptionResult result =
        yakkai::harness::validateLayerVisibilityOverrideOption(QStringLiteral("306"));

    check(!result.valid, "layer visibility override without visible value must be invalid");
    check(result.normalized.isEmpty(), "invalid layer visibility override must not normalize");
    check(result.error.contains(QStringLiteral("--debug-layer-visibility-overrides")),
          "invalid layer visibility override error must name option");
}

void testLayerVisibilityOverrideRejectsNonNumericLayerId()
{
    const yakkai::harness::LayerVisibilityOverrideOptionResult result =
        yakkai::harness::validateLayerVisibilityOverrideOption(QStringLiteral("abc:true"));

    check(!result.valid, "layer visibility override with non-numeric id must be invalid");
    check(result.normalized.isEmpty(), "non-numeric layer visibility override must not normalize");
}

void testLayerVisibilityOverrideRejectsNonBooleanValue()
{
    const yakkai::harness::LayerVisibilityOverrideOptionResult result =
        yakkai::harness::validateLayerVisibilityOverrideOption(QStringLiteral("306:maybe"));

    check(!result.valid, "layer visibility override with non-boolean value must be invalid");
    check(result.normalized.isEmpty(), "non-boolean layer visibility override must not normalize");
}

void testEmptyMousePositionOptionIsValid()
{
    const yakkai::harness::MousePositionOptionResult result =
        yakkai::harness::validateMousePositionOption(QStringLiteral("  \n\t  "));

    check(result.valid, "empty mouse position option must be valid");
    check(!result.hasPosition, "empty mouse position option must not have an override");
    check(result.normalized.isEmpty(), "empty mouse position option must normalize to empty string");
    check(result.error.isEmpty(), "empty mouse position option must not report an error");
}

void testMousePositionOptionParsesNormalizedCoordinates()
{
    const yakkai::harness::MousePositionOptionResult result =
        yakkai::harness::validateMousePositionOption(QStringLiteral(" 1.0 , 0.5 "));

    check(result.valid, "mouse position option with normalized coordinates must be valid");
    check(result.hasPosition, "mouse position option must record an override");
    check(result.x == 1.0 && result.y == 0.5, "mouse position option must parse coordinates");
    check(result.normalized == QStringLiteral("1,0.5"), "mouse position option must canonicalize coordinates");
    check(result.error.isEmpty(), "valid mouse position option must not report an error");
}

void testMousePositionOptionRejectsMalformedInput()
{
    const yakkai::harness::MousePositionOptionResult result =
        yakkai::harness::validateMousePositionOption(QStringLiteral("0.5"));

    check(!result.valid, "mouse position option without comma must be invalid");
    check(!result.hasPosition, "invalid mouse position option must not have an override");
    check(result.normalized.isEmpty(), "invalid mouse position option must not normalize");
    check(result.error.contains(QStringLiteral("--debug-mouse-position")),
          "invalid mouse position option error must name option");
}

void testMousePositionOptionRejectsOutOfRangeValues()
{
    const yakkai::harness::MousePositionOptionResult high =
        yakkai::harness::validateMousePositionOption(QStringLiteral("1.25,0.5"));
    const yakkai::harness::MousePositionOptionResult low =
        yakkai::harness::validateMousePositionOption(QStringLiteral("0.5,-0.1"));

    check(!high.valid, "mouse position option must reject x above one");
    check(!low.valid, "mouse position option must reject y below zero");
}

void testMousePositionOptionRejectsNonFiniteValues()
{
    const yakkai::harness::MousePositionOptionResult result =
        yakkai::harness::validateMousePositionOption(QStringLiteral("nan,0.5"));

    check(!result.valid, "mouse position option must reject non-finite values");
    check(result.normalized.isEmpty(), "non-finite mouse position option must not normalize");
}

void testMouseTimelineOption()
{
    {
        const auto option = yakkai::harness::validateMouseTimelineOption(QString());
        check(option.valid, "empty mouse timeline is valid");
        check(!option.hasTimeline, "empty mouse timeline has no timeline");
    }
    {
        const auto option = yakkai::harness::validateMouseTimelineOption(
            QStringLiteral("0:0.5,0.5;1000:0,0.5;2500:1,0.5"));
        check(option.valid, "mouse timeline accepts increasing keyframes");
        check(option.hasTimeline, "mouse timeline has timeline");
        check(option.normalized == QStringLiteral("0:0.5,0.5;1000:0,0.5;2500:1,0.5"),
              "mouse timeline normalized");
    }
    {
        const auto option = yakkai::harness::validateMouseTimelineOption(QStringLiteral("1000:0,0.5;0:1,0.5"));
        check(!option.valid, "mouse timeline rejects non-increasing times");
    }
    {
        const auto option =
            yakkai::harness::validateMouseTimelineOption(QStringLiteral("0:1.2,0.5;1000:0.5,0.5"));
        check(!option.valid, "mouse timeline rejects x outside range");
        check(option.error.contains(QStringLiteral("positions")),
              "mouse timeline out-of-range error describes invalid position");
    }
}

void testInteractiveMouseOptionAllowsLiveMouseWithoutSyntheticDebugInput()
{
    const yakkai::harness::InteractiveMouseOptionResult result =
        yakkai::harness::validateInteractiveMouseOption(true, false, false);

    check(result.valid, "interactive mouse must be valid without synthetic mouse inputs");
    check(result.error.isEmpty(), "valid interactive mouse option must not report an error");
}

void testInteractiveMouseOptionRejectsFixedSyntheticMousePosition()
{
    const yakkai::harness::InteractiveMouseOptionResult result =
        yakkai::harness::validateInteractiveMouseOption(true, true, false);

    check(!result.valid, "interactive mouse must reject synthetic fixed mouse input");
    check(result.error.contains(QStringLiteral("--debug-mouse-position")),
          "interactive mouse conflict error must name fixed position option");
}

void testInteractiveMouseOptionRejectsSyntheticMouseTimeline()
{
    const yakkai::harness::InteractiveMouseOptionResult result =
        yakkai::harness::validateInteractiveMouseOption(true, false, true);

    check(!result.valid, "interactive mouse must reject synthetic mouse timeline");
    check(result.error.contains(QStringLiteral("--debug-mouse-timeline")),
          "interactive mouse conflict error must name timeline option");
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
    testLayerVisibilityOverrideAcceptsSingleRule();
    testLayerVisibilityOverrideAcceptsMultipleRules();
    testLayerVisibilityOverrideRejectsMissingValue();
    testLayerVisibilityOverrideRejectsNonNumericLayerId();
    testLayerVisibilityOverrideRejectsNonBooleanValue();
    testEmptyMousePositionOptionIsValid();
    testMousePositionOptionParsesNormalizedCoordinates();
    testMousePositionOptionRejectsMalformedInput();
    testMousePositionOptionRejectsOutOfRangeValues();
    testMousePositionOptionRejectsNonFiniteValues();
    testMouseTimelineOption();
    testInteractiveMouseOptionAllowsLiveMouseWithoutSyntheticDebugInput();
    testInteractiveMouseOptionRejectsFixedSyntheticMousePosition();
    testInteractiveMouseOptionRejectsSyntheticMouseTimeline();

    if (failures != 0) {
        std::cerr << failures << " capture gate test(s) failed\n";
        return 1;
    }

    return 0;
}
