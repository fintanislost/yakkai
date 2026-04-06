/*
    SPDX-FileCopyrightText: 2026 Team7
    SPDX-License-Identifier: MIT

    Playlist configuration panel — manages playlists of any wallpaper type.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: playlistPanel
    Layout.fillWidth: true
    Layout.maximumWidth: 460
    spacing: Kirigami.Units.smallSpacing

    // External bindings
    property string playlistsJson: ""
    property int activePlaylistIndex: -1

    // Signals to parent config
    signal playlistsJsonChanged(string json)
    signal activePlaylistIndexChanged(int index)

    // Parsed data
    readonly property var playlistData: {
        if (!playlistsJson || playlistsJson.length === 0) return { playlists: [] }
        try { return JSON.parse(playlistsJson) } catch(e) { return { playlists: [] } }
    }
    readonly property var activePlaylist: {
        const idx = activePlaylistIndex
        const pls = playlistData.playlists || []
        return (idx >= 0 && idx < pls.length) ? pls[idx] : null
    }

    function save(data) {
        playlistsJsonChanged(JSON.stringify(data))
    }

    function createPlaylist(name) {
        const data = JSON.parse(JSON.stringify(playlistData))
        data.playlists.push({ name: name, mode: 0, interval: 30, items: [] })
        save(data)
        activePlaylistIndexChanged(data.playlists.length - 1)
    }

    function deletePlaylist(index) {
        const data = JSON.parse(JSON.stringify(playlistData))
        data.playlists.splice(index, 1)
        save(data)
        if (activePlaylistIndex >= data.playlists.length)
            activePlaylistIndexChanged(data.playlists.length - 1)
    }

    function renamePlaylist(index, newName) {
        const data = JSON.parse(JSON.stringify(playlistData))
        if (index >= 0 && index < data.playlists.length) {
            data.playlists[index].name = newName
            save(data)
        }
    }

    function setMode(mode) {
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = activePlaylistIndex
        if (idx >= 0 && idx < data.playlists.length) {
            data.playlists[idx].mode = mode
            save(data)
        }
    }

    function setInterval(mins) {
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = activePlaylistIndex
        if (idx >= 0 && idx < data.playlists.length) {
            data.playlists[idx].interval = mins
            save(data)
        }
    }

    function addItem(pickerItem) {
        if (!pickerItem) return
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = activePlaylistIndex
        if (idx < 0 || idx >= data.playlists.length) return
        data.playlists[idx].items.push({
            title: pickerItem.title || "",
            weType: pickerItem.weType || "scene",
            sceneSource: pickerItem.sourcePath || "",
            sceneProjectPath: pickerItem.projectPath || "",
            sceneProjectTitle: pickerItem.title || "",
            sceneSourceKind: pickerItem.sourceKind || "",
            videoSource: pickerItem.sourcePath || "",
            webSource: pickerItem.sourcePath || "",
            propertiesJson: pickerItem.propertiesJson || "",
            previewPath: pickerItem.previewPath || "",
            workshopId: pickerItem.workshopId || "",
            scheduleTime: "00:00"
        })
        save(data)
    }

    function removeItem(itemIndex) {
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = activePlaylistIndex
        if (idx < 0 || idx >= data.playlists.length) return
        data.playlists[idx].items.splice(itemIndex, 1)
        save(data)
    }

    function moveItemUp(itemIndex) {
        if (itemIndex <= 0) return
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = activePlaylistIndex
        if (idx < 0 || idx >= data.playlists.length) return
        const items = data.playlists[idx].items
        const tmp = items[itemIndex - 1]
        items[itemIndex - 1] = items[itemIndex]
        items[itemIndex] = tmp
        save(data)
    }

    function setItemTime(itemIndex, time) {
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = activePlaylistIndex
        if (idx < 0 || idx >= data.playlists.length) return
        data.playlists[idx].items[itemIndex].scheduleTime = time
        save(data)
    }

    // ---- UI ----

    RowLayout {
        Layout.fillWidth: true

        QQC2.ComboBox {
            Layout.fillWidth: true
            model: (playlistPanel.playlistData.playlists || []).map(p => p.name)
            currentIndex: playlistPanel.activePlaylistIndex
            onActivated: playlistPanel.activePlaylistIndexChanged(currentIndex)
        }

        QQC2.Button {
            text: qsTr("New")
            onClicked: {
                nameInput.text = qsTr("Playlist %1").arg((playlistPanel.playlistData.playlists || []).length + 1)
                nameDialog.mode = "create"
                nameDialog.open()
            }
        }

        QQC2.Button {
            text: qsTr("Rename")
            enabled: playlistPanel.activePlaylist !== null
            onClicked: {
                nameInput.text = playlistPanel.activePlaylist.name
                nameDialog.mode = "rename"
                nameDialog.open()
            }
        }

        QQC2.Button {
            text: qsTr("Delete")
            enabled: playlistPanel.activePlaylist !== null
            onClicked: deleteDialog.open()
        }
    }

    QQC2.ComboBox {
        Layout.fillWidth: true
        visible: playlistPanel.activePlaylist !== null
        model: [qsTr("Sequential"), qsTr("Random"), qsTr("Scheduled")]
        currentIndex: playlistPanel.activePlaylist ? (playlistPanel.activePlaylist.mode || 0) : 0
        onActivated: playlistPanel.setMode(currentIndex)
    }

    RowLayout {
        visible: playlistPanel.activePlaylist && (playlistPanel.activePlaylist.mode || 0) !== 2
        QQC2.Label { text: qsTr("Interval:") }
        QQC2.SpinBox {
            from: 1; to: 1440; stepSize: 5; editable: true
            value: playlistPanel.activePlaylist ? (playlistPanel.activePlaylist.interval || 30) : 30
            onValueModified: playlistPanel.setInterval(value)
        }
        QQC2.Label { text: qsTr("min") }
    }

    // Playlist items
    Repeater {
        model: playlistPanel.activePlaylist ? (playlistPanel.activePlaylist.items || []) : []
        delegate: RowLayout {
            Layout.fillWidth: true
            required property var modelData
            required property int index
            spacing: Kirigami.Units.smallSpacing

            Image {
                source: (modelData.previewPath || "").length > 0 ? ("file://" + modelData.previewPath) : ""
                Layout.preferredWidth: 48
                Layout.preferredHeight: 32
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: (modelData.previewPath || "").length > 0
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                QQC2.Label {
                    text: (index + 1) + ". " + (modelData.title || qsTr("Untitled"))
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                QQC2.Label {
                    text: {
                        const parts = []
                        if (modelData.weType) parts.push(modelData.weType)
                        if (modelData.workshopId) parts.push(modelData.workshopId)
                        return parts.join(" · ")
                    }
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    opacity: 0.5
                    visible: text.length > 0
                }
            }

            QQC2.TextField {
                visible: playlistPanel.activePlaylist && (playlistPanel.activePlaylist.mode || 0) === 2
                Layout.preferredWidth: 60
                text: modelData.scheduleTime || "00:00"
                placeholderText: "HH:MM"
                onEditingFinished: playlistPanel.setItemTime(index, text)
            }

            QQC2.ToolButton {
                icon.name: "go-up"
                enabled: index > 0
                onClicked: playlistPanel.moveItemUp(index)
            }

            QQC2.ToolButton {
                icon.name: "edit-delete"
                onClicked: playlistPanel.removeItem(index)
            }
        }
    }

    QQC2.Label {
        text: playlistPanel.activePlaylist
            ? qsTr("Select wallpapers below to add to \"%1\":").arg(playlistPanel.activePlaylist.name)
            : qsTr("Create a playlist first.")
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        opacity: 0.7
    }

    // Dialogs
    QQC2.Dialog {
        id: nameDialog
        property string mode: "create"
        title: mode === "create" ? qsTr("New playlist") : qsTr("Rename playlist")
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        modal: true
        anchors.centerIn: parent
        contentItem: QQC2.TextField {
            id: nameInput
            placeholderText: qsTr("Playlist name")
        }
        onAccepted: {
            if (mode === "create") playlistPanel.createPlaylist(nameInput.text)
            else playlistPanel.renamePlaylist(playlistPanel.activePlaylistIndex, nameInput.text)
        }
    }

    QQC2.Dialog {
        id: deleteDialog
        title: qsTr("Delete playlist")
        standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.No
        modal: true
        anchors.centerIn: parent
        contentItem: QQC2.Label {
            text: playlistPanel.activePlaylist
                ? qsTr("Are you sure you want to delete \"%1\"?").arg(playlistPanel.activePlaylist.name)
                : ""
            wrapMode: Text.WordWrap
        }
        onAccepted: playlistPanel.deletePlaylist(playlistPanel.activePlaylistIndex)
    }
}
