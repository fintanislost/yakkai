/*
    SPDX-FileCopyrightText: 2026 Team7

    SPDX-License-Identifier: MIT
*/

import QtQuick
import QtTest

TestCase {
    name: "ConfigPersistence"

    function createConfig(initialProperties) {
        const component = Qt.createComponent("../wallpapers/io.team7.yakkai/contents/ui/config.qml")
        compare(component.status, Component.Ready, component.errorString())
        const item = component.createObject(null, initialProperties)
        verify(item !== null, component.errorString())
        return item
    }

    function test_umbrellaSelectionRestoresFromPersistedProjectPath() {
        const projectPath = "/tmp/wallpapers/video/project.json"
        const item = createConfig({
            cfg_ContentMode: 7,
            cfg_UmbrellaSelectedType: "video",
            cfg_WEVideoProjectPath: projectPath,
            cfg_WEVideoProjectTitle: "Saved video",
            cfg_WEVideoSource: "/tmp/wallpapers/video/video.mp4"
        })

        item.wallpaperEngineVideoItems = [
            {
                projectPath: projectPath,
                title: "Saved video",
                sourcePath: "/tmp/wallpapers/video/video.mp4",
                weType: "video"
            }
        ]

        verify(item.selectedWallpaperEngineProject !== null)
        compare(item.selectedWallpaperEngineProject.projectPath, projectPath)

        item.destroy()
    }

    function test_configHydratesFromWallpaperConfiguration() {
        const playlistJson = "{\"playlists\":[{\"name\":\"Saved\",\"items\":[]}]}"
        const wallpaperConfiguration = {
            keys: function() {
                return [
                    "ContentMode",
                    "WEVideoLibraryPath",
                    "PlaylistAllJson",
                    "ActivePlaylistAllIndex",
                    "UmbrellaSelectedType"
                ]
            },
            ContentMode: 8,
            WEVideoLibraryPath: "/tmp/Steam/",
            PlaylistAllJson: playlistJson,
            ActivePlaylistAllIndex: 0,
            UmbrellaSelectedType: "video"
        }
        const item = createConfig({
            wallpaperConfiguration: wallpaperConfiguration
        })

        compare(item.cfg_ContentMode, 8)
        compare(item.cfg_WEVideoLibraryPath, "/tmp/Steam/")
        compare(item.cfg_PlaylistAllJson, playlistJson)
        compare(item.cfg_ActivePlaylistAllIndex, 0)
        compare(item.cfg_UmbrellaSelectedType, "video")

        item.destroy()
    }
}
