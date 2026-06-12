import QtQuick
import io.team7.scene 1.0

Item {
    id: root

    property url sceneSource: ""
    property string assetsPath: ""
    property int fillModeValue: 0
    property bool mouseInputEnabled: false
    property bool muted: true
    property string debugEffectCapturesPath: ""
    property string debugEffectCaptureCommand: ""
    property int debugEffectCaptureDelayMs: 0
    property string debugEffectCaptureLayers: ""
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
            return YakkaiSceneViewer.AspectFit
        case 2:
            return YakkaiSceneViewer.Stretch
        case 0:
        default:
            return YakkaiSceneViewer.AspectCrop
        }
    }

    function prepareForCaptureExit() {
        if (typeof viewer.pause === "function") {
            viewer.pause()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#11161d"
    }

    YakkaiSceneViewer {
        id: viewer
        anchors.fill: parent
        debugEffectCapturesPath: root.debugEffectCapturesPath
        debugEffectCaptureCommand: root.debugEffectCaptureCommand
        debugEffectCaptureDelayMs: root.debugEffectCaptureDelayMs
        debugEffectCaptureLayers: root.debugEffectCaptureLayers
        debugEffectProbeLayers: root.debugEffectProbeLayers
        debugEffectProbeHighRiskLayers: root.debugEffectProbeHighRiskLayers
        debugEffectProbeChannelMapSlots: root.debugEffectProbeChannelMapSlots
        debugEffectProbeMaxEffects: root.debugEffectProbeMaxEffects
        debugPuppetEffectFinalMesh: root.debugPuppetEffectFinalMesh
        debugPuppetEffectRouteOnly: root.debugPuppetEffectRouteOnly
        debugPuppetAnimationLayerOverrides: root.debugPuppetAnimationLayerOverrides
        scenePropertiesJson: root.scenePropertiesJson
        source: root.sceneSource
        assets: root.assetsUrl(root.assetsPath)
        fillMode: root.sceneFillMode()
        fps: 30
        muted: root.muted
        mouseInputEnabled: root.mouseInputEnabled

        Connections {
            target: viewer

            function onFirstFrame() {
                root.firstFrameReady()
            }
        }
    }
}
