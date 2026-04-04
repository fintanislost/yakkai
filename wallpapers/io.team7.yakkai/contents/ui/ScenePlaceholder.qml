/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import org.kde.plasma.plasma5support as Plasma5Support

Item {
    id: root

    property url sceneSource: ""
    property string sceneSourceKind: ""
    property string projectTitle: ""
    property string assetsPath: ""
    property string emptyMessage: qsTr("Select a Wallpaper Engine scene wallpaper in the wallpaper settings.")
    property bool experimentalEnabled: false
    property bool mouseInputEnabled: false
    property int fillModeValue: 0
    property bool muted: true
    property string runtimeErrorText: ""

    property string sourceExistsStatus: qsTr("Not checked")
    property string assetsExistsStatus: qsTr("Not checked")
    property string backendModuleStatus: qsTr("Not checked")
    property bool backendModuleStaged: false
    property var pendingDiagnostics: ({})

    readonly property bool hasSelection: String(sceneSource).length > 0
    readonly property string resolvedSourceKind: String(sceneSourceKind ?? "").length > 0
        ? sceneSourceKind
        : (localPath(sceneSource).split("/").filter(function(part) {
            return part.length > 0
        }).pop() ?? "")
    readonly property string statusText: hasSelection
        ? (runtimeErrorText.length > 0
            ? runtimeErrorText
            : (experimentalEnabled
                ? (backendModuleStatus === qsTr("Checking…")
                    ? qsTr("Wallpaper Engine Scene Native is checking whether the repo-owned Yakkai scene module is staged into this wallpaper package.")
                    : (backendModuleStaged
                    ? qsTr("Wallpaper Engine Scene Native is in guarded experimental mode. If the repo-owned native renderer does not come up cleanly, Yakkai falls back to this safe placeholder instead of continuing the attempt.")
                    : qsTr("Wallpaper Engine Scene Native needs the repo-owned Yakkai scene module to be staged into this wallpaper package before the guarded native renderer can start. Build the native workspace, then update the wallpaper package again.")))
                : qsTr("Wallpaper Engine Scene is currently a research-only mode. Scene projects can be scanned and selected safely, but rendering is intentionally disabled to avoid destabilizing Plasma.")))
        : emptyMessage
    readonly property string nativeModuleDirPath: localPath(Qt.resolvedUrl("../imports/io/team7/scene"))

    function localPath(value) {
        const asText = String(value ?? "")

        if (asText.startsWith("file://")) {
            return decodeURIComponent(asText.substring(7))
        }

        return asText
    }

    function shellQuote(value) {
        return "'" + String(value).replace(/'/g, "'\"'\"'") + "'"
    }

    function queueDiagnostic(kind, shellScript) {
        const command = "sh -c " + shellQuote(shellScript)
        const mapping = Object.assign({}, pendingDiagnostics)
        mapping[command] = kind
        pendingDiagnostics = mapping
        diagnosticsProbe.connectSource(command)
    }

    function refreshDiagnostics() {
        const sourcePath = localPath(sceneSource)

        sourceExistsStatus = hasSelection ? qsTr("Checking…") : qsTr("No scene selected")
        assetsExistsStatus = assetsPath.length > 0 ? qsTr("Checking…") : qsTr("No Steam assets path available")
        backendModuleStatus = qsTr("Checking…")
        backendModuleStaged = false

        if (hasSelection) {
            queueDiagnostic("source", "test -f " + shellQuote(sourcePath) + " && printf yes || printf no")
        }

        if (assetsPath.length > 0) {
            queueDiagnostic("assets", "test -d " + shellQuote(assetsPath) + " && printf yes || printf no")
        }

        queueDiagnostic(
            "backend",
            "if test -f " + shellQuote(nativeModuleDirPath + "/qmldir")
                + " && test -f " + shellQuote(nativeModuleDirPath + "/libyakkai_scene_backendplugin.so")
                + " && test -f " + shellQuote(nativeModuleDirPath + "/libyakkai_scene_backend.so")
                + "; then printf yes; else printf no; fi"
        )
    }

    onSceneSourceChanged: refreshDiagnostics()
    onAssetsPathChanged: refreshDiagnostics()

    Component.onCompleted: refreshDiagnostics()

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#121821" }
            GradientStop { position: 1.0; color: "#1d2633" }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.82, 820)
        height: contentColumn.implicitHeight + 36
        radius: 12
        color: "#aa0b0f14"
        border.color: "#335f6f84"
        border.width: 1
    }

    Column {
        id: contentColumn
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.72, 740)
        spacing: 10

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "white"
            font.pixelSize: 22
            text: qsTr("Wallpaper Engine Scene")
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#f0f0f0"
            text: root.statusText
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            color: "#c1ccd8"
            text: hasSelection
                ? qsTr("Project: %1\nResolved scene source: %2\nSource kind: %3")
                    .arg(projectTitle.length > 0 ? projectTitle : qsTr("Unknown"))
                    .arg(String(sceneSource))
                    .arg(resolvedSourceKind.length > 0 ? resolvedSourceKind : qsTr("Unknown"))
                : qsTr("No scene project is currently selected.")
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#dde6ef"
            text: qsTr("Scene source exists: %1\nWallpaper Engine assets directory exists: %2\nYakkai native scene module staged: %3")
                .arg(sourceExistsStatus)
                .arg(assetsExistsStatus)
                .arg(backendModuleStatus)
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#d6deea"
            text: qsTr("Experimental native renderer enabled: %1")
                .arg(experimentalEnabled ? qsTr("Yes") : qsTr("No"))
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            color: "#9fb0c2"
            text: qsTr("Expected assets path: %1\nExpected native module path: %2")
                .arg(assetsPath.length > 0 ? assetsPath : qsTr("Unknown"))
                .arg(nativeModuleDirPath.length > 0 ? nativeModuleDirPath : qsTr("Unknown"))
        }
    }

    Plasma5Support.DataSource {
        id: diagnosticsProbe
        engine: "executable"
        connectedSources: []

        onNewData: function(source, data) {
            diagnosticsProbe.disconnectSource(source)

            const kind = root.pendingDiagnostics[source] ?? ""
            const mapping = Object.assign({}, root.pendingDiagnostics)
            delete mapping[source]
            root.pendingDiagnostics = mapping

            const status = (data.stdout ?? "").trim() === "yes" ? qsTr("Yes") : qsTr("No")

            if (kind === "source") {
                root.sourceExistsStatus = status
            } else if (kind === "assets") {
                root.assetsExistsStatus = status
            } else if (kind === "backend") {
                root.backendModuleStatus = status
                root.backendModuleStaged = (data.stdout ?? "").trim() === "yes"
            }
        }
    }
}
