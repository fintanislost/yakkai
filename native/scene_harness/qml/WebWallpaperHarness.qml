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
    property bool debugSyntheticAudioEnabled: false
    property int debugSyntheticAudioBins: 128
    property int debugSyntheticAudioIntervalMs: 33
    property bool syntheticAudioArmed: false
    property real syntheticAudioOriginMs: 0
    property string scenePropertiesJson: "{}"
    property bool firstFrameEmitted: false
    readonly property string backendStatus: "web loaded=" + web.pageLoaded
        + " propertiesSent=" + web.propertiesSent
        + " syntheticAudio=" + root.debugSyntheticAudioEnabled
        + " diagnostics=" + web.runtimeDiagnostics
        + " source=" + String(sceneSource)

    signal firstFrameReady()

    function prepareForCaptureExit() {
        web.prepareForCaptureExit()
    }

    Timer {
        id: settleTimer
        interval: 750
        repeat: false

        onTriggered: {
            if (!root.firstFrameEmitted) {
                root.syntheticAudioOriginMs = Date.now()
                root.syntheticAudioArmed = true
                root.firstFrameEmitted = true
                root.firstFrameReady()
            }
        }
    }

    WebBackground {
        id: web
        anchors.fill: parent
        webSource: root.sceneSource
        userPropertiesJson: root.scenePropertiesJson.length > 0 ? root.scenePropertiesJson : "{}"
        muted: root.muted
        debugSyntheticAudioEnabled: root.debugSyntheticAudioEnabled && root.syntheticAudioArmed
        debugSyntheticAudioBins: root.debugSyntheticAudioBins
        debugSyntheticAudioIntervalMs: root.debugSyntheticAudioIntervalMs
        debugSyntheticAudioOriginMs: root.syntheticAudioOriginMs

        onPageLoadedChanged: {
            if (pageLoaded) {
                settleTimer.restart()
            } else {
                settleTimer.stop()
                root.firstFrameEmitted = false
                root.syntheticAudioArmed = false
                root.syntheticAudioOriginMs = 0
            }
        }
    }
}
