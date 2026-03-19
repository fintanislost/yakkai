import QtQuick
import io.papercompany.scene 1.0

Item {
    id: root

    property url sceneSource: ""
    property string assetsPath: ""
    property int fillModeValue: 0
    property bool mouseInputEnabled: false
    property bool muted: true

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
            return PaperSceneViewer.AspectFit
        case 2:
            return PaperSceneViewer.Stretch
        case 0:
        default:
            return PaperSceneViewer.AspectCrop
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#11161d"
    }

    PaperSceneViewer {
        id: viewer
        anchors.fill: parent
        source: root.sceneSource
        assets: root.assetsUrl(root.assetsPath)
        fillMode: root.sceneFillMode()
        fps: 30
        muted: root.muted
        mouseInputEnabled: root.mouseInputEnabled
    }
}
