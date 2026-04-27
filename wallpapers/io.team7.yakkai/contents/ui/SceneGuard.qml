/*
    SPDX-FileCopyrightText: 2026 Team7

    SPDX-License-Identifier: MIT
*/

import QtQuick

Item {
    id: root

    property string logPrefix: "[Yakkai]"
    property url sceneSource: ""
    property string sceneSourceKind: ""
    property string scenePropertiesJson: "{}"
    property string projectTitle: ""
    property string assetsPath: ""
    property string emptyMessage: qsTr("Select a Wallpaper Engine scene wallpaper in the wallpaper settings.")
    property bool experimentalEnabled: false
    property bool mouseInputEnabled: false
    property int fillModeValue: 0
    property bool muted: true
    property bool runtimeActive: false
    property string pendingRestartReason: ""
    property url runtimeSceneSource: ""
    property string runtimeAssetsPath: ""
    property string runtimeScenePropertiesJson: "{}"
    property int runtimeFillModeValue: 0
    property bool runtimeMouseInputEnabled: false

    readonly property bool openGlScenegraph: GraphicsInfo.api === GraphicsInfo.OpenGL
    readonly property string graphicsBackendName: graphicsApiName()
    readonly property bool readyForExperimentalAttempt: experimentalEnabled
        && String(sceneSource).length > 0
        && String(assetsPath).length > 0
        && openGlScenegraph
    readonly property string runtimeErrorText: runtimeLoader.status === Loader.Error
        ? qsTr("The experimental scene renderer failed to load. Yakkai stayed on the safe placeholder instead of invoking the renderer.")
        : ((!openGlScenegraph && experimentalEnabled && String(sceneSource).length > 0 && String(assetsPath).length > 0)
            ? qsTr("The experimental scene renderer needs Plasma's Qt Quick OpenGL backend. Plasma is currently using %1, so Yakkai stayed on the safe placeholder instead of invoking the native renderer.")
                .arg(graphicsBackendName)
            : "")

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

    function syncRuntimeInputs() {
        runtimeSceneSource = sceneSource
        runtimeAssetsPath = assetsPath
        runtimeScenePropertiesJson = scenePropertiesJson
        runtimeFillModeValue = fillModeValue
        runtimeMouseInputEnabled = mouseInputEnabled
    }

    function activateRuntime() {
        if (!readyForExperimentalAttempt) {
            runtimeActive = false
            return
        }

        syncRuntimeInputs()
        runtimeActive = true
    }

    function beginRuntimeRestart(reason) {
        pendingRestartReason = reason

        log("scene guard restarting runtime reason=" + pendingRestartReason
            + " source=" + String(sceneSource)
            + " assets=" + assetsPath
            + " fillModeValue=" + fillModeValue
            + " mouseInputEnabled=" + mouseInputEnabled)

        runtimeActive = false
        restartActivateTimer.restart()
    }

    function scheduleRuntimeRestartIfReady(reason) {
        if (runtimeActive && readyForExperimentalAttempt) {
            scheduleRuntimeRestart(reason)
        }
    }

    onSceneSourceChanged: {
        log("scene guard sceneSource=" + String(sceneSource))

        if (runtimeActive && readyForExperimentalAttempt) {
            beginRuntimeRestart("sceneSourceChanged")
        }
    }
    onAssetsPathChanged: {
        log("scene guard assetsPath=" + assetsPath)

        if (runtimeActive && readyForExperimentalAttempt) {
            beginRuntimeRestart("assetsPathChanged")
        }
    }
    onExperimentalEnabledChanged: log("scene guard experimentalEnabled=" + experimentalEnabled)
    onScenePropertiesJsonChanged: {
        log("scene guard scenePropertiesJson length=" + String(scenePropertiesJson).length)
        scheduleRuntimeRestartIfReady("scenePropertiesJsonChanged")
    }
    onMouseInputEnabledChanged: {
        log("scene guard mouseInputEnabled=" + mouseInputEnabled)
        scheduleRuntimeRestartIfReady("mouseInputEnabledChanged")
    }
    onFillModeValueChanged: {
        log("scene guard fillModeValue=" + fillModeValue)
        scheduleRuntimeRestartIfReady("fillModeValueChanged")
    }
    onMutedChanged: log("scene guard muted=" + muted)
    onReadyForExperimentalAttemptChanged: {
        log("scene guard readyForExperimentalAttempt=" + readyForExperimentalAttempt)
        activateRuntime()
    }
    onGraphicsBackendNameChanged: {
        log("scene guard graphicsApi=" + graphicsBackendName + " openGlScenegraph=" + openGlScenegraph)
        activateRuntime()
    }

    Component.onCompleted: {
        log("scene guard completed source=" + String(sceneSource)
            + " assets=" + assetsPath
            + " experimentalEnabled=" + experimentalEnabled
            + " mouseInputEnabled=" + mouseInputEnabled
            + " fillModeValue=" + fillModeValue
            + " graphicsApi=" + graphicsBackendName
            + " openGlScenegraph=" + openGlScenegraph)
        activateRuntime()
    }

    function scheduleRuntimeRestart(reason) {
        pendingRestartReason = reason
        restartTimer.restart()
    }

    Timer {
        id: restartTimer
        interval: 120
        repeat: false

        onTriggered: {
            if (!root.readyForExperimentalAttempt) {
                return
            }

            root.beginRuntimeRestart(root.pendingRestartReason)
        }
    }

    Timer {
        id: restartActivateTimer
        interval: 0
        repeat: false

        onTriggered: {
            root.activateRuntime()
        }
    }

    Component {
        id: runtimeComponent

        SceneRuntime {
            logPrefix: root.logPrefix
            sceneSource: root.runtimeSceneSource
            assetsPath: root.runtimeAssetsPath
            scenePropertiesJson: root.runtimeScenePropertiesJson
            fillModeValue: root.runtimeFillModeValue
            muted: root.muted
            mouseInputEnabled: root.runtimeMouseInputEnabled
        }
    }

    Loader {
        id: runtimeLoader
        anchors.fill: parent
        active: root.runtimeActive
        asynchronous: true
        sourceComponent: runtimeComponent
        visible: status === Loader.Ready

        onStatusChanged: {
            root.log("scene guard loader status=" + status + " active=" + active)

            if (status === Loader.Ready && item) {
                root.log("scene guard runtime ready")
            } else if (status === Loader.Error) {
                root.log("scene guard runtime failed, falling back to placeholder")
            }
        }
    }

    ScenePlaceholder {
        anchors.fill: parent
        visible: runtimeLoader.status !== Loader.Ready
        sceneSource: root.sceneSource
        sceneSourceKind: root.sceneSourceKind
        projectTitle: root.projectTitle
        assetsPath: root.assetsPath
        emptyMessage: root.emptyMessage
        experimentalEnabled: root.experimentalEnabled
        runtimeErrorText: root.runtimeErrorText
        graphicsBackendName: root.graphicsBackendName
    }
}
