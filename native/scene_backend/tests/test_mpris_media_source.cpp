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

void testSelectionPrefersMetadataProviderWhenNoPlayerIsPlaying()
{
    const QString selected = YakkaiMprisMediaSource::selectPreferredServiceForTest({
        {
            .service = QStringLiteral("org.mpris.MediaPlayer2.Gwenview"),
            .playbackStatus = QStringLiteral("Stopped"),
            .metadata = QVariantMap(),
        },
        {
            .service = QStringLiteral("org.mpris.MediaPlayer2.vlc"),
            .playbackStatus = QStringLiteral("Paused"),
            .metadata = metadata(),
        },
    });

    check(selected == QStringLiteral("org.mpris.MediaPlayer2.vlc"),
          "selection prefers metadata-bearing paused player over stopped blank provider");
}

void testSelectionPrefersPlayingMetadataProvider()
{
    const QString selected = YakkaiMprisMediaSource::selectPreferredServiceForTest({
        {
            .service = QStringLiteral("org.mpris.MediaPlayer2.paused"),
            .playbackStatus = QStringLiteral("Paused"),
            .metadata = metadata(),
        },
        {
            .service = QStringLiteral("org.mpris.MediaPlayer2.playing"),
            .playbackStatus = QStringLiteral("Playing"),
            .metadata = metadata(),
        },
    });

    check(selected == QStringLiteral("org.mpris.MediaPlayer2.playing"),
          "selection prefers playing metadata-bearing player over paused metadata provider");
}

void testSelectionFallsBackToPlayingBlankProvider()
{
    const QString selected = YakkaiMprisMediaSource::selectPreferredServiceForTest({
        {
            .service = QStringLiteral("org.mpris.MediaPlayer2.stopped"),
            .playbackStatus = QStringLiteral("Stopped"),
            .metadata = QVariantMap(),
        },
        {
            .service = QStringLiteral("org.mpris.MediaPlayer2.playing"),
            .playbackStatus = QStringLiteral("Playing"),
            .metadata = QVariantMap(),
        },
    });

    check(selected == QStringLiteral("org.mpris.MediaPlayer2.playing"),
          "selection falls back to playing blank provider when no metadata is available");
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

void testPlayerServiceSwitchEmitsStableMediaChange()
{
    YakkaiMprisMediaSource source;
    QSignalSpy stableSpy(&source, &YakkaiMprisMediaSource::mediaChanged);
    QSignalSpy runtimeSpy(&source, &YakkaiMprisMediaSource::runtimeMediaChanged);

    source.publishStateForTest(playerState(42000000LL));

    auto switchedPlayer = playerState(84000000LL);
    switchedPlayer.service = QStringLiteral("org.mpris.MediaPlayer2.other");
    source.publishStateForTest(switchedPlayer);

    check(stableSpy.count() == 2, "player service switch emits stable mediaChanged");
    check(runtimeSpy.count() == 2, "player service switch emits runtimeMediaChanged");
    check(source.activeService() == QStringLiteral("org.mpris.MediaPlayer2.other"),
          "active service follows selected player switch");
    check(mediaFromPayload(source.mediaJson()).value(QStringLiteral("position")).toDouble() == 84.0,
          "stable mediaJson refreshes from switched player payload");
}

void testPlaybackStatusChangeEmitsStableMediaChange()
{
    YakkaiMprisMediaSource source;
    QSignalSpy stableSpy(&source, &YakkaiMprisMediaSource::mediaChanged);
    QSignalSpy runtimeSpy(&source, &YakkaiMprisMediaSource::runtimeMediaChanged);

    source.publishStateForTest(playerState(42000000LL));

    auto pausedPlayer = playerState(84000000LL);
    pausedPlayer.playbackStatus = QStringLiteral("Paused");
    source.publishStateForTest(pausedPlayer);

    check(stableSpy.count() == 2, "playback status change emits stable mediaChanged");
    check(runtimeSpy.count() == 2, "playback status change emits runtimeMediaChanged");
    check(source.available(), "paused player remains available");
    check(!source.playing(), "paused player clears playing");
    check(!mediaFromPayload(source.mediaJson()).value(QStringLiteral("playing")).toBool(true),
          "stable mediaJson refreshes paused playback state");
}

void testPlaybackStatusCaseChangeIsNotStableMetadataChange()
{
    YakkaiMprisMediaSource source;
    QSignalSpy stableSpy(&source, &YakkaiMprisMediaSource::mediaChanged);
    QSignalSpy runtimeSpy(&source, &YakkaiMprisMediaSource::runtimeMediaChanged);

    source.publishStateForTest(playerState(42000000LL));

    auto lowercasePlayer = playerState(84000000LL);
    lowercasePlayer.playbackStatus = QStringLiteral("playing");
    source.publishStateForTest(lowercasePlayer);

    check(stableSpy.count() == 1, "playback status case-only change does not emit stable mediaChanged");
    check(runtimeSpy.count() == 2, "playback status case-only change still emits runtime position update");
    check(source.playing(), "case-insensitive playback status keeps playing true");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testSelectionPrefersMetadataProviderWhenNoPlayerIsPlaying();
    testSelectionPrefersPlayingMetadataProvider();
    testSelectionFallsBackToPlayingBlankProvider();
    testPositionOnlyUpdatesRuntimePayloadWithoutStableMediaSignal();
    testUnavailablePublishesStableAndRuntimeFallback();
    testPlayerServiceSwitchEmitsStableMediaChange();
    testPlaybackStatusChangeEmitsStableMediaChange();
    testPlaybackStatusCaseChangeIsNotStableMetadataChange();

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
