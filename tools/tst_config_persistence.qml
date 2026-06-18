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

    function createSceneMediaProperties(initialProperties) {
        const component = Qt.createComponent("../wallpapers/io.team7.yakkai/contents/ui/SceneMediaProperties.qml")
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
                    "UmbrellaSelectedType",
                    "LinuxMediaIntegrationEnabled"
                ]
            },
            ContentMode: 8,
            WEVideoLibraryPath: "/tmp/Steam/",
            PlaylistAllJson: playlistJson,
            ActivePlaylistAllIndex: 0,
            UmbrellaSelectedType: "video",
            LinuxMediaIntegrationEnabled: false
        }
        const item = createConfig({
            wallpaperConfiguration: wallpaperConfiguration
        })

        compare(item.cfg_ContentMode, 8)
        compare(item.cfg_WEVideoLibraryPath, "/tmp/Steam/")
        compare(item.cfg_PlaylistAllJson, playlistJson)
        compare(item.cfg_ActivePlaylistAllIndex, 0)
        compare(item.cfg_UmbrellaSelectedType, "video")
        compare(item.cfg_LinuxMediaIntegrationEnabled, false)

        item.destroy()
    }

    function test_sceneMediaPropertiesEnabledMergePreservesSceneAndInsertsMedia() {
        const item = createSceneMediaProperties({
            scenePropertiesJson: "{\"volume\":0.65,\"mode\":\"scene\",\"nested\":{\"enabled\":true}}",
            mediaJson: "{\"__yakkaiMedia\":{\"title\":\"Track\",\"artist\":\"Artist\",\"playing\":true}}",
            mediaIntegrationEnabled: true
        })

        const merged = JSON.parse(item.mergedScenePropertiesJson)
        compare(merged.volume, 0.65)
        compare(merged.mode, "scene")
        compare(merged.nested.enabled, true)
        compare(merged.__yakkaiMedia.title, "Track")
        compare(merged.__yakkaiMedia.artist, "Artist")
        compare(merged.__yakkaiMedia.playing, true)

        item.destroy()
    }

    function test_sceneMediaPropertiesDisabledMergeRemovesSyntheticPayload() {
        const item = createSceneMediaProperties({
            scenePropertiesJson: "{\"volume\":0.2,\"__yakkaiMedia\":{\"title\":\"Old\"},\"mode\":\"scene\"}",
            mediaJson: "{\"__yakkaiMedia\":{\"title\":\"New\"}}",
            mediaIntegrationEnabled: false
        })

        const merged = JSON.parse(item.mergedScenePropertiesJson)
        compare(merged.volume, 0.2)
        compare(merged.mode, "scene")
        verify(!merged.hasOwnProperty("__yakkaiMedia"))

        item.destroy()
    }

    function test_sceneMediaPropertiesDefensivelyIgnoresInvalidJsonAndMissingMediaObject() {
        const invalidItem = createSceneMediaProperties({
            scenePropertiesJson: "{not json",
            mediaJson: "{\"__yakkaiMedia\":{\"title\":\"Track\"}}",
            mediaIntegrationEnabled: true
        })
        compare(invalidItem.mergedScenePropertiesJson, "{\"__yakkaiMedia\":{\"title\":\"Track\"}}")
        invalidItem.destroy()

        const missingMediaItem = createSceneMediaProperties({
            scenePropertiesJson: "{\"volume\":0.4,\"__yakkaiMedia\":{\"title\":\"Old\"}}",
            mediaJson: "{\"__yakkaiMedia\":\"not an object\"}",
            mediaIntegrationEnabled: true
        })
        const merged = JSON.parse(missingMediaItem.mergedScenePropertiesJson)
        compare(merged.volume, 0.4)
        verify(!merged.hasOwnProperty("__yakkaiMedia"))
        missingMediaItem.destroy()
    }

    function test_sceneMediaPropertiesRuntimeMediaUpdatesWithoutChangingStableSceneProperties() {
        const item = createSceneMediaProperties({
            scenePropertiesJson: "{\"volume\":0.65}",
            mediaJson: "{\"__yakkaiMedia\":{\"title\":\"Track\",\"position\":42}}",
            runtimeMediaJson: "{\"__yakkaiMedia\":{\"title\":\"Track\",\"position\":42}}",
            mediaIntegrationEnabled: true
        })

        const stableBefore = item.mergedScenePropertiesJson
        item.runtimeMediaJson = "{\"__yakkaiMedia\":{\"title\":\"Track\",\"position\":84}}"

        compare(item.mergedScenePropertiesJson, stableBefore)
        const runtimeMedia = JSON.parse(item.mergedRuntimeMediaJson).__yakkaiMedia
        compare(runtimeMedia.title, "Track")
        compare(runtimeMedia.position, 84)

        item.destroy()
    }

    function test_sceneMediaPropertiesRuntimeMediaSanitizesInvalidPayload() {
        const item = createSceneMediaProperties({
            runtimeMediaJson: "{not json",
            mediaIntegrationEnabled: true
        })

        compare(item.mergedRuntimeMediaJson, "{}")

        item.destroy()
    }

    function test_sceneMediaPropertiesRuntimeMediaDisabledReturnsEmptyObject() {
        const item = createSceneMediaProperties({
            runtimeMediaJson: "{\"__yakkaiMedia\":{\"title\":\"Track\",\"position\":84}}",
            mediaIntegrationEnabled: false
        })

        compare(item.mergedRuntimeMediaJson, "{}")

        item.destroy()
    }
}
