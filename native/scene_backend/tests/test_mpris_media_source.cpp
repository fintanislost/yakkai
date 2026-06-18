#include "YakkaiMprisMediaSource.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QVariantMap>
#include <QtTest/QSignalSpy>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

QJsonObject mediaFromPayload(const QString& payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    check(document.isObject(), "payload is a JSON object");
    return document.object().value(QStringLiteral("__yakkaiMedia")).toObject();
}

QVariantMap metadata()
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("xesam:title"), QStringLiteral("Instalock"));
    metadata.insert(QStringLiteral("xesam:artist"), QStringList{QStringLiteral("WYLTK")});
    metadata.insert(QStringLiteral("xesam:album"), QStringLiteral("Back to Inova"));
    metadata.insert(QStringLiteral("mpris:length"), 240000000LL);
    return metadata;
}

yakkai::mpris::PlayerState playerState(qint64 positionUsec)
{
    return {
        .service = QStringLiteral("org.mpris.MediaPlayer2.test"),
        .playbackStatus = QStringLiteral("Playing"),
        .metadata = metadata(),
        .positionUsec = positionUsec,
    };
}

void testPositionOnlyUpdatesRuntimePayloadWithoutStableMediaSignal()
{
    YakkaiMprisMediaSource source;
    QSignalSpy stableSpy(&source, &YakkaiMprisMediaSource::mediaChanged);
    QSignalSpy runtimeSpy(&source, &YakkaiMprisMediaSource::runtimeMediaChanged);

    source.publishStateForTest(playerState(42000000LL));

    check(stableSpy.count() == 1, "initial media state emits stable mediaChanged");
    check(runtimeSpy.count() == 1, "initial media state emits runtimeMediaChanged");
    check(mediaFromPayload(source.mediaJson()).value(QStringLiteral("position")).toDouble() == 42.0,
          "stable mediaJson includes initial position snapshot");
    check(mediaFromPayload(source.runtimeMediaJson()).value(QStringLiteral("position")).toDouble() == 42.0,
          "runtime mediaJson includes initial position");

    source.publishStateForTest(playerState(84000000LL));

    check(stableSpy.count() == 1, "position-only update does not emit stable mediaChanged");
    check(runtimeSpy.count() == 2, "position-only update emits runtimeMediaChanged");
    check(mediaFromPayload(source.mediaJson()).value(QStringLiteral("position")).toDouble() == 42.0,
          "stable mediaJson keeps previous position snapshot");
    check(mediaFromPayload(source.runtimeMediaJson()).value(QStringLiteral("position")).toDouble() == 84.0,
          "runtime mediaJson advances position");
}

void testUnavailablePublishesStableAndRuntimeFallback()
{
    YakkaiMprisMediaSource source;
    QSignalSpy stableSpy(&source, &YakkaiMprisMediaSource::mediaChanged);
    QSignalSpy runtimeSpy(&source, &YakkaiMprisMediaSource::runtimeMediaChanged);

    source.publishStateForTest(playerState(42000000LL));
    source.publishUnavailableForTest(QStringLiteral("No player"));

    check(stableSpy.count() == 2, "unavailable state emits stable mediaChanged after available state");
    check(runtimeSpy.count() == 2, "unavailable state emits runtimeMediaChanged after available state");
    check(!source.available(), "unavailable state clears available");
    check(!source.playing(), "unavailable state clears playing");
    check(!mediaFromPayload(source.mediaJson()).value(QStringLiteral("available")).toBool(true),
          "stable mediaJson uses unavailable fallback");
    check(!mediaFromPayload(source.runtimeMediaJson()).value(QStringLiteral("available")).toBool(true),
          "runtime mediaJson uses unavailable fallback");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testPositionOnlyUpdatesRuntimePayloadWithoutStableMediaSignal();
    testUnavailablePublishesStableAndRuntimeFallback();

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
