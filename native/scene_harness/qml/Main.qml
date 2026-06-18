import QtQuick
import QtQuick.Window

Window {
    id: root

    width: sceneHarnessWindowWidth
    height: sceneHarnessWindowHeight
    visible: true
    visibility: sceneHarnessFullscreen ? Window.FullScreen : Window.Windowed
    flags: (sceneHarnessFrameless || sceneHarnessFullscreen)
        ? (Qt.Window | Qt.FramelessWindowHint)
        : Qt.Window
    color: "#0b1016"
    title: "Paper Scene Harness"

    property url sceneSource: sceneHarnessSource
    property string assetsPath: sceneHarnessAssetsPath
    property int fillModeValue: sceneHarnessFillModeValue
    property bool mouseInputEnabled: sceneHarnessMouseInput
    property bool muted: sceneHarnessMuted
    property bool showInfoOverlay: sceneHarnessShowInfoOverlay
    property string backendName: sceneHarnessBackend
    property string backendQmlFile: sceneHarnessBackendQmlFile
    property string debugEffectCapturesPath: sceneHarnessDebugEffectCapturesPath
    property string debugEffectCaptureCommand: sceneHarnessDebugEffectCaptureCommand
    property int debugEffectCaptureDelayMs: sceneHarnessDebugEffectCaptureDelayMs
    property string debugEffectCaptureLayers: sceneHarnessDebugEffectCaptureLayers
    property string debugEffectProbeLayers: sceneHarnessDebugEffectProbeLayers
    property string debugEffectProbeHighRiskLayers: sceneHarnessDebugEffectProbeHighRiskLayers
    property string debugEffectProbeChannelMapSlots: sceneHarnessDebugEffectProbeChannelMapSlots
    property string debugEffectProbeMaxEffects: sceneHarnessDebugEffectProbeMaxEffects
    property string debugPuppetEffectFinalMesh: sceneHarnessDebugPuppetEffectFinalMesh
    property bool debugPuppetEffectRouteOnly: sceneHarnessDebugPuppetEffectRouteOnly
    property string debugPuppetAnimationLayerOverrides: sceneHarnessDebugPuppetAnimationLayerOverrides
    property string debugLayerVisibilityOverrides: sceneHarnessDebugLayerVisibilityOverrides
    property string debugMousePosition: sceneHarnessDebugMousePosition
    property string debugMouseTimeline: sceneHarnessDebugMouseTimeline || ""
    property bool debugInteractiveMouse: sceneHarnessDebugInteractiveMouse
    property bool debugSyntheticAudioEnabled: sceneHarnessDebugSyntheticAudioEnabled
    property int debugSyntheticAudioBins: sceneHarnessDebugSyntheticAudioBins
    property int debugSyntheticAudioIntervalMs: sceneHarnessDebugSyntheticAudioIntervalMs
    property string scenePropertiesJson: sceneHarnessScenePropertiesJson
    property bool captureReady: false
    property string backendStatus: backendLoader.item && backendLoader.item.backendStatus
        ? backendLoader.item.backendStatus
        : ""

    Component.onCompleted: {
        console.log("[Harness] window completed backend=" + backendName
            + " source=" + String(sceneSource)
            + " fill=" + fillModeName()
            + " mouse=" + mouseInputEnabled)
    }

    function fillModeName() {
        switch (fillModeValue) {
        case 1:
            return "fit"
        case 2:
            return "stretch"
        case 0:
        default:
            return "crop"
        }
    }

    function prepareForCaptureExit() {
        console.log("[Harness] preparing capture exit")
        if (backendLoader.item
                && typeof backendLoader.item.prepareForCaptureExit === "function") {
            backendLoader.item.prepareForCaptureExit()
        }
    }

    Loader {
        id: backendLoader
        anchors.fill: parent
        source: backendQmlFile

        onStatusChanged: {
            console.log("[Harness] loader status=" + status + " source=" + source)
            if (status === Loader.Error) {
                console.log("[Harness] loader errors=" + backendLoader.source)
            }
        }

        onLoaded: {
            console.log("[Harness] loader loaded backend item")
            item.debugEffectCapturesPath = Qt.binding(function() {
                return root.debugEffectCapturesPath
            })
            item.debugEffectCaptureCommand = Qt.binding(function() {
                return root.debugEffectCaptureCommand
            })
            item.debugEffectCaptureDelayMs = Qt.binding(function() {
                return root.debugEffectCaptureDelayMs
            })
            item.debugEffectCaptureLayers = Qt.binding(function() {
                return root.debugEffectCaptureLayers
            })
            item.debugEffectProbeLayers = Qt.binding(function() {
                return root.debugEffectProbeLayers
            })
            item.debugEffectProbeHighRiskLayers = Qt.binding(function() {
                return root.debugEffectProbeHighRiskLayers
            })
            item.debugEffectProbeChannelMapSlots = Qt.binding(function() {
                return root.debugEffectProbeChannelMapSlots
            })
            item.debugEffectProbeMaxEffects = Qt.binding(function() {
                return root.debugEffectProbeMaxEffects
            })
            item.debugPuppetEffectFinalMesh = Qt.binding(function() {
                return root.debugPuppetEffectFinalMesh
            })
            item.debugPuppetEffectRouteOnly = Qt.binding(function() {
                return root.debugPuppetEffectRouteOnly
            })
            item.debugPuppetAnimationLayerOverrides = Qt.binding(function() {
                return root.debugPuppetAnimationLayerOverrides
            })
            item.debugLayerVisibilityOverrides = Qt.binding(function() {
                return root.debugLayerVisibilityOverrides
            })
            item.debugMousePosition = Qt.binding(function() {
                return root.debugMousePosition
            })
            item.debugMouseTimeline = Qt.binding(function() {
                return root.debugMouseTimeline
            })
            item.debugInteractiveMouse = Qt.binding(function() {
                return root.debugInteractiveMouse
            })
            item.scenePropertiesJson = Qt.binding(function() {
                return root.scenePropertiesJson
            })
            item.sceneSource = Qt.binding(function() {
                return root.sceneSource
            })
            item.assetsPath = Qt.binding(function() {
                return root.assetsPath
            })
            item.fillModeValue = Qt.binding(function() {
                return root.fillModeValue
            })
            item.mouseInputEnabled = Qt.binding(function() {
                return root.mouseInputEnabled
            })
            item.muted = Qt.binding(function() {
                return root.muted
            })
            if ("debugSyntheticAudioEnabled" in item) {
                item.debugSyntheticAudioEnabled = Qt.binding(function() {
                    return root.debugSyntheticAudioEnabled
                })
            }
            if ("debugSyntheticAudioBins" in item) {
                item.debugSyntheticAudioBins = Qt.binding(function() {
                    return root.debugSyntheticAudioBins
                })
            }
            if ("debugSyntheticAudioIntervalMs" in item) {
                item.debugSyntheticAudioIntervalMs = Qt.binding(function() {
                    return root.debugSyntheticAudioIntervalMs
                })
            }
        }
    }

    Connections {
        target: backendLoader.item
        ignoreUnknownSignals: true

        function onBackendStatusChanged() {
            console.log("[Harness] backendStatus=" + String(backendLoader.item.backendStatus ?? ""))
        }

        function onFirstFrameReady() {
            if (!root.captureReady) {
                root.captureReady = true
                console.log("[Harness] backend first frame ready for capture")
            }
        }
    }

    Rectangle {
        visible: root.showInfoOverlay
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 16
        width: Math.min(parent.width * 0.44, 760)
        height: infoText.implicitHeight + 24
        radius: 12
        color: "#b20b1016"
        border.color: "#33576a7f"
        border.width: 1
    }

    Text {
        id: infoText
        visible: root.showInfoOverlay
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 28
        width: Math.min(parent.width * 0.4, 700)
        color: "white"
        wrapMode: Text.WrapAnywhere
        text: "Paper Scene Harness\n"
            + "Backend: " + backendName + "\n"
            + "Source: " + String(sceneSource) + "\n"
            + "Assets: " + assetsPath + "\n"
            + "Fill: " + fillModeName() + "\n"
            + "Mouse Input: " + (mouseInputEnabled ? "on" : "off") + "\n"
            + "Muted: " + (muted ? "yes" : "no") + "\n"
            + "Loader Status: " + backendLoader.status + "\n"
            + (backendLoader.status === Loader.Error
                ? "Backend QML failed to load: " + backendQmlFile + "\n"
                : "")
            + (backendLoader.item && backendLoader.item.backendStatus
                ? "Backend Status: " + backendLoader.item.backendStatus + "\n"
                : "")
            + "Capture Ready: " + (root.captureReady ? "yes" : "no") + "\n"
    }
}
