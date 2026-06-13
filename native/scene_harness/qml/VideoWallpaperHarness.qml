import QtQuick
import "../../../wallpapers/io.team7.yakkai/contents/ui"

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
    property string debugLayerVisibilityOverrides: ""
    property string debugMousePosition: ""
    property string debugMouseTimeline: ""
    property bool debugInteractiveMouse: false
    property string scenePropertiesJson: ""
    property bool firstFrameEmitted: false
    readonly property string backendStatus: "video frames=" + video.receivedFrameCount + " source=" + String(sceneSource)

    signal firstFrameReady()

    function prepareForCaptureExit() {
        video.videoSource = ""
    }

    VideoBackground {
        id: video
        anchors.fill: parent
        videoSource: root.sceneSource
        fillModeValue: root.fillModeValue
        muted: root.muted
        verboseLogging: true

        onHasRenderedFrameChanged: {
            if (hasRenderedFrame && !root.firstFrameEmitted) {
                root.firstFrameEmitted = true
                root.firstFrameReady()
            }
        }
    }
}
