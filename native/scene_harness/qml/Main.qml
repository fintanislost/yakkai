import QtQuick
import QtQuick.Window

Window {
    id: root

    width: sceneHarnessWindowWidth
    height: sceneHarnessWindowHeight
    visible: true
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
    property string debugEffectProbeLayers: sceneHarnessDebugEffectProbeLayers

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
            item.debugEffectProbeLayers = Qt.binding(function() {
                return root.debugEffectProbeLayers
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
        }
    }

    Connections {
        target: backendLoader.item
        ignoreUnknownSignals: true

        function onBackendStatusChanged() {
            console.log("[Harness] backendStatus=" + String(backendLoader.item.backendStatus ?? ""))
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
    }
}
