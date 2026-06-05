import QtQuick
import com.github.catsout.wallpaperEngineKde 1.2

Item {
    id: root

    property url sceneSource: ""
    property string assetsPath: ""
    property int fillModeValue: 0
    property bool mouseInputEnabled: false
    property bool muted: true
    property string backendStatus: "System SceneViewer backend"
    property string debugEffectCapturesPath: ""
    property string debugEffectCaptureCommand: ""
    property int debugEffectCaptureDelayMs: 0
    property string debugEffectProbeLayers: ""
    property string debugEffectProbeHighRiskLayers: ""
    property string debugEffectProbeChannelMapSlots: ""
    property string debugEffectProbeMaxEffects: ""
    property string debugPuppetEffectFinalMesh: ""
    property bool debugPuppetEffectRouteOnly: false
    property string debugPuppetAnimationLayerOverrides: ""
    property string scenePropertiesJson: ""

    signal firstFrameReady()

    function assetsUrl(path) {
        const asText = String(path ?? "")

        if (asText.length === 0) {
            return ""
        }

        if (asText.startsWith("file://")) {
            return asText
        }

        if (asText.startsWith("/")) {
            return Qt.resolvedUrl(asText)
        }

        return asText
    }

    function sceneFillMode() {
        switch (fillModeValue) {
        case 1:
            return SceneViewer.ASPECTFIT
        case 2:
            return SceneViewer.STRETCH
        case 0:
        default:
            return SceneViewer.ASPECTCROP
        }
    }

    function applyMouseInput() {
        viewer.setAcceptMouse(mouseInputEnabled)
        viewer.setAcceptHover(mouseInputEnabled)
    }

    function prepareForCaptureExit() {
        if (typeof viewer.pause === "function") {
            viewer.pause()
        }
    }

    onMouseInputEnabledChanged: applyMouseInput()

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    SceneViewer {
        id: viewer
        anchors.fill: parent
        source: root.sceneSource
        assets: root.assetsUrl(root.assetsPath)
        fillMode: root.sceneFillMode()
        fps: 30
        speed: 1.0
        muted: root.muted
        volume: root.muted ? 0.0 : 1.0

        Component.onCompleted: {
            root.applyMouseInput()
        }

        Connections {
            target: viewer
            ignoreUnknownSignals: true

            function onFirstFrame() {
                root.firstFrameReady()
            }
        }
    }
}
