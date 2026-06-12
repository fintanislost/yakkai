/*
    SPDX-FileCopyrightText: 2026 Team7

    SPDX-License-Identifier: MIT
*/

import QtQuick
import QtWebEngine

Item {
    id: root

    property url webSource: ""
    property bool muted: true
    property bool captureExiting: false
    property string emptyMessage: qsTr("Select a Wallpaper Engine web wallpaper in the wallpaper settings.")
    property string projectTitle: ""
    property string userPropertiesJson: "{}"
    property bool pageLoaded: false
    property bool propertiesSent: false
    property int propertyPushAttempts: 0
    property bool compatScriptInstalled: false
    property string loadErrorText: ""
    property string runtimeDiagnostics: ""
    property bool showDiagnosticsOverlay: false
    property bool debugSyntheticAudioEnabled: false
    property int debugSyntheticAudioBins: 128
    property int debugSyntheticAudioIntervalMs: 33
    property real debugSyntheticAudioOriginMs: 0
    property int syntheticAudioStartAttempts: 0
    property real debugSyntheticAudioRate: 0.18
    readonly property string logPrefix: "[Yakkai]"
    readonly property int maxPropertyPushAttempts: 80
    readonly property int maxSyntheticAudioStartAttempts: 40
    readonly property var generalProperties: ({ fps: 24 })
    readonly property var parsedUserProperties: parseUserProperties(userPropertiesJson)
    readonly property string statusText: {
        if (webSource.toString().length === 0) {
            return emptyMessage
        }

        if (loadErrorText.length > 0) {
            return loadErrorText
        }

        if (webView.loading) {
            return qsTr("Loading Wallpaper Engine web wallpaper…")
        }

        if (!pageLoaded) {
            return qsTr("Waiting for the Wallpaper Engine page to finish loading…")
        }

        return ""
    }
    readonly property string diagnosticText: {
        if (statusText.length === 0 && pageLoaded && propertiesSent && runtimeDiagnostics.length === 0) {
            return ""
        }

        const diagnostics = runtimeDiagnostics.length > 0
            ? " | " + runtimeDiagnostics
            : ""

        return qsTr("Title: %1 | URL: %2 | Properties sent: %3")
            .arg(projectTitle.length > 0 ? projectTitle : qsTr("Unknown"))
            .arg(String(webSource))
            .arg(propertiesSent ? qsTr("yes") : qsTr("no")) + diagnostics
    }

    function log(message) {
        console.log(logPrefix + " " + message)
    }

    function shouldShowDiagnosticOverlay() {
        return showDiagnosticsOverlay && diagnosticText.length > 0
    }

    function parseUserProperties(source) {
        const text = String(source ?? "")

        if (text.length === 0) {
            return {}
        }

        try {
            const parsed = JSON.parse(text)
            return parsed && typeof parsed === "object" ? parsed : {}
        } catch (error) {
            log("failed to parse WE web properties JSON: " + error)
            return {}
        }
    }

    function pushProperties() {
        if (captureExiting || !pageLoaded) {
            return
        }

        propertyPushAttempts += 1
        const generalJson = JSON.stringify(generalProperties)
        const userJson = JSON.stringify(parsedUserProperties)
        const script = `
            (function() {
                if (!window.__paperGradientSetProperties) {
                    return false;
                }

                return window.__paperGradientSetProperties(${generalJson}, ${userJson}) === true;
            })();
        `

        webView.runJavaScript(script, function(result) {
            if (captureExiting) {
                return
            }

            propertiesSent = result === true

            if (propertiesSent) {
                propertyRetryTimer.stop()
                runtimeDiagnostics = ""
                log("sent WE web property payload for " + projectTitle + " after " + propertyPushAttempts + " attempt(s)")
            } else {
                log("WE web property shim did not accept payload on attempt " + propertyPushAttempts)
                schedulePropertyRetry()
            }
        })
    }

    function schedulePropertyRetry() {
        if (captureExiting || !pageLoaded || propertiesSent) {
            propertyRetryTimer.stop()
            return
        }

        if (propertyPushAttempts >= maxPropertyPushAttempts) {
            propertyRetryTimer.stop()
            log("WE web property payload was not accepted after " + propertyPushAttempts + " attempt(s)")
            return
        }

        propertyRetryTimer.restart()
    }

    function loadWebSource() {
        if (!compatScriptInstalled) {
            return
        }

        webView.url = webSource
    }

    function updateRuntimeDiagnostics() {
        if (!pageLoaded) {
            runtimeDiagnostics = ""
            return
        }

        const script = `
            (function() {
                function moduleState(name) {
                    const target = window[name];
                    if (!target) {
                        return null;
                    }

                    let loaded = null;
                    try {
                        loaded = typeof target.loaded === "function" ? target.loaded() : null;
                    } catch (error) {
                        loaded = "error:" + String(error);
                    }

                    return {
                        load: target.load || null,
                        loaded: loaded
                    };
                }

                const loaderElement = document.querySelector("#loader");
                const loaderStyle = loaderElement ? window.getComputedStyle(loaderElement) : null;
                const latest = window.__paperGradientLatestProperties || {};
                const latestUser = latest.user || {};

                return JSON.stringify({
                    readyState: document.readyState,
                    hasShim: typeof window.__paperGradientSetProperties === "function",
                    hasListener: !!window.wallpaperPropertyListener,
                    latestUserKeys: Object.keys(latestUser).length,
                    loaderDisplay: loaderStyle ? loaderStyle.display : null,
                    loaderOpacity: loaderElement ? (loaderElement.style.opacity || loaderStyle.opacity) : null,
                    weather: moduleState("weather"),
                    date: moduleState("date"),
                    note: moduleState("note")
                });
            })();
        `

        webView.runJavaScript(script, function(result) {
            runtimeDiagnostics = String(result ?? "")
        })
    }

    function scheduleSyntheticAudioRetry() {
        if (captureExiting || !pageLoaded || !debugSyntheticAudioEnabled) {
            syntheticAudioRetryTimer.stop()
            return
        }

        if (syntheticAudioStartAttempts >= maxSyntheticAudioStartAttempts) {
            syntheticAudioRetryTimer.stop()
            log("WE web synthetic audio did not start after " + syntheticAudioStartAttempts + " attempt(s)")
            return
        }

        syntheticAudioRetryTimer.restart()
    }

    function startSyntheticAudio() {
        if (captureExiting || !pageLoaded) {
            syntheticAudioRetryTimer.stop()
            return
        }

        if (!debugSyntheticAudioEnabled) {
            stopSyntheticAudio()
            return
        }

        syntheticAudioStartAttempts += 1
        const bins = Math.max(8, debugSyntheticAudioBins)
        const interval = Math.max(16, debugSyntheticAudioIntervalMs)
        const phaseRate = debugSyntheticAudioRate
        const originMs = debugSyntheticAudioOriginMs > 0 ? debugSyntheticAudioOriginMs : Date.now()
        webView.runJavaScript(`
            (function() {
                if (!window.wpeQml || !window.wpeQml.sigAudio) {
                    return false;
                }

                if (window.__yakkaiSyntheticAudioTimer) {
                    clearInterval(window.__yakkaiSyntheticAudioTimer);
                }

                window.__yakkaiSyntheticAudioOriginMs = ${originMs};
                function emitSyntheticAudio() {
                    const elapsedIntervals = Math.max(0, (Date.now() - window.__yakkaiSyntheticAudioOriginMs) / ${interval});
                    const phase = elapsedIntervals * ${phaseRate};
                    const values = [];
                    for (let index = 0; index < ${bins}; ++index) {
                        const wave = Math.sin(phase + index * 0.21);
                        const pulse = Math.sin(phase * 0.37 + index * 0.07);
                        values.push(Math.max(0, Math.min(1, 0.55 + wave * 0.35 + pulse * 0.10)));
                    }
                    window.wpeQml.sigAudio.emit(values);
                }

                emitSyntheticAudio();
                window.__yakkaiSyntheticAudioTimer = setInterval(emitSyntheticAudio, ${interval});
                return true;
            })();
        `, function(result) {
            if (captureExiting) {
                return
            }

            if (result === true) {
                syntheticAudioRetryTimer.stop()
                syntheticAudioStartAttempts = 0
            } else {
                scheduleSyntheticAudioRetry()
            }
        })
    }

    function stopSyntheticAudio() {
        syntheticAudioRetryTimer.stop()
        syntheticAudioStartAttempts = 0

        if (!compatScriptInstalled) {
            return
        }

        webView.runJavaScript(`
            (function() {
                if (window.__yakkaiSyntheticAudioTimer) {
                    clearInterval(window.__yakkaiSyntheticAudioTimer);
                    window.__yakkaiSyntheticAudioTimer = null;
                }
                return true;
            })();
        `)
    }

    function prepareForCaptureExit() {
        captureExiting = true
        propertyRetryTimer.stop()
        runtimeDiagnosticsTimer.stop()
        syntheticAudioRetryTimer.stop()
        syntheticAudioStartAttempts = 0
        webView.runJavaScript(`
            (function() {
                if (window.__yakkaiSyntheticAudioTimer) {
                    clearInterval(window.__yakkaiSyntheticAudioTimer);
                    window.__yakkaiSyntheticAudioTimer = null;
                }
                document.querySelectorAll("video,audio").forEach(function(element) {
                    try {
                        element.pause();
                    } catch (error) {
                    }
                });
                return true;
            })();
        `)
    }

    onWebSourceChanged: {
        stopSyntheticAudio()
        pageLoaded = false
        propertiesSent = false
        propertyPushAttempts = 0
        runtimeDiagnostics = ""
        propertyRetryTimer.stop()
        loadErrorText = ""
        log("webSource=" + String(webSource))
        loadWebSource()
    }
    onUserPropertiesJsonChanged: {
        propertiesSent = false
        propertyPushAttempts = 0
        runtimeDiagnostics = ""

        if (pageLoaded) {
            pushProperties()
        }
    }
    onDebugSyntheticAudioEnabledChanged: {
        syntheticAudioStartAttempts = 0
        if (debugSyntheticAudioEnabled) {
            startSyntheticAudio()
        } else {
            stopSyntheticAudio()
        }
    }
    onDebugSyntheticAudioBinsChanged: {
        if (debugSyntheticAudioEnabled) {
            syntheticAudioStartAttempts = 0
            startSyntheticAudio()
        }
    }
    onDebugSyntheticAudioIntervalMsChanged: {
        if (debugSyntheticAudioEnabled) {
            syntheticAudioStartAttempts = 0
            startSyntheticAudio()
        }
    }
    onMutedChanged: log("web audio muted=" + muted)

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    WebEngineView {
        id: webView
        anchors.fill: parent
        url: ""
        audioMuted: root.captureExiting || root.muted
        activeFocusOnPress: false

        Component.onCompleted: {
            settings.fullscreenSupportEnabled = true
            settings.autoLoadIconsForPage = false
            settings.printElementBackgrounds = false
            settings.playbackRequiresUserGesture = false
            settings.pdfViewerEnabled = false
            settings.showScrollBars = false
            settings.localContentCanAccessRemoteUrls = true
            settings.webGLEnabled = true
            settings.accelerated2dCanvasEnabled = true
            settings.allowGeolocationOnInsecureOrigins = true

            userScripts.insert([
                {
                    injectionPoint: WebEngineScript.DocumentCreation,
                    worldId: WebEngineScript.MainWorld,
                    name: "PaperGradientCompat",
                    sourceCode: `
                        (function() {
                            function createSignal() {
                                const listeners = [];

                                return {
                                    connect: function(listener) {
                                        if (typeof listener === "function" && listeners.indexOf(listener) === -1) {
                                            listeners.push(listener);
                                        }
                                    },
                                    disconnect: function(listener) {
                                        const index = listeners.indexOf(listener);
                                        if (index >= 0) {
                                            listeners.splice(index, 1);
                                        }
                                    },
                                    emit: function(payload) {
                                        listeners.slice().forEach(function(listener) {
                                            try {
                                                listener(payload);
                                            } catch (error) {
                                                console.error("[Yakkai] WE web listener failed", error);
                                            }
                                        });
                                    }
                                };
                            }

                            const signalGeneral = createSignal();
                            const signalUser = createSignal();
                            const signalAudio = createSignal();
                            const signalMediaProperties = createSignal();
                            const signalMediaThumbnail = createSignal();
                            const signalMediaTimeline = createSignal();
                            const signalMediaPlayback = createSignal();

                            if (!window.wpeQml) {
                                window.wpeQml = {
                                    loaded: true,
                                    sigGeneralProperties: signalGeneral,
                                    sigUserProperties: signalUser,
                                    sigAudio: signalAudio,
                                    sigMediaProperties: signalMediaProperties,
                                    sigMediaThumbnail: signalMediaThumbnail,
                                    sigMediaTimeline: signalMediaTimeline,
                                    sigMediaPlayback: signalMediaPlayback
                                };
                            } else {
                                window.wpeQml.loaded = true;
                                if (!window.wpeQml.sigGeneralProperties) {
                                    window.wpeQml.sigGeneralProperties = signalGeneral;
                                }
                                if (!window.wpeQml.sigUserProperties) {
                                    window.wpeQml.sigUserProperties = signalUser;
                                }
                                if (!window.wpeQml.sigAudio) {
                                    window.wpeQml.sigAudio = signalAudio;
                                }
                                if (!window.wpeQml.sigMediaProperties) {
                                    window.wpeQml.sigMediaProperties = signalMediaProperties;
                                }
                                if (!window.wpeQml.sigMediaThumbnail) {
                                    window.wpeQml.sigMediaThumbnail = signalMediaThumbnail;
                                }
                                if (!window.wpeQml.sigMediaTimeline) {
                                    window.wpeQml.sigMediaTimeline = signalMediaTimeline;
                                }
                                if (!window.wpeQml.sigMediaPlayback) {
                                    window.wpeQml.sigMediaPlayback = signalMediaPlayback;
                                }
                            }

                            window.__paperGradientLatestProperties = {
                                general: {},
                                user: {}
                            };

                            function updateWallpaperEngineViewportGlobals() {
                                window.width = window.innerWidth;
                                window.height = window.innerHeight;
                            }

                            updateWallpaperEngineViewportGlobals();
                            window.addEventListener("resize", updateWallpaperEngineViewportGlobals);

                            window.wallpaperRegisterAudioListener = function(listener) {
                                window.wpeQml.sigAudio.connect(listener);
                            };

                            window.wallpaperRegisterMediaPropertiesListener = function(listener) {
                                window.wpeQml.sigMediaProperties.connect(listener);
                            };

                            window.wallpaperRegisterMediaThumbnailListener = function(listener) {
                                window.wpeQml.sigMediaThumbnail.connect(listener);
                            };

                            window.wallpaperRegisterMediaTimelineListener = function(listener) {
                                window.wpeQml.sigMediaTimeline.connect(listener);
                            };

                            window.wallpaperRegisterMediaPlaybackListener = function(listener) {
                                window.wpeQml.sigMediaPlayback.connect(listener);
                            };

                            function deliverToPropertyListener() {
                                const propertyListener = window.wallpaperPropertyListener;
                                if (!propertyListener) {
                                    return false;
                                }

                                try {
                                    if (propertyListener.applyGeneralProperties) {
                                        propertyListener.applyGeneralProperties(window.__paperGradientLatestProperties.general);
                                    }

                                    if (propertyListener.applyUserProperties) {
                                        propertyListener.applyUserProperties(window.__paperGradientLatestProperties.user);
                                    }

                                    return true;
                                } catch (error) {
                                    console.error("[Yakkai] WE web property listener failed", error);
                                    return false;
                                }
                            }

                            window.__paperGradientSetProperties = function(generalProperties, userProperties) {
                                window.__paperGradientLatestProperties.general = generalProperties || {};
                                window.__paperGradientLatestProperties.user = userProperties || {};
                                window.wpeQml.sigGeneralProperties.emit(window.__paperGradientLatestProperties.general);
                                window.wpeQml.sigUserProperties.emit(window.__paperGradientLatestProperties.user);
                                return deliverToPropertyListener();
                            };

                            let bindTries = 0;
                            const bindTimer = setInterval(function() {
                                bindTries += 1;
                                if (deliverToPropertyListener() || bindTries >= 40) {
                                    clearInterval(bindTimer);
                                }
                            }, 250);

                            document.addEventListener("DOMContentLoaded", function() {
                                deliverToPropertyListener();
                            }, { once: true });

                            if (document.body) {
                                document.body.ondragstart = function() { return false; };
                            } else {
                                document.addEventListener("DOMContentLoaded", function() {
                                    if (document.body) {
                                        document.body.ondragstart = function() { return false; };
                                    }
                                }, { once: true });
                            }
                        })();
                    `
                }
            ])

            root.log("WE web component completed webgl="
                + settings.webGLEnabled
                + " canvas2d="
                + settings.accelerated2dCanvasEnabled)
            root.compatScriptInstalled = true
            root.loadWebSource()
        }

        onLoadingChanged: function(loadRequest) {
            if (loadRequest.status === WebEngineView.LoadStartedStatus) {
                root.stopSyntheticAudio()
                root.pageLoaded = false
                root.propertiesSent = false
                root.propertyPushAttempts = 0
                root.runtimeDiagnostics = ""
                propertyRetryTimer.stop()
                root.loadErrorText = ""
                root.log("WE web loading started url=" + String(root.webSource))
                return
            }

            if (loadRequest.status === WebEngineView.LoadSucceededStatus) {
                if (root.captureExiting) {
                    return
                }

                root.pageLoaded = true
                root.loadErrorText = ""
                root.propertyPushAttempts = 0
                root.syntheticAudioStartAttempts = 0
                root.log("WE web load succeeded title=" + root.projectTitle)
                root.pushProperties()
                root.updateRuntimeDiagnostics()
                root.startSyntheticAudio()
                return
            }

            if (loadRequest.status === WebEngineView.LoadFailedStatus) {
                root.stopSyntheticAudio()
                root.pageLoaded = false
                root.propertiesSent = false
                root.propertyPushAttempts = 0
                root.runtimeDiagnostics = ""
                propertyRetryTimer.stop()
                root.loadErrorText = loadRequest.errorString && loadRequest.errorString.length > 0
                    ? loadRequest.errorString
                    : qsTr("QtWebEngine could not load this Wallpaper Engine web wallpaper.")
                root.log("WE web load failed error=" + root.loadErrorText)
            }
        }

        onJavaScriptConsoleMessage: function(level, message, lineNumber, sourceID) {
            root.log("WE web console level=" + level + " source=" + sourceID + ":" + lineNumber + " message=" + message)
        }
    }

    Timer {
        id: propertyRetryTimer
        interval: 250
        repeat: false

        onTriggered: root.pushProperties()
    }

    Timer {
        id: runtimeDiagnosticsTimer
        interval: 1000
        repeat: true
        running: root.pageLoaded && root.showDiagnosticsOverlay && !root.propertiesSent

        onTriggered: root.updateRuntimeDiagnostics()
    }

    Timer {
        id: syntheticAudioRetryTimer
        interval: Math.max(100, root.debugSyntheticAudioIntervalMs)
        repeat: false

        onTriggered: root.startSyntheticAudio()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.72, 620)
        height: statusColumn.implicitHeight + 32
        radius: 10
        color: "#66000000"
        visible: statusColumn.visible
    }

    Column {
        id: statusColumn
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.62, 580)
        spacing: 6
        visible: statusTextLabel.visible || diagnosticLabel.visible
    }

    Text {
        id: statusTextLabel
        parent: statusColumn
        width: statusColumn.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "white"
        text: root.statusText
        visible: text.length > 0
    }

    Text {
        id: diagnosticLabel
        parent: statusColumn
        width: statusColumn.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#d0d0d0"
        font.pixelSize: Math.max(11, statusTextLabel.font.pixelSize - 1)
        text: root.diagnosticText
        visible: root.shouldShowDiagnosticOverlay()
    }
}
