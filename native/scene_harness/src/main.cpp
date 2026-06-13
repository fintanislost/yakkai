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
#include <QtCore/QProcess>
#include <QtCore/QSize>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQuick/QQuickWindow>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

#include "CaptureGate.hpp"
#include "InteractiveMouseOption.hpp"
#include "LayerVisibilityOverrideOption.hpp"
#include "MousePositionOption.hpp"
#include "MouseTimelineOption.hpp"
#include "PuppetSimulationOption.hpp"
#include "ScenePropertiesOption.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
constexpr int DebugEffectCaptureReadyTimeoutMs = 180000;
constexpr int DebugEffectManifestWaitMs = 30000;
constexpr int DebugEffectManifestPollMs = 50;

struct CaptureRequest
{
    int timeMs = 0;
    QString fileName;
};

struct RecordingRequest
{
    QString outputPath;
    int durationMs = 10000;
    int fps = 30;
    int startDelayMs = 0;
};

enum class CaptureExitMode {
    Graceful,
    Immediate,
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

std::optional<int> parsePositiveInt(const QString& value)
{
    const std::optional<int> parsed = parseNonNegativeInt(value);
    if (!parsed || *parsed == 0) {
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

QStringList rawVideoEncoderArguments(const QString& outputPath, const QSize& frameSize, int fps)
{
    return QStringList{
        QStringLiteral("-y"),
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-f"),
        QStringLiteral("rawvideo"),
        QStringLiteral("-pix_fmt"),
        QStringLiteral("rgba"),
        QStringLiteral("-s"),
        QStringLiteral("%1x%2").arg(frameSize.width()).arg(frameSize.height()),
        QStringLiteral("-r"),
        QString::number(fps),
        QStringLiteral("-i"),
        QStringLiteral("pipe:0"),
        QStringLiteral("-an"),
        QStringLiteral("-c:v"),
        QStringLiteral("libx264"),
        QStringLiteral("-preset"),
        QStringLiteral("veryfast"),
        QStringLiteral("-tune"),
        QStringLiteral("zerolatency"),
        QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv420p"),
        outputPath,
    };
}

bool writeRgbaFrame(QProcess* process, const QImage& frame)
{
    const QImage rgba = frame.format() == QImage::Format_RGBA8888
        ? frame
        : frame.convertToFormat(QImage::Format_RGBA8888);
    const qsizetype expectedLineBytes = static_cast<qsizetype>(rgba.width()) * 4;
    QByteArray frameBytes;
    frameBytes.reserve(static_cast<qsizetype>(rgba.height()) * expectedLineBytes);
    for (int y = 0; y < rgba.height(); ++y) {
        frameBytes.append(reinterpret_cast<const char*>(rgba.constScanLine(y)), expectedLineBytes);
    }

    const qint64 written = process->write(frameBytes);
    if (written != frameBytes.size()) {
        qWarning() << "yakkai_scene_harness: failed to write complete raw video frame"
                   << "written=" << written
                   << "expected=" << frameBytes.size();
        return false;
    }
    if (!process->waitForBytesWritten(5000)) {
        qWarning() << "yakkai_scene_harness: timed out writing raw video frame to ffmpeg";
        return false;
    }
    return true;
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

void requestCaptureExit(QQuickWindow* window, int status, CaptureExitMode exitMode)
{
    if (exitMode == CaptureExitMode::Immediate) {
        qInfo() << "yakkai_scene_harness: immediate capture exit requested status=" << status;
        std::fprintf(stderr, "yakkai_scene_harness: immediate capture exit requested status=%d\n", status);
        std::fflush(stderr);
        std::_Exit(status);
    }

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
                           int captureDelayMs,
                           CaptureExitMode exitMode)
{
    QTimer::singleShot(std::max(captureDelayMs, 0), app, [guardedWindow, absoluteCapturePath, exitMode]() {
        if (!guardedWindow) {
            qWarning() << "yakkai_scene_harness: capture window was destroyed before capture";
            QCoreApplication::exit(3);
            return;
        }
        requestCaptureExit(guardedWindow, saveWindowCapture(guardedWindow, absoluteCapturePath), exitMode);
    });
}

void scheduleMultiCaptures(QCoreApplication* app,
                           QPointer<QQuickWindow> guardedWindow,
                           const std::vector<CaptureRequest>& captures,
                           const QString& absoluteCaptureDir,
                           CaptureExitMode exitMode)
{
    auto remaining = std::make_shared<int>(static_cast<int>(captures.size()));
    auto failed = std::make_shared<bool>(false);

    for (const CaptureRequest& capture : captures) {
        const QString absolutePath = QDir(absoluteCaptureDir).filePath(capture.fileName);
        QTimer::singleShot(capture.timeMs, app, [guardedWindow, absolutePath, remaining, failed, exitMode]() {
            if (!guardedWindow) {
                qWarning() << "yakkai_scene_harness: capture window was destroyed before capture";
                *failed = true;
            } else if (saveWindowCapture(guardedWindow, absolutePath) != CaptureSuccess) {
                *failed = true;
            }

            *remaining -= 1;
            if (*remaining == 0) {
                requestCaptureExit(guardedWindow, *failed ? 5 : 0, exitMode);
            }
        });
    }
}

void finishWindowRecording(QPointer<QQuickWindow> guardedWindow,
                           const std::shared_ptr<QProcess>& encoder,
                           int status,
                           int frameCount,
                           const QElapsedTimer& elapsed,
                           CaptureExitMode exitMode)
{
    int finalStatus = status;
    if (encoder->state() != QProcess::NotRunning) {
        encoder->closeWriteChannel();
        if (!encoder->waitForFinished(15000)) {
            qWarning() << "yakkai_scene_harness: timed out waiting for ffmpeg to finalize live recording";
            encoder->terminate();
            if (!encoder->waitForFinished(5000)) {
                encoder->kill();
                encoder->waitForFinished(5000);
            }
            finalStatus = 5;
        }
    }

    if (encoder->exitStatus() != QProcess::NormalExit || encoder->exitCode() != 0) {
        qWarning() << "yakkai_scene_harness: ffmpeg recording process failed"
                   << "exitStatus=" << encoder->exitStatus()
                   << "exitCode=" << encoder->exitCode()
                   << "stderr=" << QString::fromLocal8Bit(encoder->readAllStandardError());
        finalStatus = 5;
    }

    qInfo() << "yakkai_scene_harness: live recording finished"
            << "frames=" << frameCount
            << "elapsedMs=" << elapsed.elapsed()
            << "status=" << finalStatus;
    requestCaptureExit(guardedWindow, finalStatus, exitMode);
}

void scheduleWindowRecording(QCoreApplication* app,
                             QPointer<QQuickWindow> guardedWindow,
                             const RecordingRequest& request,
                             CaptureExitMode exitMode)
{
    QTimer::singleShot(std::max(request.startDelayMs, 0), app, [app, guardedWindow, request, exitMode]() {
        if (!guardedWindow) {
            qWarning() << "yakkai_scene_harness: recording window was destroyed before first frame";
            QCoreApplication::exit(3);
            return;
        }

        QImage firstFrame = guardedWindow->grabWindow();
        if (firstFrame.isNull()) {
            qWarning() << "yakkai_scene_harness: recording first frame is null";
            QCoreApplication::exit(CaptureNullImage);
            return;
        }
        firstFrame = firstFrame.convertToFormat(QImage::Format_RGBA8888);
        const QSize frameSize = firstFrame.size();
        const QFileInfo outputInfo(request.outputPath);
        const QString absoluteOutputPath = outputInfo.absoluteFilePath();
        if (!QDir().mkpath(outputInfo.absolutePath())) {
            qWarning() << "yakkai_scene_harness: failed to create recording output directory" << outputInfo.absolutePath();
            QCoreApplication::exit(5);
            return;
        }

        auto encoder = std::make_shared<QProcess>();
        encoder->setProgram(QStringLiteral("ffmpeg"));
        encoder->setArguments(rawVideoEncoderArguments(absoluteOutputPath, frameSize, request.fps));
        encoder->setProcessChannelMode(QProcess::SeparateChannels);
        qInfo() << "yakkai_scene_harness: starting live recording"
                << "path=" << absoluteOutputPath
                << "size=" << frameSize
                << "fps=" << request.fps
                << "durationMs=" << request.durationMs
                << "startDelayMs=" << request.startDelayMs;
        encoder->start();
        if (!encoder->waitForStarted(5000)) {
            qWarning() << "yakkai_scene_harness: failed to start ffmpeg for live recording"
                       << "error=" << encoder->errorString();
            QCoreApplication::exit(5);
            return;
        }

        const int targetFrameCount = std::max(1, static_cast<int>(std::ceil(static_cast<double>(request.durationMs) * request.fps / 1000.0)));
        const int timerIntervalMs = std::max(1, static_cast<int>(std::round(1000.0 / request.fps)));
        auto frameCount = std::make_shared<int>(0);
        auto failed = std::make_shared<bool>(false);
        auto elapsed = std::make_shared<QElapsedTimer>();
        elapsed->start();
        auto timer = std::make_shared<QTimer>();
        timer->setInterval(timerIntervalMs);

        auto writeFrame = [guardedWindow, encoder, frameSize, frameCount, failed]() {
            if (!guardedWindow) {
                qWarning() << "yakkai_scene_harness: recording window was destroyed during capture";
                *failed = true;
                return;
            }
            QImage frame = guardedWindow->grabWindow();
            if (frame.isNull()) {
                qWarning() << "yakkai_scene_harness: recording frame is null";
                *failed = true;
                return;
            }
            frame = frame.convertToFormat(QImage::Format_RGBA8888);
            if (frame.size() != frameSize) {
                qWarning() << "yakkai_scene_harness: recording frame size changed"
                           << "actual=" << frame.size()
                           << "expected=" << frameSize;
                *failed = true;
                return;
            }
            if (!writeRgbaFrame(encoder.get(), frame)) {
                *failed = true;
                return;
            }
            *frameCount += 1;
        };

        writeFrame();
        QObject::connect(timer.get(), &QTimer::timeout, app, [timer, writeFrame, frameCount, failed, targetFrameCount, elapsed, guardedWindow, encoder, exitMode]() {
            if (*failed || *frameCount >= targetFrameCount) {
                timer->stop();
                finishWindowRecording(guardedWindow, encoder, *failed ? 5 : 0, *frameCount, *elapsed, exitMode);
                return;
            }
            writeFrame();
            if (*failed || *frameCount >= targetFrameCount) {
                timer->stop();
                finishWindowRecording(guardedWindow, encoder, *failed ? 5 : 0, *frameCount, *elapsed, exitMode);
            }
        });
        timer->start();
    });
}

void startCaptureTimersAfterFirstFrame(QCoreApplication* app,
                                       QPointer<QQuickWindow> guardedWindow,
                                       int readyTimeoutMs,
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

        qInfo() << "yakkai_scene_harness: backend readiness signaled; arming capture timers";
        readyPollTimer->stop();
        readyTimeoutTimer->stop();
        readyPollTimer->deleteLater();
        readyTimeoutTimer->deleteLater();
        startCaptureTimers();
    };

    QObject::connect(readyPollTimer, &QTimer::timeout, app, [guardedWindow, gate, tryStart]() {
        if (!guardedWindow) {
            qWarning() << "yakkai_scene_harness: capture window was destroyed before backend readiness";
            QCoreApplication::exit(3);
            return;
        }

        if (guardedWindow->property("captureReady").toBool()) {
            gate->markFirstFrameReady();
            (*tryStart)();
        }
    });
    QObject::connect(readyTimeoutTimer, &QTimer::timeout, app, [guardedWindow, readyTimeoutMs]() {
        const QString status = guardedWindow
            ? guardedWindow->property("backendStatus").toString()
            : QStringLiteral("<window destroyed>");
        qWarning() << "yakkai_scene_harness: timed out waiting for backend readiness before capture"
                   << "timeoutMs=" << readyTimeoutMs
                   << "backendStatus=" << status;
        QCoreApplication::exit(7);
    });

    qInfo() << "yakkai_scene_harness: waiting for backend readiness before capture"
            << "timeoutMs=" << readyTimeoutMs;
    if (guardedWindow && guardedWindow->property("captureReady").toBool()) {
        gate->markFirstFrameReady();
        (*tryStart)();
        return;
    }

    readyPollTimer->start();
    readyTimeoutTimer->start(readyTimeoutMs);
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

std::optional<CaptureExitMode> captureExitModeFromString(const QString& value)
{
    if (value == QStringLiteral("graceful")) {
        return CaptureExitMode::Graceful;
    }
    if (value == QStringLiteral("immediate")) {
        return CaptureExitMode::Immediate;
    }
    return std::nullopt;
}

QString backendQmlFile(const QString& qmlDir, const QString& backend)
{
    if (backend == QStringLiteral("paper")) {
        return QDir(qmlDir).filePath(QStringLiteral("YakkaiSceneViewerHarness.qml"));
    }
    if (backend == QStringLiteral("video")) {
        return QDir(qmlDir).filePath(QStringLiteral("VideoWallpaperHarness.qml"));
    }
    if (backend == QStringLiteral("web")) {
        return QDir(qmlDir).filePath(QStringLiteral("WebWallpaperHarness.qml"));
    }

    return QDir(qmlDir).filePath(QStringLiteral("SystemSceneViewerHarness.qml"));
}

QString wallpaperPackageUiDir(const QString& qmlDir)
{
    return QDir(qmlDir).absoluteFilePath(QStringLiteral("../../../wallpapers/io.team7.yakkai/contents/ui"));
}
}

int main(int argc, char* argv[])
{
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("yakkai_scene_harness"));
    QCoreApplication::setOrganizationName(QStringLiteral("Team7"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Standalone Yakkai scene harness"));
    parser.addHelpOption();

    QCommandLineOption backendOption(
        QStringList{QStringLiteral("backend")},
        QStringLiteral("Select the backend to load: system, paper, video, or web."),
        QStringLiteral("backend"),
        QStringLiteral("system")
    );
    QCommandLineOption sourceOption(
        QStringList{QStringLiteral("source")},
        QStringLiteral("Absolute path to scene.json, scene.pkg, video file, or web index.html."),
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
    QCommandLineOption interactiveMouseOption(
        QStringList{QStringLiteral("interactive-mouse")},
        QStringLiteral("Harness-only live pointer input for manual mouse/parallax checks. Cannot be combined with synthetic debug mouse options.")
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
    QCommandLineOption captureExitModeOption(
        QStringList{QStringLiteral("capture-exit-mode")},
        QStringLiteral("Harness-only capture exit mode: graceful|immediate. Immediate exits after the final capture without Qt teardown."),
        QStringLiteral("mode"),
        QStringLiteral("graceful")
    );
    QCommandLineOption recordOption(
        QStringList{QStringLiteral("record")},
        QStringLiteral("Record a live MP4 by piping repeated window grabs to ffmpeg, then exit."),
        QStringLiteral("path")
    );
    QCommandLineOption recordDurationOption(
        QStringList{QStringLiteral("record-duration-ms")},
        QStringLiteral("Live recording duration in milliseconds."),
        QStringLiteral("ms"),
        QStringLiteral("10000")
    );
    QCommandLineOption recordFpsOption(
        QStringList{QStringLiteral("record-fps")},
        QStringLiteral("Live recording frame rate."),
        QStringLiteral("fps"),
        QStringLiteral("30")
    );
    QCommandLineOption recordStartDelayOption(
        QStringList{QStringLiteral("record-start-delay-ms")},
        QStringLiteral("Delay after backend readiness before live recording starts."),
        QStringLiteral("ms"),
        QStringLiteral("0")
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
    QCommandLineOption debugEffectCaptureLayersOption(
        QStringList{QStringLiteral("debug-effect-capture-layers")},
        QStringLiteral("Only register and dump normal debug effect captures for the listed layer IDs. Requires --debug-effect-captures."),
        QStringLiteral("ids")
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
    QCommandLineOption debugLayerVisibilityOverridesOption(
        QStringList{QStringLiteral("debug-layer-visibility-overrides")},
        QStringLiteral("Comma-separated harness-only layer visibility overrides: layerId:true|false. Requires --debug-effect-captures."),
        QStringLiteral("rules")
    );
    QCommandLineOption debugMousePositionOption(
        QStringList{QStringLiteral("debug-mouse-position")},
        QStringLiteral("Harness-only synthetic normalized mouse position as x,y. Requires --debug-effect-captures."),
        QStringLiteral("x,y")
    );
    QCommandLineOption debugMouseTimelineOption(
        QStringList{QStringLiteral("debug-mouse-timeline")},
        QStringLiteral("Harness-only synthetic mouse timeline as timeMs:x,y;timeMs:x,y. Requires --debug-effect-captures."),
        QStringLiteral("timeline")
    );
    QCommandLineOption scenePropertiesJsonOption(
        QStringList{QStringLiteral("scene-properties-json")},
        QStringLiteral("Wallpaper Engine properties JSON object forwarded to backends that support it."),
        QStringLiteral("json")
    );
    QCommandLineOption puppetSimulationOption(
        QStringList{QStringLiteral("puppet-simulation")},
        QStringLiteral("Harness-only puppet simulation mode: off, diagnostic, or runtime."),
        QStringLiteral("mode")
    );
    QCommandLineOption debugSyntheticAudioOption(
        QStringList{QStringLiteral("debug-synthetic-audio")},
        QStringLiteral("Harness-only: emit synthetic Wallpaper Engine web audio FFT data. Only affects --backend web.")
    );
    QCommandLineOption debugSyntheticAudioBinsOption(
        QStringList{QStringLiteral("debug-synthetic-audio-bins")},
        QStringLiteral("Synthetic web audio bin count."),
        QStringLiteral("count"),
        QStringLiteral("128")
    );
    QCommandLineOption debugSyntheticAudioIntervalOption(
        QStringList{QStringLiteral("debug-synthetic-audio-interval-ms")},
        QStringLiteral("Synthetic web audio interval in milliseconds."),
        QStringLiteral("ms"),
        QStringLiteral("33")
    );

    parser.addOption(backendOption);
    parser.addOption(sourceOption);
    parser.addOption(assetsOption);
    parser.addOption(fillOption);
    parser.addOption(windowSizeOption);
    parser.addOption(mouseOption);
    parser.addOption(interactiveMouseOption);
    parser.addOption(unmutedOption);
    parser.addOption(hideInfoOverlayOption);
    parser.addOption(captureOption);
    parser.addOption(captureDelayOption);
    parser.addOption(captureDirOption);
    parser.addOption(captureTimesOption);
    parser.addOption(captureSequenceOption);
    parser.addOption(captureExitModeOption);
    parser.addOption(recordOption);
    parser.addOption(recordDurationOption);
    parser.addOption(recordFpsOption);
    parser.addOption(recordStartDelayOption);
    parser.addOption(debugEffectCapturesOption);
    parser.addOption(debugEffectCaptureDelayOption);
    parser.addOption(debugEffectCaptureLayersOption);
    parser.addOption(debugEffectProbeLayersOption);
    parser.addOption(debugEffectProbeHighRiskLayersOption);
    parser.addOption(debugEffectProbeChannelMapSlotsOption);
    parser.addOption(debugEffectProbeMaxEffectsOption);
    parser.addOption(debugPuppetEffectFinalMeshOption);
    parser.addOption(debugPuppetEffectRouteOnlyOption);
    parser.addOption(debugPuppetAnimationLayerOverridesOption);
    parser.addOption(debugLayerVisibilityOverridesOption);
    parser.addOption(debugMousePositionOption);
    parser.addOption(debugMouseTimelineOption);
    parser.addOption(scenePropertiesJsonOption);
    parser.addOption(puppetSimulationOption);
    parser.addOption(debugSyntheticAudioOption);
    parser.addOption(debugSyntheticAudioBinsOption);
    parser.addOption(debugSyntheticAudioIntervalOption);
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
    const QString captureExitModeValue = parser.value(captureExitModeOption).trimmed();
    const QString recordPath = parser.value(recordOption).trimmed();
    const QString recordDurationValue = parser.value(recordDurationOption).trimmed();
    const QString recordFpsValue = parser.value(recordFpsOption).trimmed();
    const QString recordStartDelayValue = parser.value(recordStartDelayOption).trimmed();
    const QString debugEffectCapturesPath = parser.value(debugEffectCapturesOption).trimmed();
    const QString debugEffectCaptureDelayValue = parser.value(debugEffectCaptureDelayOption).trimmed();
    const QString debugEffectCaptureLayersValue = parser.value(debugEffectCaptureLayersOption).trimmed();
    const QString debugEffectProbeLayersValue = parser.value(debugEffectProbeLayersOption).trimmed();
    const QString debugEffectProbeHighRiskLayersValue = parser.value(debugEffectProbeHighRiskLayersOption).trimmed();
    const QString debugEffectProbeChannelMapSlotsValue = parser.value(debugEffectProbeChannelMapSlotsOption).trimmed();
    const QString debugEffectProbeMaxEffectsValue = parser.value(debugEffectProbeMaxEffectsOption).trimmed();
    const QString debugPuppetEffectFinalMeshValue =
        parser.value(debugPuppetEffectFinalMeshOption).trimmed().toLower();
    const QString debugPuppetAnimationLayerOverridesValue =
        parser.value(debugPuppetAnimationLayerOverridesOption).trimmed();
    const yakkai::harness::LayerVisibilityOverrideOptionResult debugLayerVisibilityOverrides =
        yakkai::harness::validateLayerVisibilityOverrideOption(parser.value(debugLayerVisibilityOverridesOption));
    const yakkai::harness::MousePositionOptionResult debugMousePosition =
        yakkai::harness::validateMousePositionOption(parser.value(debugMousePositionOption));
    const yakkai::harness::MouseTimelineOptionResult debugMouseTimeline =
        yakkai::harness::validateMouseTimelineOption(parser.value(debugMouseTimelineOption));
    const yakkai::harness::ScenePropertiesJsonOptionResult scenePropertiesJson =
        yakkai::harness::validateScenePropertiesJsonOption(parser.value(scenePropertiesJsonOption));
    const yakkai::harness::PuppetSimulationOptionResult puppetSimulation =
        yakkai::harness::validatePuppetSimulationOption(parser.value(puppetSimulationOption));
    const QString debugSyntheticAudioBinsValue = parser.value(debugSyntheticAudioBinsOption).trimmed();
    const QString debugSyntheticAudioIntervalValue = parser.value(debugSyntheticAudioIntervalOption).trimmed();
    const bool debugEffectCapturesRequested = !debugEffectCapturesPath.isEmpty();
    const bool debugEffectCaptureDelayRequested = parser.isSet(debugEffectCaptureDelayOption);
    const bool debugEffectCaptureLayersRequested = !debugEffectCaptureLayersValue.isEmpty();
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
    const bool debugLayerVisibilityOverridesRequested =
        !debugLayerVisibilityOverrides.normalized.isEmpty();
    const bool debugMousePositionRequested = debugMousePosition.hasPosition;
    const bool debugMouseTimelineRequested = debugMouseTimeline.hasTimeline;
    const bool interactiveMouseRequested = parser.isSet(interactiveMouseOption);
    const bool mouseInputRequested = parser.isSet(mouseOption) || interactiveMouseRequested;
    const yakkai::harness::InteractiveMouseOptionResult interactiveMouse =
        yakkai::harness::validateInteractiveMouseOption(interactiveMouseRequested,
                                                        debugMousePositionRequested,
                                                        debugMouseTimelineRequested);
    const QString debugEffectCapturesDir =
        debugEffectCapturesRequested ? QFileInfo(debugEffectCapturesPath).absoluteFilePath() : QString();
    const QString debugEffectCaptureCommand = app.arguments().join(QLatin1Char(' '));
    const std::optional<int> debugEffectCaptureDelayMs =
        debugEffectCaptureDelayRequested ? parseNonNegativeInt(debugEffectCaptureDelayValue) : std::optional<int>(0);
    const std::optional<QString> debugEffectCaptureLayers =
        debugEffectCaptureLayersRequested ? parsePositiveIdList(debugEffectCaptureLayersValue) : std::optional<QString>(QString());
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
    const std::optional<CaptureExitMode> captureExitMode = captureExitModeFromString(captureExitModeValue);
    const std::optional<int> recordDurationMs = parsePositiveInt(recordDurationValue);
    const std::optional<int> recordFps = parsePositiveInt(recordFpsValue);
    const std::optional<int> recordStartDelayMs = parseNonNegativeInt(recordStartDelayValue);
    const std::optional<int> debugSyntheticAudioBins = parseNonNegativeInt(debugSyntheticAudioBinsValue);
    const std::optional<int> debugSyntheticAudioIntervalMs = parseNonNegativeInt(debugSyntheticAudioIntervalValue);
    const bool multiCaptureRequested = !captureDirPath.isEmpty() || !captureTimesValue.isEmpty() || !captureSequenceValue.isEmpty();
    const bool recordRequested = !recordPath.isEmpty();
    std::optional<std::vector<CaptureRequest>> parsedCaptures;
    const std::optional<QSize> windowSize = parseWindowSize(windowSizeValue);

    if (!scenePropertiesJson.valid) {
        qWarning().noquote() << "yakkai_scene_harness:" << scenePropertiesJson.error;
        return 2;
    }
    if (!debugLayerVisibilityOverrides.valid) {
        qWarning().noquote() << "yakkai_scene_harness:" << debugLayerVisibilityOverrides.error;
        return 2;
    }
    if (!debugMousePosition.valid) {
        qWarning().noquote() << "yakkai_scene_harness:" << debugMousePosition.error;
        return 2;
    }
    if (!debugMouseTimeline.valid) {
        qWarning().noquote() << "yakkai_scene_harness:" << debugMouseTimeline.error;
        return 2;
    }
    if (!interactiveMouse.valid) {
        qWarning().noquote() << "yakkai_scene_harness:" << interactiveMouse.error;
        return 2;
    }
    if (!puppetSimulation.valid) {
        qWarning().noquote() << "yakkai_scene_harness:" << puppetSimulation.error;
        return 2;
    }
    if (!captureExitMode) {
        const QByteArray encodedCaptureExitMode = captureExitModeValue.toLocal8Bit();
        std::fprintf(stderr, "yakkai_scene_harness: invalid --capture-exit-mode %s\n", encodedCaptureExitMode.constData());
        qWarning() << "yakkai_scene_harness: invalid --capture-exit-mode" << captureExitModeValue;
        return 2;
    }
    if (!recordDurationMs) {
        qWarning() << "yakkai_scene_harness: invalid --record-duration-ms value" << recordDurationValue;
        return 2;
    }
    if (!recordFps) {
        qWarning() << "yakkai_scene_harness: invalid --record-fps value" << recordFpsValue;
        return 2;
    }
    if (!recordStartDelayMs) {
        qWarning() << "yakkai_scene_harness: invalid --record-start-delay-ms value" << recordStartDelayValue;
        return 2;
    }
    if (!debugSyntheticAudioBins || *debugSyntheticAudioBins == 0) {
        qWarning() << "yakkai_scene_harness: invalid --debug-synthetic-audio-bins value" << debugSyntheticAudioBinsValue;
        return 2;
    }
    if (!debugSyntheticAudioIntervalMs || *debugSyntheticAudioIntervalMs == 0) {
        qWarning() << "yakkai_scene_harness: invalid --debug-synthetic-audio-interval-ms value" << debugSyntheticAudioIntervalValue;
        return 2;
    }
    if (*captureExitMode == CaptureExitMode::Immediate && debugEffectCapturesRequested) {
        std::fprintf(stderr, "yakkai_scene_harness: --capture-exit-mode immediate cannot be combined with --debug-effect-captures\n");
        qWarning() << "yakkai_scene_harness: --capture-exit-mode immediate cannot be combined with --debug-effect-captures";
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
    if (debugEffectCaptureLayersRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-capture-layers requires --debug-effect-captures";
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
    if (debugLayerVisibilityOverridesRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-layer-visibility-overrides requires --debug-effect-captures";
        return 2;
    }
    if (debugMousePositionRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-mouse-position requires --debug-effect-captures";
        return 2;
    }
    if (debugMouseTimelineRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-mouse-timeline requires --debug-effect-captures";
        return 2;
    }
    if (debugMouseTimelineRequested && debugMousePositionRequested) {
        qWarning() << "yakkai_scene_harness: --debug-mouse-timeline cannot be combined with --debug-mouse-position";
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
    if (!debugEffectCaptureLayers) {
        qWarning() << "yakkai_scene_harness: invalid --debug-effect-capture-layers value" << debugEffectCaptureLayersValue;
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
    if (recordRequested && (!capturePath.isEmpty() || multiCaptureRequested)) {
        qWarning() << "yakkai_scene_harness: --record cannot be combined with --capture, --capture-dir, --capture-times-ms, or --capture-sequence";
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
    const CaptureExitMode selectedCaptureExitMode = *captureExitMode;

    if (!capturePath.isEmpty() || multiCaptureRequested || recordRequested) {
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
    engine.addImportPath(wallpaperPackageUiDir(qmlDir));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessBackend"), backend);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessSource"), QUrl::fromLocalFile(sourcePath));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessAssetsPath"), assetsPath);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessFillModeValue"), fillModeFromString(fillMode));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessWindowWidth"), windowSize->width());
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessWindowHeight"), windowSize->height());
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessMouseInput"), mouseInputRequested);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessMuted"), !parser.isSet(unmutedOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessShowInfoOverlay"), !parser.isSet(hideInfoOverlayOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessBackendQmlFile"), backendQmlFile(qmlDir, backend));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessQmlDir"), qmlDir);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectCapturesPath"), debugEffectCapturesDir);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectCaptureCommand"), debugEffectCaptureCommand);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectCaptureDelayMs"), *debugEffectCaptureDelayMs);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectCaptureLayers"), *debugEffectCaptureLayers);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectProbeLayers"), *debugEffectProbeLayers);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectProbeHighRiskLayers"), *debugEffectProbeHighRiskLayers);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectProbeChannelMapSlots"), *debugEffectProbeChannelMapSlots);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectProbeMaxEffects"), normalizedDebugEffectProbeMaxEffects);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugPuppetEffectFinalMesh"), debugPuppetEffectFinalMeshValue);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugPuppetEffectRouteOnly"), debugPuppetEffectRouteOnlyRequested);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugPuppetAnimationLayerOverrides"), debugPuppetAnimationLayerOverridesValue);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugLayerVisibilityOverrides"), debugLayerVisibilityOverrides.normalized);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugMousePosition"), debugMousePosition.normalized);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugMouseTimeline"), debugMouseTimeline.normalized);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugInteractiveMouse"), interactiveMouseRequested);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugSyntheticAudioEnabled"), parser.isSet(debugSyntheticAudioOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugSyntheticAudioBins"), *debugSyntheticAudioBins);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugSyntheticAudioIntervalMs"), *debugSyntheticAudioIntervalMs);
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
    const RecordingRequest recordingRequest{
        recordRequested ? QFileInfo(recordPath).absoluteFilePath() : QString(),
        *recordDurationMs,
        *recordFps,
        *recordStartDelayMs,
    };

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [&app, capturePath, captureDelayMs, captureDirPath, captureTimesValue, captureSequenceValue, parsedCaptures, selectedCaptureExitMode, recordRequested, recordingRequest, debugEffectCapturesRequested](QObject* object, const QUrl&) {
        const bool multiCaptureRequested = !captureDirPath.isEmpty() || !captureTimesValue.isEmpty() || !captureSequenceValue.isEmpty();
        if (capturePath.isEmpty() && !multiCaptureRequested && !recordRequested) {
            return;
        }

        auto* quickWindow = qobject_cast<QQuickWindow*>(object);
        if (!quickWindow) {
            qWarning() << "yakkai_scene_harness: capture or recording requested but root object is not a QQuickWindow";
            QCoreApplication::exit(2);
            return;
        }

        QPointer<QQuickWindow> guardedWindow(quickWindow);

        if (recordRequested) {
            startCaptureTimersAfterFirstFrame(&app, guardedWindow, CaptureReadyTimeoutMs, [&app, guardedWindow, recordingRequest, selectedCaptureExitMode]() {
                scheduleWindowRecording(&app, guardedWindow, recordingRequest, selectedCaptureExitMode);
            });
            return;
        }

        if (!capturePath.isEmpty()) {
            const QString absoluteCapturePath = QFileInfo(capturePath).absoluteFilePath();
            const int readyTimeoutMs = debugEffectCapturesRequested
                ? DebugEffectCaptureReadyTimeoutMs
                : CaptureReadyTimeoutMs;
            startCaptureTimersAfterFirstFrame(&app, guardedWindow, readyTimeoutMs, [&app, guardedWindow, absoluteCapturePath, captureDelayMs, selectedCaptureExitMode]() {
                scheduleSingleCapture(&app, guardedWindow, absoluteCapturePath, captureDelayMs, selectedCaptureExitMode);
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
        startCaptureTimersAfterFirstFrame(&app, guardedWindow, CaptureReadyTimeoutMs, [&app, guardedWindow, parsedCaptures, absoluteCaptureDir, selectedCaptureExitMode]() {
            scheduleMultiCaptures(&app, guardedWindow, *parsedCaptures, absoluteCaptureDir, selectedCaptureExitMode);
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
