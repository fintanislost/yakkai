/*
    SPDX-FileCopyrightText: 2026 Team7
    SPDX-License-Identifier: MIT

    Playlist runtime — cycles through playlist items, applies wallpaper config.
    Each item stores its type (scene/video/web) and source info.
*/

import QtQuick

QtObject {
    id: player

    property string playlistsJson: ""
    property int activePlaylistIndex: -1
    property bool active: false
    property int currentIndex: 0

    // Parsed data
    readonly property var playlistData: {
        if (!playlistsJson || playlistsJson.length === 0) return { playlists: [] }
        try { return JSON.parse(playlistsJson) } catch(e) { return { playlists: [] } }
    }
    readonly property var playlist: {
        const idx = activePlaylistIndex
        const pls = playlistData.playlists || []
        return (idx >= 0 && idx < pls.length) ? pls[idx] : null
    }
    readonly property var items: (playlist && playlist.items) ? playlist.items : []
    readonly property var currentItem: (items && currentIndex >= 0 && currentIndex < items.length) ? items[currentIndex] : null
    readonly property string currentType: currentItem ? (currentItem.weType || "scene") : "scene"
    readonly property int cycleMode: playlist ? (playlist.mode || 0) : 0
    readonly property int intervalMinutes: playlist ? (playlist.interval || 30) : 30

    // Output signals
    signal applyItem(var item)

    function tryApply() {
        if (!active || !items || items.length === 0) return
        if (cycleMode === 2) {
            checkSchedule()
        } else {
            if (currentIndex < 0 || currentIndex >= items.length)
                currentIndex = 0
            var item = items[currentIndex]
            console.log("[Yakkai] playlist tryApply: index=" + currentIndex
                + " title=" + (item ? item.title : "null")
                + " type=" + (item ? item.weType : "null")
                + " items=" + items.length)
            applyItem(item)
        }
    }

    function advance() {
        if (!items || items.length < 2) return
        if (cycleMode === 1) {
            // Random
            let next = Math.floor(Math.random() * items.length)
            while (next === currentIndex && items.length > 1)
                next = Math.floor(Math.random() * items.length)
            currentIndex = next
        } else {
            currentIndex = (currentIndex + 1) % items.length
        }
        applyItem(items[currentIndex])
    }

    function checkSchedule() {
        if (!items || items.length === 0) return
        const now = new Date()
        const nowMins = now.getHours() * 60 + now.getMinutes()
        let bestMins = -1
        let candidates = []
        for (let i = 0; i < items.length; i++) {
            const t = items[i].scheduleTime || "00:00"
            const parts = t.split(":")
            const mins = parseInt(parts[0] || "0") * 60 + parseInt(parts[1] || "0")
            if (mins <= nowMins && mins > bestMins) {
                bestMins = mins
                candidates = [i]
            } else if (mins <= nowMins && mins === bestMins) {
                candidates.push(i)
            }
        }
        if (bestMins < 0) {
            for (let i = 0; i < items.length; i++) {
                const t = items[i].scheduleTime || "00:00"
                const parts = t.split(":")
                const mins = parseInt(parts[0] || "0") * 60 + parseInt(parts[1] || "0")
                if (mins > bestMins) {
                    bestMins = mins
                    candidates = [i]
                } else if (mins === bestMins) {
                    candidates.push(i)
                }
            }
        }
        const bestIdx = candidates[Math.floor(Math.random() * candidates.length)]
        currentIndex = bestIdx
        applyItem(items[bestIdx])
    }

    // Timers
    property Timer cycleTimer: Timer {
        interval: player.intervalMinutes * 60 * 1000
        repeat: true
        running: player.active && player.items && player.items.length > 1 && player.cycleMode !== 2
        onTriggered: player.advance()
    }

    property Timer scheduleTimer: Timer {
        interval: 60000
        repeat: true
        running: player.active && player.items && player.items.length > 1 && player.cycleMode === 2
        onTriggered: player.checkSchedule()
    }

    onActiveChanged: {
        currentIndex = 0
        tryApply()
    }
    onPlaylistDataChanged: {
        if (active) {
            currentIndex = 0
            tryApply()
        }
    }
    onActivePlaylistIndexChanged: {
        currentIndex = 0
        if (active) tryApply()
    }
}
