/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import QtQuick.Window
import com.github.catsout.wallpaperEngineKde 1.2

Item {
    id: root

    property string logPrefix: "[Paper Gradient]"
    property url sceneSource: ""
    property string assetsPath: ""
    property int fillModeValue: 0
    property bool muted: true
    property bool mouseInputEnabled: false
    property bool firstFrameSeen: false

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

    function log(message) {
        console.log(logPrefix + " " + message)
    }

    function graphicsApiName() {
        switch (GraphicsInfo.api) {
        case GraphicsInfo.OpenGL:
            return "OpenGL"
        case GraphicsInfo.Software:
            return "Software"
        case GraphicsInfo.Direct3D11:
            return "Direct3D11"
        case GraphicsInfo.Vulkan:
            return "Vulkan"
        case GraphicsInfo.Metal:
            return "Metal"
        case GraphicsInfo.Null:
            return "Null"
        case GraphicsInfo.OpenVG:
            return "OpenVG"
        default:
            return "Unknown(" + GraphicsInfo.api + ")"
        }
    }

    function logGeometry(context) {
        const windowWidth = root.window ? root.window.width : -1
        const windowHeight = root.window ? root.window.height : -1
        const windowDpr = root.window ? root.window.devicePixelRatio : 0

        log("scene runtime geometry context=" + context
            + " item=" + Math.round(root.width) + "x" + Math.round(root.height)
            + " window=" + Math.round(windowWidth) + "x" + Math.round(windowHeight)
            + " dpr=" + windowDpr
            + " graphicsApi=" + graphicsApiName()
            + " graphicsVersion=" + GraphicsInfo.majorVersion + "." + GraphicsInfo.minorVersion
            + " fillModeValue=" + fillModeValue
            + " mouseInputEnabled=" + mouseInputEnabled)
    }

    function sceneFillModeEnum() {
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

    function sceneFillModeName() {
        switch (fillModeValue) {
        case 1:
            return "ASPECTFIT"
        case 2:
            return "STRETCH"
        case 0:
        default:
            return "ASPECTCROP"
        }
    }

    function applyFillMode() {
        const nextFillMode = sceneFillModeEnum()
        player.fillMode = nextFillMode
        log("scene runtime applyFillMode=" + sceneFillModeName() + " value=" + fillModeValue)
    }

    function applyMouseInput() {
        player.setAcceptMouse(mouseInputEnabled)
        player.setAcceptHover(mouseInputEnabled)
        log("scene runtime mouseInputEnabled=" + mouseInputEnabled)
    }

    onSceneSourceChanged: {
        firstFrameSeen = false
        firstFrameWatchdog.restart()
        log("scene runtime sceneSource=" + String(sceneSource))
    }
    onAssetsPathChanged: log("scene runtime assetsPath=" + assetsPath)
    onMutedChanged: log("scene runtime muted=" + muted)
    onFillModeValueChanged: applyFillMode()
    onMouseInputEnabledChanged: applyMouseInput()
    onWidthChanged: logGeometry("widthChanged")
    onHeightChanged: logGeometry("heightChanged")
    onFirstFrameSeenChanged: {
        if (firstFrameSeen) {
            firstFrameWatchdog.stop()
        }
        log("scene runtime firstFrameSeen=" + firstFrameSeen)
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Timer {
        id: firstFrameWatchdog
        interval: 5000
        repeat: false

        onTriggered: {
            if (!root.firstFrameSeen) {
                root.log("scene runtime still waiting for first frame source=" + String(root.sceneSource)
                    + " fillModeValue=" + root.fillModeValue
                    + " mouseInputEnabled=" + root.mouseInputEnabled)
            }
        }
    }

    SceneViewer {
        id: player
        anchors.fill: parent
        source: root.sceneSource
        assets: root.assetsUrl(root.assetsPath)
        fillMode: SceneViewer.ASPECTCROP
        fps: 30
        speed: 1.0
        muted: root.muted
        volume: root.muted ? 0.0 : 1.0

        Component.onCompleted: {
            root.log("scene runtime component completed")
            root.applyFillMode()
            root.applyMouseInput()
            root.logGeometry("playerCompleted")
            if (String(root.sceneSource).length > 0) {
                root.log("scene runtime initial source=" + String(root.sceneSource))
            }
        }

        Connections {
            target: player

            function onFirstFrame() {
                root.firstFrameSeen = true
                root.logGeometry("firstFrame")
                root.log("scene runtime firstFrame source=" + String(root.sceneSource))
            }

            function onSourceChanged() {
                root.log("scene viewer sourceChanged=" + String(player.source))
            }

            function onFillModeChanged() {
                root.log("scene viewer fillModeChanged=" + player.fillMode)
            }
        }
    }

    Component.onCompleted: {
        log("scene runtime wrapper completed source=" + String(sceneSource)
            + " assets=" + assetsPath
            + " muted=" + muted
            + " fillModeValue=" + fillModeValue
            + " mouseInputEnabled=" + mouseInputEnabled)
        logGeometry("wrapperCompleted")
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.48, 420)
        height: loadingText.implicitHeight + 24
        radius: 10
        color: "#66000000"
        visible: !root.firstFrameSeen
    }

    Text {
        id: loadingText
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.4, 380)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "white"
        text: qsTr("Starting experimental Wallpaper Engine scene renderer…")
        visible: !root.firstFrameSeen
    }
}
