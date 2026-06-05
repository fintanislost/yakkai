#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QSize>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

#include "CaptureGate.hpp"
#include "PuppetSimulationOption.hpp"
#include "ScenePropertiesOption.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace
{
constexpr int MaxCaptureSequenceFrames = 3600;
constexpr int CaptureExitDrainMs = 500;
constexpr int CaptureReadyPollMs = 50;
constexpr int CaptureReadyTimeoutMs = 60000;
constexpr int DebugEffectManifestWaitMs = 5000;
constexpr int DebugEffectManifestPollMs = 50;

struct CaptureRequest
{
    int timeMs = 0;
    QString fileName;
};

enum CaptureStatus
{
    CaptureSuccess = 0,
    CaptureNullImage = 4,
    CaptureSaveFailed = 5,
};

std::optional<int> parseNonNegativeInt(const QString& value)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed < 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::vector<CaptureRequest>> parseCaptureTimes(const QString& value)
{
    std::vector<CaptureRequest> captures;
    const QStringList parts = value.split(',', Qt::KeepEmptyParts);
    for (const QString& rawPart : parts) {
        const QString trimmed = rawPart.trimmed();
        if (trimmed.isEmpty()) {
            return std::nullopt;
        }
        const std::optional<int> parsed = parseNonNegativeInt(trimmed);
        if (!parsed) {
            return std::nullopt;
        }
        captures.push_back(CaptureRequest{*parsed, QStringLiteral("frame-%1ms.png").arg(*parsed, 8, 10, QLatin1Char('0'))});
    }
    std::sort(captures.begin(), captures.end(), [](const CaptureRequest& left, const CaptureRequest& right) {
        return left.timeMs < right.timeMs;
    });
    const auto duplicate = std::adjacent_find(captures.begin(), captures.end(), [](const CaptureRequest& left, const CaptureRequest& right) {
        return left.timeMs == right.timeMs;
    });
    if (duplicate != captures.end()) {
        return std::nullopt;
    }
    return captures;
}

std::optional<std::vector<CaptureRequest>> parseCaptureSequence(const QString& value)
{
    const QStringList parts = value.split(':');
    if (parts.size() != 3) {
        return std::nullopt;
    }

    const std::optional<int> startMs = parseNonNegativeInt(parts.at(0).trimmed());
    const std::optional<int> frameCount = parseNonNegativeInt(parts.at(1).trimmed());
    const std::optional<int> intervalMs = parseNonNegativeInt(parts.at(2).trimmed());
    if (!startMs || !frameCount || !intervalMs || *frameCount == 0 || *intervalMs == 0) {
        return std::nullopt;
    }
    if (*frameCount > MaxCaptureSequenceFrames) {
        return std::nullopt;
    }

    std::vector<CaptureRequest> captures;
    captures.reserve(static_cast<size_t>(*frameCount));
    for (int index = 0; index < *frameCount; ++index) {
        const qint64 timeMs64 = static_cast<qint64>(*startMs) + static_cast<qint64>(index) * static_cast<qint64>(*intervalMs);
        if (timeMs64 > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        const int timeMs = static_cast<int>(timeMs64);
        captures.push_back(CaptureRequest{timeMs, QStringLiteral("frame-%1.png").arg(index, 4, 10, QLatin1Char('0'))});
    }
    return captures;
}

std::optional<QSize> parseWindowSize(const QString& value)
{
    const QStringList parts = value.trimmed().toLower().split('x', Qt::KeepEmptyParts);
    if (parts.size() != 2) {
        return std::nullopt;
    }

    const std::optional<int> width = parseNonNegativeInt(parts.at(0).trimmed());
    const std::optional<int> height = parseNonNegativeInt(parts.at(1).trimmed());
    if (!width || !height || *width == 0 || *height == 0) {
        return std::nullopt;
    }
    return QSize(*width, *height);
}

std::optional<QString> parsePositiveIdList(const QString& value)
{
    std::vector<int> ids;
    const QStringList parts = value.split(',', Qt::KeepEmptyParts);
    for (const QString& rawPart : parts) {
        const QString trimmed = rawPart.trimmed();
        if (trimmed.isEmpty()) {
            return std::nullopt;
        }
        const std::optional<int> parsed = parseNonNegativeInt(trimmed);
        if (!parsed || *parsed == 0) {
            return std::nullopt;
        }
        if (std::find(ids.begin(), ids.end(), *parsed) == ids.end()) {
            ids.push_back(*parsed);
        }
    }
    if (ids.empty()) {
        return std::nullopt;
    }

    QStringList normalized;
    for (int id : ids) {
        normalized.push_back(QString::number(id));
    }
    return normalized.join(QLatin1Char(','));
}

std::optional<QString> parseChannelMapSlotList(const QString& value)
{
    constexpr int kMaxDiagnosticChannelMapSlot = 63;

    std::vector<int> channelSlots;
    const QStringList parts = value.split(',', Qt::KeepEmptyParts);
    for (const QString& rawPart : parts) {
        const QString trimmed = rawPart.trimmed();
        if (trimmed.isEmpty()) {
            return std::nullopt;
        }
        const std::optional<int> parsed = parseNonNegativeInt(trimmed);
        if (!parsed || *parsed > kMaxDiagnosticChannelMapSlot) {
            return std::nullopt;
        }
        if (std::find(channelSlots.begin(), channelSlots.end(), *parsed) == channelSlots.end()) {
            channelSlots.push_back(*parsed);
        }
    }
    if (channelSlots.empty()) {
        return std::nullopt;
    }

    QStringList normalized;
    for (int slot : channelSlots) {
        normalized.push_back(QString::number(slot));
    }
    return normalized.join(QLatin1Char(','));
}

CaptureStatus saveWindowCapture(QQuickWindow* window, const QString& absolutePath)
{
    const QImage image = window->grabWindow();
    qInfo() << "yakkai_scene_harness: capture size=" << image.size()
            << "devicePixelRatio=" << image.devicePixelRatio()
            << "path=" << absolutePath;
    if (image.isNull()) {
        qWarning() << "yakkai_scene_harness: capture image is null";
        return CaptureNullImage;
    }

    if (!image.save(absolutePath)) {
        qWarning() << "yakkai_scene_harness: failed to save capture to" << absolutePath;
        return CaptureSaveFailed;
    }

    return CaptureSuccess;
}

bool readDebugEffectManifestOk(const QString& manifestPath, QString* error)
{
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("debug effect manifest was not written");
        }
        return false;
    }

    const QJsonDocument manifest = QJsonDocument::fromJson(manifestFile.readAll());
    if (!manifest.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("debug effect manifest is not a JSON object");
        }
        return false;
    }

    const QString status = manifest.object().value(QStringLiteral("status")).toString();
    if (status != QStringLiteral("ok")) {
        if (error != nullptr) {
            *error = QStringLiteral("debug effect manifest status is %1").arg(status);
        }
        return false;
    }

    return true;
}

bool waitForDebugEffectManifestOk(const QString& manifestPath)
{
    QString lastError;
    QElapsedTimer timer;
    timer.start();
    do {
        if (readDebugEffectManifestOk(manifestPath, &lastError)) {
            return true;
        }
        QThread::msleep(DebugEffectManifestPollMs);
    } while (timer.elapsed() < DebugEffectManifestWaitMs);

    qWarning().noquote() << "yakkai_scene_harness:" << lastError << manifestPath;
    return false;
}

void requestCaptureExit(QQuickWindow* window, int status)
{
    bool invokedShutdownHook = false;
    if (window != nullptr) {
        invokedShutdownHook = QMetaObject::invokeMethod(window, "prepareForCaptureExit", Qt::DirectConnection);
    }

    qInfo() << "yakkai_scene_harness: capture exit requested status=" << status
            << "shutdownHook=" << invokedShutdownHook
            << "drainMs=" << CaptureExitDrainMs;

    QTimer::singleShot(CaptureExitDrainMs, QCoreApplication::instance(), [status]() {
        QCoreApplication::exit(status);
    });
}

void scheduleSingleCapture(QCoreApplication* app,
                           QPointer<QQuickWindow> guardedWindow,
                           QString absoluteCapturePath,
                           int captureDelayMs)
{
    QTimer::singleShot(std::max(captureDelayMs, 0), app, [guardedWindow, absoluteCapturePath]() {
        if (!guardedWindow) {
            qWarning() << "yakkai_scene_harness: capture window was destroyed before capture";
            QCoreApplication::exit(3);
            return;
        }
        requestCaptureExit(guardedWindow, saveWindowCapture(guardedWindow, absoluteCapturePath));
    });
}

void scheduleMultiCaptures(QCoreApplication* app,
                           QPointer<QQuickWindow> guardedWindow,
                           const std::vector<CaptureRequest>& captures,
                           const QString& absoluteCaptureDir)
{
    auto remaining = std::make_shared<int>(static_cast<int>(captures.size()));
    auto failed = std::make_shared<bool>(false);

    for (const CaptureRequest& capture : captures) {
        const QString absolutePath = QDir(absoluteCaptureDir).filePath(capture.fileName);
        QTimer::singleShot(capture.timeMs, app, [guardedWindow, absolutePath, remaining, failed]() {
            if (!guardedWindow) {
                qWarning() << "yakkai_scene_harness: capture window was destroyed before capture";
                *failed = true;
            } else if (saveWindowCapture(guardedWindow, absolutePath) != CaptureSuccess) {
                *failed = true;
            }

            *remaining -= 1;
            if (*remaining == 0) {
                requestCaptureExit(guardedWindow, *failed ? 5 : 0);
            }
        });
    }
}

void startCaptureTimersAfterFirstFrame(QCoreApplication* app,
                                       QPointer<QQuickWindow> guardedWindow,
                                       std::function<void()> startCaptureTimers)
{
    auto gate = std::make_shared<yakkai::harness::CaptureStartGate>();
    gate->markRootWindowReady();

    auto readyPollTimer = new QTimer(app);
    auto readyTimeoutTimer = new QTimer(app);
    readyPollTimer->setInterval(CaptureReadyPollMs);
    readyTimeoutTimer->setSingleShot(true);

    auto tryStart = std::make_shared<std::function<void()>>();
    *tryStart = [gate, readyPollTimer, readyTimeoutTimer, startCaptureTimers = std::move(startCaptureTimers)]() {
        if (!gate->consumeReadyToStart()) {
            return;
        }

        qInfo() << "yakkai_scene_harness: backend first frame ready; arming capture timers";
        readyPollTimer->stop();
        readyTimeoutTimer->stop();
        readyPollTimer->deleteLater();
        readyTimeoutTimer->deleteLater();
        startCaptureTimers();
    };

    QObject::connect(readyPollTimer, &QTimer::timeout, app, [guardedWindow, gate, tryStart]() {
        if (!guardedWindow) {
            qWarning() << "yakkai_scene_harness: capture window was destroyed before backend first frame";
            QCoreApplication::exit(3);
            return;
        }

        if (guardedWindow->property("captureReady").toBool()) {
            gate->markFirstFrameReady();
            (*tryStart)();
        }
    });
    QObject::connect(readyTimeoutTimer, &QTimer::timeout, app, [guardedWindow]() {
        const QString status = guardedWindow
            ? guardedWindow->property("backendStatus").toString()
            : QStringLiteral("<window destroyed>");
        qWarning() << "yakkai_scene_harness: timed out waiting for backend first frame before capture"
                   << "timeoutMs=" << CaptureReadyTimeoutMs
                   << "backendStatus=" << status;
        QCoreApplication::exit(7);
    });

    qInfo() << "yakkai_scene_harness: waiting for backend first frame before capture"
            << "timeoutMs=" << CaptureReadyTimeoutMs;
    if (guardedWindow && guardedWindow->property("captureReady").toBool()) {
        gate->markFirstFrameReady();
        (*tryStart)();
        return;
    }

    readyPollTimer->start();
    readyTimeoutTimer->start(CaptureReadyTimeoutMs);
}

int fillModeFromString(const QString& fillMode)
{
    if (fillMode == QStringLiteral("fit")) {
        return 1;
    }

    if (fillMode == QStringLiteral("stretch")) {
        return 2;
    }

    return 0;
}

QString backendQmlFile(const QString& qmlDir, const QString& backend)
{
    if (backend == QStringLiteral("paper")) {
        return QDir(qmlDir).filePath(QStringLiteral("YakkaiSceneViewerHarness.qml"));
    }

    return QDir(qmlDir).filePath(QStringLiteral("SystemSceneViewerHarness.qml"));
}
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("yakkai_scene_harness"));
    QCoreApplication::setOrganizationName(QStringLiteral("Team7"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Standalone Yakkai scene harness"));
    parser.addHelpOption();

    QCommandLineOption backendOption(
        QStringList{QStringLiteral("backend")},
        QStringLiteral("Select the backend to load: system or paper."),
        QStringLiteral("backend"),
        QStringLiteral("system")
    );
    QCommandLineOption sourceOption(
        QStringList{QStringLiteral("source")},
        QStringLiteral("Absolute path to scene.json or scene.pkg."),
        QStringLiteral("path")
    );
    QCommandLineOption assetsOption(
        QStringList{QStringLiteral("assets")},
        QStringLiteral("Absolute path to wallpaper_engine/assets."),
        QStringLiteral("path")
    );
    QCommandLineOption fillOption(
        QStringList{QStringLiteral("fill")},
        QStringLiteral("Initial fill mode: crop, fit, or stretch."),
        QStringLiteral("mode"),
        QStringLiteral("crop")
    );
    QCommandLineOption windowSizeOption(
        QStringList{QStringLiteral("window-size")},
        QStringLiteral("Harness window size WIDTHxHEIGHT."),
        QStringLiteral("size"),
        QStringLiteral("1600x900")
    );
    QCommandLineOption mouseOption(
        QStringList{QStringLiteral("mouse")},
        QStringLiteral("Enable mouse and hover input.")
    );
    QCommandLineOption unmutedOption(
        QStringList{QStringLiteral("unmuted")},
        QStringLiteral("Start with audio enabled.")
    );
    QCommandLineOption hideInfoOverlayOption(
        QStringList{QStringLiteral("hide-info-overlay")},
        QStringLiteral("Hide the harness info overlay.")
    );
    QCommandLineOption captureOption(
        QStringList{QStringLiteral("capture")},
        QStringLiteral("Save a window capture to the given path, then exit."),
        QStringLiteral("path")
    );
    QCommandLineOption captureDelayOption(
        QStringList{QStringLiteral("capture-delay-ms")},
        QStringLiteral("Delay before capturing the window."),
        QStringLiteral("ms"),
        QStringLiteral("2500")
    );
    QCommandLineOption captureDirOption(
        QStringList{QStringLiteral("capture-dir")},
        QStringLiteral("Save multiple window captures into the given directory."),
        QStringLiteral("path")
    );
    QCommandLineOption captureTimesOption(
        QStringList{QStringLiteral("capture-times-ms")},
        QStringLiteral("Comma-separated capture timestamps in milliseconds, used with --capture-dir."),
        QStringLiteral("times")
    );
    QCommandLineOption captureSequenceOption(
        QStringList{QStringLiteral("capture-sequence")},
        QStringLiteral("Capture sequence START_MS:FRAME_COUNT:INTERVAL_MS, used with --capture-dir."),
        QStringLiteral("sequence")
    );
    QCommandLineOption debugEffectCapturesOption(
        QStringList{QStringLiteral("debug-effect-captures")},
        QStringLiteral("Write effect input/output/final-publish debug captures and manifest into the given directory. Only supported by --backend paper."),
        QStringLiteral("path")
    );
    QCommandLineOption debugEffectCaptureDelayOption(
        QStringList{QStringLiteral("debug-effect-capture-delay-ms")},
        QStringLiteral("Wait for the requested scene time before dumping debug effect captures. Requires --debug-effect-captures."),
        QStringLiteral("ms"),
        QStringLiteral("0")
    );
    QCommandLineOption debugEffectProbeLayersOption(
        QStringList{QStringLiteral("debug-effect-probe-layers")},
        QStringLiteral("Layer IDs whose stripped puppet mixed effect chains should be rendered only for debug capture. Requires --debug-effect-captures."),
        QStringLiteral("ids")
    );
    QCommandLineOption debugEffectProbeHighRiskLayersOption(
        QStringList{QStringLiteral("debug-effect-probe-high-risk-layers")},
        QStringLiteral("Layer IDs whose stripped high-risk blur/LUT/color-grading effect chains should be rendered only for debug capture. Requires --debug-effect-captures."),
        QStringLiteral("ids")
    );
    QCommandLineOption debugEffectProbeChannelMapSlotsOption(
        QStringList{QStringLiteral("debug-effect-probe-channelmap-slots")},
        QStringLiteral("Channelmap blend slots to force active for stripped puppet layer debug captures. Requires --debug-effect-captures and --debug-effect-probe-layers."),
        QStringLiteral("slots")
    );
    QCommandLineOption debugEffectProbeMaxEffectsOption(
        QStringList{QStringLiteral("debug-effect-probe-max-effects")},
        QStringLiteral("Limit forced debug-probe layers to their first N visible effects. Requires --debug-effect-captures and a probe layer list."),
        QStringLiteral("count")
    );
    QCommandLineOption debugPuppetEffectFinalMeshOption(
        QStringList{QStringLiteral("debug-puppet-effect-final-mesh")},
        QStringLiteral("Harness-only puppet effect final mesh override: layer-card, image-space, or deferred-puppet-final. Requires --debug-effect-captures and a probe layer list."),
        QStringLiteral("mode")
    );
    QCommandLineOption debugPuppetEffectRouteOnlyOption(
        QStringList{QStringLiteral("debug-puppet-effect-route-only")},
        QStringLiteral("Harness-only puppet effect diagnostic that keeps the offscreen puppet route active with no visible effect passes. Requires --debug-effect-captures and a probe layer list.")
    );
    QCommandLineOption debugPuppetAnimationLayerOverridesOption(
        QStringList{QStringLiteral("debug-puppet-animation-layer-overrides")},
        QStringLiteral("Semicolon-separated harness-only puppet animation layer overrides: layerId:animationId:key=value[,key=value]. Requires --debug-effect-captures."),
        QStringLiteral("rules")
    );
    QCommandLineOption scenePropertiesJsonOption(
        QStringList{QStringLiteral("scene-properties-json")},
        QStringLiteral("Scene properties override JSON object forwarded to the paper backend."),
        QStringLiteral("json")
    );
    QCommandLineOption puppetSimulationOption(
        QStringList{QStringLiteral("puppet-simulation")},
        QStringLiteral("Harness-only puppet simulation mode: off, diagnostic, or runtime."),
        QStringLiteral("mode")
    );

    parser.addOption(backendOption);
    parser.addOption(sourceOption);
    parser.addOption(assetsOption);
    parser.addOption(fillOption);
    parser.addOption(windowSizeOption);
    parser.addOption(mouseOption);
    parser.addOption(unmutedOption);
    parser.addOption(hideInfoOverlayOption);
    parser.addOption(captureOption);
    parser.addOption(captureDelayOption);
    parser.addOption(captureDirOption);
    parser.addOption(captureTimesOption);
    parser.addOption(captureSequenceOption);
    parser.addOption(debugEffectCapturesOption);
    parser.addOption(debugEffectCaptureDelayOption);
    parser.addOption(debugEffectProbeLayersOption);
    parser.addOption(debugEffectProbeHighRiskLayersOption);
    parser.addOption(debugEffectProbeChannelMapSlotsOption);
    parser.addOption(debugEffectProbeMaxEffectsOption);
    parser.addOption(debugPuppetEffectFinalMeshOption);
    parser.addOption(debugPuppetEffectRouteOnlyOption);
    parser.addOption(debugPuppetAnimationLayerOverridesOption);
    parser.addOption(scenePropertiesJsonOption);
    parser.addOption(puppetSimulationOption);
    parser.process(app);

    const QString qmlDir = QStringLiteral(YAKKAI_SCENE_HARNESS_QML_DIR);
    const QString buildQmlImportDir = QStringLiteral(YAKKAI_SCENE_HARNESS_BUILD_QML_IMPORT_DIR);
    const QString backend = parser.value(backendOption).trimmed().toLower();
    const QString rawSourcePath = parser.value(sourceOption).trimmed();
    const QString rawAssetsPath = parser.value(assetsOption).trimmed();
    const QString sourcePath = rawSourcePath.isEmpty() ? QString() : QFileInfo(rawSourcePath).absoluteFilePath();
    const QString assetsPath = rawAssetsPath.isEmpty() ? QString() : QFileInfo(rawAssetsPath).absoluteFilePath();
    const QString fillMode = parser.value(fillOption).trimmed().toLower();
    const QString windowSizeValue = parser.value(windowSizeOption).trimmed();
    const QString capturePath = parser.value(captureOption).trimmed();
    const int captureDelayMs = parser.value(captureDelayOption).toInt();
    const QString captureDirPath = parser.value(captureDirOption).trimmed();
    const QString captureTimesValue = parser.value(captureTimesOption).trimmed();
    const QString captureSequenceValue = parser.value(captureSequenceOption).trimmed();
    const QString debugEffectCapturesPath = parser.value(debugEffectCapturesOption).trimmed();
    const QString debugEffectCaptureDelayValue = parser.value(debugEffectCaptureDelayOption).trimmed();
    const QString debugEffectProbeLayersValue = parser.value(debugEffectProbeLayersOption).trimmed();
    const QString debugEffectProbeHighRiskLayersValue = parser.value(debugEffectProbeHighRiskLayersOption).trimmed();
    const QString debugEffectProbeChannelMapSlotsValue = parser.value(debugEffectProbeChannelMapSlotsOption).trimmed();
    const QString debugEffectProbeMaxEffectsValue = parser.value(debugEffectProbeMaxEffectsOption).trimmed();
    const QString debugPuppetEffectFinalMeshValue =
        parser.value(debugPuppetEffectFinalMeshOption).trimmed().toLower();
    const QString debugPuppetAnimationLayerOverridesValue =
        parser.value(debugPuppetAnimationLayerOverridesOption).trimmed();
    const yakkai::harness::ScenePropertiesJsonOptionResult scenePropertiesJson =
        yakkai::harness::validateScenePropertiesJsonOption(parser.value(scenePropertiesJsonOption));
    const yakkai::harness::PuppetSimulationOptionResult puppetSimulation =
        yakkai::harness::validatePuppetSimulationOption(parser.value(puppetSimulationOption));
    const bool debugEffectCapturesRequested = !debugEffectCapturesPath.isEmpty();
    const bool debugEffectCaptureDelayRequested = parser.isSet(debugEffectCaptureDelayOption);
    const bool debugEffectProbeLayersRequested = !debugEffectProbeLayersValue.isEmpty();
    const bool debugEffectProbeHighRiskLayersRequested = !debugEffectProbeHighRiskLayersValue.isEmpty();
    const bool debugEffectProbeChannelMapSlotsRequested = !debugEffectProbeChannelMapSlotsValue.isEmpty();
    const bool debugEffectProbeMaxEffectsRequested = parser.isSet(debugEffectProbeMaxEffectsOption);
    const bool debugPuppetEffectFinalMeshRequested =
        parser.isSet(debugPuppetEffectFinalMeshOption);
    const bool debugPuppetEffectRouteOnlyRequested =
        parser.isSet(debugPuppetEffectRouteOnlyOption);
    const bool debugPuppetAnimationLayerOverridesRequested =
        !debugPuppetAnimationLayerOverridesValue.isEmpty();
    const QString debugEffectCapturesDir =
        debugEffectCapturesRequested ? QFileInfo(debugEffectCapturesPath).absoluteFilePath() : QString();
    const QString debugEffectCaptureCommand = app.arguments().join(QLatin1Char(' '));
    const std::optional<int> debugEffectCaptureDelayMs =
        debugEffectCaptureDelayRequested ? parseNonNegativeInt(debugEffectCaptureDelayValue) : std::optional<int>(0);
    const std::optional<QString> debugEffectProbeLayers =
        debugEffectProbeLayersRequested ? parsePositiveIdList(debugEffectProbeLayersValue) : std::optional<QString>(QString());
    const std::optional<QString> debugEffectProbeHighRiskLayers =
        debugEffectProbeHighRiskLayersRequested ? parsePositiveIdList(debugEffectProbeHighRiskLayersValue) : std::optional<QString>(QString());
    const std::optional<QString> debugEffectProbeChannelMapSlots =
        debugEffectProbeChannelMapSlotsRequested ? parseChannelMapSlotList(debugEffectProbeChannelMapSlotsValue) : std::optional<QString>(QString());
    const std::optional<int> debugEffectProbeMaxEffects =
        debugEffectProbeMaxEffectsRequested ? parseNonNegativeInt(debugEffectProbeMaxEffectsValue) : std::optional<int>(0);
    const QString normalizedDebugEffectProbeMaxEffects =
        debugEffectProbeMaxEffectsRequested && debugEffectProbeMaxEffects
            ? QString::number(*debugEffectProbeMaxEffects)
            : QString();
    const bool multiCaptureRequested = !captureDirPath.isEmpty() || !captureTimesValue.isEmpty() || !captureSequenceValue.isEmpty();
    std::optional<std::vector<CaptureRequest>> parsedCaptures;
    const std::optional<QSize> windowSize = parseWindowSize(windowSizeValue);

    if (!scenePropertiesJson.valid) {
        qWarning().noquote() << "yakkai_scene_harness:" << scenePropertiesJson.error;
        return 2;
    }
    if (!puppetSimulation.valid) {
        qWarning().noquote() << "yakkai_scene_harness:" << puppetSimulation.error;
        return 2;
    }
    if (debugEffectCapturesRequested && backend != QStringLiteral("paper")) {
        qWarning() << "yakkai_scene_harness: --debug-effect-captures requires --backend paper";
        return 2;
    }
    if (debugEffectCaptureDelayRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-capture-delay-ms requires --debug-effect-captures";
        return 2;
    }
    if (debugEffectProbeLayersRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-probe-layers requires --debug-effect-captures";
        return 2;
    }
    if (debugEffectProbeHighRiskLayersRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-probe-high-risk-layers requires --debug-effect-captures";
        return 2;
    }
    if (debugEffectProbeChannelMapSlotsRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-probe-channelmap-slots requires --debug-effect-captures";
        return 2;
    }
    if (debugEffectProbeChannelMapSlotsRequested && !debugEffectProbeLayersRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-probe-channelmap-slots requires --debug-effect-probe-layers";
        return 2;
    }
    if (debugEffectProbeChannelMapSlotsRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-probe-channelmap-slots is quarantined because the derived channelmap render path produces glitchy puppet fragments";
        return 2;
    }
    if (debugEffectProbeMaxEffectsRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-probe-max-effects requires --debug-effect-captures";
        return 2;
    }
    if (debugPuppetEffectFinalMeshRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-puppet-effect-final-mesh requires --debug-effect-captures";
        return 2;
    }
    if (debugPuppetEffectRouteOnlyRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-puppet-effect-route-only requires --debug-effect-captures";
        return 2;
    }
    if (debugPuppetAnimationLayerOverridesRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-puppet-animation-layer-overrides requires --debug-effect-captures";
        return 2;
    }
    if (debugEffectProbeMaxEffectsRequested &&
        !debugEffectProbeLayersRequested &&
        !debugEffectProbeHighRiskLayersRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-probe-max-effects requires --debug-effect-probe-layers or --debug-effect-probe-high-risk-layers";
        return 2;
    }
    if (debugPuppetEffectFinalMeshRequested &&
        !debugEffectProbeLayersRequested &&
        !debugEffectProbeHighRiskLayersRequested) {
        qWarning() << "yakkai_scene_harness: --debug-puppet-effect-final-mesh requires --debug-effect-probe-layers or --debug-effect-probe-high-risk-layers";
        return 2;
    }
    if (debugPuppetEffectRouteOnlyRequested &&
        !debugEffectProbeLayersRequested &&
        !debugEffectProbeHighRiskLayersRequested) {
        qWarning() << "yakkai_scene_harness: --debug-puppet-effect-route-only requires --debug-effect-probe-layers or --debug-effect-probe-high-risk-layers";
        return 2;
    }
    if (debugPuppetEffectFinalMeshRequested &&
        debugPuppetEffectFinalMeshValue != QStringLiteral("layer-card") &&
        debugPuppetEffectFinalMeshValue != QStringLiteral("image-space") &&
        debugPuppetEffectFinalMeshValue != QStringLiteral("deferred-puppet-final")) {
        qWarning() << "yakkai_scene_harness: invalid --debug-puppet-effect-final-mesh value" << debugPuppetEffectFinalMeshValue;
        return 2;
    }
    if (!debugEffectProbeLayers) {
        qWarning() << "yakkai_scene_harness: invalid --debug-effect-probe-layers value" << debugEffectProbeLayersValue;
        return 2;
    }
    if (!debugEffectProbeHighRiskLayers) {
        qWarning() << "yakkai_scene_harness: invalid --debug-effect-probe-high-risk-layers value" << debugEffectProbeHighRiskLayersValue;
        return 2;
    }
    if (!debugEffectProbeChannelMapSlots) {
        qWarning() << "yakkai_scene_harness: invalid --debug-effect-probe-channelmap-slots value" << debugEffectProbeChannelMapSlotsValue;
        return 2;
    }
    if (!debugEffectProbeMaxEffects) {
        qWarning() << "yakkai_scene_harness: invalid --debug-effect-probe-max-effects value" << debugEffectProbeMaxEffectsValue;
        return 2;
    }
    if (!debugEffectCaptureDelayMs) {
        qWarning() << "yakkai_scene_harness: invalid --debug-effect-capture-delay-ms value" << debugEffectCaptureDelayValue;
        return 2;
    }
    if (debugEffectCapturesRequested && !QDir().mkpath(debugEffectCapturesDir)) {
        qWarning() << "yakkai_scene_harness: failed to create debug effect capture directory" << debugEffectCapturesDir;
        return 5;
    }
    if (!windowSize) {
        qWarning() << "yakkai_scene_harness: invalid --window-size" << windowSizeValue;
        return 2;
    }
    if (!capturePath.isEmpty() && multiCaptureRequested) {
        qWarning() << "yakkai_scene_harness: --capture cannot be combined with --capture-dir, --capture-times-ms, or --capture-sequence";
        return 2;
    }
    if (multiCaptureRequested && captureDirPath.isEmpty()) {
        qWarning() << "yakkai_scene_harness: --capture-dir is required for multi-capture";
        return 2;
    }
    if (!captureTimesValue.isEmpty() && !captureSequenceValue.isEmpty()) {
        qWarning() << "yakkai_scene_harness: use either --capture-times-ms or --capture-sequence, not both";
        return 2;
    }
    if (!captureTimesValue.isEmpty()) {
        parsedCaptures = parseCaptureTimes(captureTimesValue);
    } else if (!captureSequenceValue.isEmpty()) {
        parsedCaptures = parseCaptureSequence(captureSequenceValue);
    }
    if (multiCaptureRequested && (!parsedCaptures || parsedCaptures->empty())) {
        qWarning() << "yakkai_scene_harness: invalid multi-capture schedule";
        return 2;
    }

    if (!capturePath.isEmpty() || multiCaptureRequested) {
        app.setQuitOnLastWindowClosed(false);
    }

    if (!puppetSimulation.normalized.isEmpty()) {
        qputenv("YAKKAI_PUPPET_SIMULATION", puppetSimulation.normalized.toUtf8());
    }

    QQmlApplicationEngine engine;
    QObject::connect(&app, &QGuiApplication::aboutToQuit, []() {
        qInfo() << "yakkai_scene_harness: aboutToQuit";
    });
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, []() {
        qInfo() << "yakkai_scene_harness: lastWindowClosed";
    });
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            qWarning().noquote() << "yakkai_scene_harness: qml warning:" << warning.toString();
        }
    });
    engine.addImportPath(buildQmlImportDir);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessBackend"), backend);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessSource"), QUrl::fromLocalFile(sourcePath));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessAssetsPath"), assetsPath);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessFillModeValue"), fillModeFromString(fillMode));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessWindowWidth"), windowSize->width());
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessWindowHeight"), windowSize->height());
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessMouseInput"), parser.isSet(mouseOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessMuted"), !parser.isSet(unmutedOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessShowInfoOverlay"), !parser.isSet(hideInfoOverlayOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessBackendQmlFile"), backendQmlFile(qmlDir, backend));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessQmlDir"), qmlDir);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectCapturesPath"), debugEffectCapturesDir);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectCaptureCommand"), debugEffectCaptureCommand);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectCaptureDelayMs"), *debugEffectCaptureDelayMs);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectProbeLayers"), *debugEffectProbeLayers);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectProbeHighRiskLayers"), *debugEffectProbeHighRiskLayers);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectProbeChannelMapSlots"), *debugEffectProbeChannelMapSlots);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectProbeMaxEffects"), normalizedDebugEffectProbeMaxEffects);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugPuppetEffectFinalMesh"), debugPuppetEffectFinalMeshValue);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugPuppetEffectRouteOnly"), debugPuppetEffectRouteOnlyRequested);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugPuppetAnimationLayerOverrides"), debugPuppetAnimationLayerOverridesValue);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessScenePropertiesJson"), scenePropertiesJson.normalized);

    const QUrl mainQml = QUrl::fromLocalFile(QDir(qmlDir).filePath(QStringLiteral("Main.qml")));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            qWarning() << "yakkai_scene_harness: objectCreationFailed";
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection
    );
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [](QObject* object, const QUrl& url) {
        qInfo() << "yakkai_scene_harness: objectCreated" << url << (object ? "ok" : "null");

        auto* window = qobject_cast<QWindow*>(object);
        if (!window) {
            return;
        }

        qInfo() << "yakkai_scene_harness: rootWindow initial visible=" << window->isVisible()
                << "size=" << window->size();
        QObject::connect(window, &QWindow::visibleChanged, window, [window](bool visible) {
            qInfo() << "yakkai_scene_harness: rootWindow visibleChanged" << visible;
        });
        QObject::connect(window, &QWindow::widthChanged, window, [window]() {
            qInfo() << "yakkai_scene_harness: rootWindow widthChanged" << window->width();
        });
        QObject::connect(window, &QWindow::heightChanged, window, [window]() {
            qInfo() << "yakkai_scene_harness: rootWindow heightChanged" << window->height();
        });
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [&app, capturePath, captureDelayMs, captureDirPath, captureTimesValue, captureSequenceValue, parsedCaptures](QObject* object, const QUrl&) {
        const bool multiCaptureRequested = !captureDirPath.isEmpty() || !captureTimesValue.isEmpty() || !captureSequenceValue.isEmpty();
        if (capturePath.isEmpty() && !multiCaptureRequested) {
            return;
        }

        auto* quickWindow = qobject_cast<QQuickWindow*>(object);
        if (!quickWindow) {
            qWarning() << "yakkai_scene_harness: capture requested but root object is not a QQuickWindow";
            QCoreApplication::exit(2);
            return;
        }

        QPointer<QQuickWindow> guardedWindow(quickWindow);

        if (!capturePath.isEmpty()) {
            const QString absoluteCapturePath = QFileInfo(capturePath).absoluteFilePath();
            startCaptureTimersAfterFirstFrame(&app, guardedWindow, [&app, guardedWindow, absoluteCapturePath, captureDelayMs]() {
                scheduleSingleCapture(&app, guardedWindow, absoluteCapturePath, captureDelayMs);
            });
            return;
        }

        if (!parsedCaptures || parsedCaptures->empty()) {
            qWarning() << "yakkai_scene_harness: invalid multi-capture schedule";
            QCoreApplication::exit(2);
            return;
        }

        const QString absoluteCaptureDir = QFileInfo(captureDirPath).absoluteFilePath();
        if (!QDir().mkpath(absoluteCaptureDir)) {
            qWarning() << "yakkai_scene_harness: failed to create capture directory" << absoluteCaptureDir;
            QCoreApplication::exit(5);
            return;
        }
        startCaptureTimersAfterFirstFrame(&app, guardedWindow, [&app, guardedWindow, parsedCaptures, absoluteCaptureDir]() {
            scheduleMultiCaptures(&app, guardedWindow, *parsedCaptures, absoluteCaptureDir);
        });
    });
    engine.load(mainQml);

    QTimer::singleShot(0, &app, [&engine]() {
        qInfo() << "yakkai_scene_harness: rootObjects after load" << engine.rootObjects().size();
    });

    const int appStatus = app.exec();
    if (appStatus != 0 || !debugEffectCapturesRequested) {
        return appStatus;
    }

    const QString manifestPath = QDir(debugEffectCapturesDir).filePath(QStringLiteral("manifest.json"));
    if (!waitForDebugEffectManifestOk(manifestPath)) {
        return 6;
    }

    return 0;
}
