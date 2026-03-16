/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick

Item {
    id: root

    property string logPrefix: "[Paper Gradient]"
    property url sceneSource: ""
    property string sceneSourceKind: ""
    property string projectTitle: ""
    property string assetsPath: ""
    property string emptyMessage: qsTr("Select a Wallpaper Engine scene wallpaper in the wallpaper settings.")
    property bool experimentalEnabled: false
    property bool mouseInputEnabled: false
    property int fillModeValue: 0
    property bool muted: true
    property bool runtimeActive: false
    property string pendingRestartReason: ""

    readonly property bool readyForExperimentalAttempt: experimentalEnabled
        && String(sceneSource).length > 0
        && String(assetsPath).length > 0
    readonly property string runtimeErrorText: runtimeLoader.status === Loader.Error
        ? qsTr("The experimental scene renderer failed to load. Paper Gradient stayed on the safe placeholder instead of invoking the renderer.")
        : ""

    function log(message) {
        console.log(logPrefix + " " + message)
    }

    onSceneSourceChanged: log("scene guard sceneSource=" + String(sceneSource))
    onAssetsPathChanged: log("scene guard assetsPath=" + assetsPath)
    onExperimentalEnabledChanged: log("scene guard experimentalEnabled=" + experimentalEnabled)
    onMouseInputEnabledChanged: {
        log("scene guard mouseInputEnabled=" + mouseInputEnabled)

        if (runtimeLoader.status === Loader.Ready && readyForExperimentalAttempt) {
            scheduleRuntimeRestart("mouseInputEnabledChanged")
        }
    }
    onFillModeValueChanged: {
        log("scene guard fillModeValue=" + fillModeValue)

        if (runtimeLoader.status === Loader.Ready && readyForExperimentalAttempt) {
            scheduleRuntimeRestart("fillModeValueChanged")
        }
    }
    onMutedChanged: log("scene guard muted=" + muted)
    onReadyForExperimentalAttemptChanged: {
        log("scene guard readyForExperimentalAttempt=" + readyForExperimentalAttempt)
        runtimeActive = readyForExperimentalAttempt
    }

    Component.onCompleted: {
        log("scene guard completed source=" + String(sceneSource)
            + " assets=" + assetsPath
            + " experimentalEnabled=" + experimentalEnabled
            + " mouseInputEnabled=" + mouseInputEnabled
            + " fillModeValue=" + fillModeValue)
        runtimeActive = readyForExperimentalAttempt
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

            root.log("scene guard restarting runtime reason=" + root.pendingRestartReason
                + " fillModeValue=" + root.fillModeValue
                + " mouseInputEnabled=" + root.mouseInputEnabled)
            root.runtimeActive = false
            restartActivateTimer.start()
        }
    }

    Timer {
        id: restartActivateTimer
        interval: 0
        repeat: false

        onTriggered: {
            root.runtimeActive = root.readyForExperimentalAttempt
        }
    }

    Component {
        id: runtimeComponent

        SceneRuntime {
            logPrefix: root.logPrefix
            sceneSource: root.sceneSource
            assetsPath: root.assetsPath
            fillModeValue: root.fillModeValue
            muted: root.muted
            mouseInputEnabled: root.mouseInputEnabled
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
    }
}
