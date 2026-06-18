#include "MprisMediaPayload.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QVariantMap>

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

QJsonObject parseObject(const QString& json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    check(document.isObject(), "payload is a JSON object");
    return document.object();
}

QJsonObject mediaFromPayload(const QString& payload)
{
    return parseObject(payload).value(QStringLiteral("__yakkaiMedia")).toObject();
}

QVariantMap minimalMetadata()
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("xesam:title"), QStringLiteral("Instalock"));
    return metadata;
}

QVariantMap fullMetadata()
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("xesam:title"), QStringLiteral("Instalock"));
    metadata.insert(QStringLiteral("xesam:artist"), QStringList{QStringLiteral("fintanislost")});
    metadata.insert(QStringLiteral("xesam:album"), QStringLiteral("Back to Inova"));
    metadata.insert(QStringLiteral("mpris:length"), 240000000LL);
    metadata.insert(QStringLiteral("mpris:artUrl"), QStringLiteral("file:///home/fintan/repos/yakkai/native/scene_harness/tests/fixtures/media/Instalock-cover.png"));
    return metadata;
}

void checkUnavailableMediaShape(const QJsonObject& media, const char* context)
{
    check(!media.value(QStringLiteral("available")).toBool(true), context);
    check(!media.value(QStringLiteral("playing")).toBool(true), "unavailable media playing false");
    check(media.value(QStringLiteral("title")).toString().isEmpty(), "unavailable media title empty");
    check(media.value(QStringLiteral("artist")).toString().isEmpty(), "unavailable media artist empty");
    check(media.value(QStringLiteral("album")).toString().isEmpty(), "unavailable media album empty");
    check(media.value(QStringLiteral("duration")).toDouble(-1.0) == 0.0, "unavailable media duration zero");
    check(media.value(QStringLiteral("position")).toDouble(-1.0) == 0.0, "unavailable media position zero");
    check(media.value(QStringLiteral("albumArtPath")).toString().isEmpty(), "unavailable media album art path empty");
}

void testBuildsPlayingPayload()
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("xesam:title"), QStringLiteral("Instalock"));
    metadata.insert(QStringLiteral("xesam:artist"), QStringList{QStringLiteral("fintanislost")});
    metadata.insert(QStringLiteral("xesam:album"), QStringLiteral("Back to Inova"));
    metadata.insert(QStringLiteral("mpris:length"), 240000000LL);
    metadata.insert(QStringLiteral("mpris:artUrl"), QStringLiteral("file:///home/fintan/repos/yakkai/native/scene_harness/tests/fixtures/media/Instalock-cover.png"));

    const auto payload = yakkai::mpris::buildMediaPayload({
        .service = QStringLiteral("org.mpris.MediaPlayer2.test"),
        .playbackStatus = QStringLiteral("Playing"),
        .metadata = metadata,
        .positionUsec = 42000000LL,
    });

    const QJsonObject root = parseObject(payload);
    const QJsonObject media = root.value(QStringLiteral("__yakkaiMedia")).toObject();
    check(media.value(QStringLiteral("available")).toBool(false), "available true");
    check(media.value(QStringLiteral("playing")).toBool(false), "playing true");
    check(media.value(QStringLiteral("title")).toString() == QStringLiteral("Instalock"), "title mapped");
    check(media.value(QStringLiteral("artist")).toString() == QStringLiteral("fintanislost"), "artist mapped");
    check(media.value(QStringLiteral("album")).toString() == QStringLiteral("Back to Inova"), "album mapped");
    check(media.value(QStringLiteral("duration")).toDouble() == 240.0, "duration seconds mapped");
    check(media.value(QStringLiteral("position")).toDouble() == 42.0, "position seconds mapped");
    check(media.value(QStringLiteral("albumArtPath")).toString().startsWith(QStringLiteral("/home/fintan/repos/yakkai/")),
          "file art URL normalized to local path");
}

void testStoppedPayloadKeepsClockFallback()
{
    const auto payload = yakkai::mpris::buildUnavailableMediaPayload();
    checkUnavailableMediaShape(mediaFromPayload(payload), "unavailable false");
}

void testPositionClampsToDuration()
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("mpris:length"), 10000000LL);

    const auto payload = yakkai::mpris::buildMediaPayload({
        .service = QStringLiteral("org.mpris.MediaPlayer2.test"),
        .playbackStatus = QStringLiteral("Playing"),
        .metadata = metadata,
        .positionUsec = 90000000LL,
    });

    const QJsonObject media = parseObject(payload).value(QStringLiteral("__yakkaiMedia")).toObject();
    check(media.value(QStringLiteral("duration")).toDouble() == 10.0, "duration 10 seconds");
    check(media.value(QStringLiteral("position")).toDouble() == 10.0, "position clamped to duration");
}

void testNormalizeArtUrlRejectsUnavailableUrls()
{
    const QString localPath = QStringLiteral("/home/fintan/repos/yakkai/native/scene_harness/tests/fixtures/media/Instalock-cover.png");
    check(yakkai::mpris::normalizeArtUrl(localPath) == localPath,
          "bare local art path normalized to local path");
    check(yakkai::mpris::normalizeArtUrl(QStringLiteral("covers/cover.png")).isEmpty(),
          "relative art path rejected");
    check(yakkai::mpris::normalizeArtUrl(QStringLiteral("https://example.test/cover.png")).isEmpty(),
          "remote art URL rejected");
    check(yakkai::mpris::normalizeArtUrl(QStringLiteral("qrc:/covers/cover.png")).isEmpty(),
          "non-file art URL rejected");
    check(yakkai::mpris::normalizeArtUrl(QString()).isEmpty(), "empty art URL rejected");
}

void testPlayingRequiresAvailablePlayingStatus()
{
    const auto lowercasePayload = yakkai::mpris::buildMediaPayload({
        .service = QStringLiteral("org.mpris.MediaPlayer2.test"),
        .playbackStatus = QStringLiteral("playing"),
        .metadata = minimalMetadata(),
    });
    check(mediaFromPayload(lowercasePayload).value(QStringLiteral("playing")).toBool(false),
          "lowercase playing status maps to playing true");

    const auto mixedcasePayload = yakkai::mpris::buildMediaPayload({
        .service = QStringLiteral("org.mpris.MediaPlayer2.test"),
        .playbackStatus = QStringLiteral("PlAyInG"),
        .metadata = minimalMetadata(),
    });
    check(mediaFromPayload(mixedcasePayload).value(QStringLiteral("playing")).toBool(false),
          "mixedcase playing status maps to playing true");

    const auto pausedPayload = yakkai::mpris::buildMediaPayload({
        .service = QStringLiteral("org.mpris.MediaPlayer2.test"),
        .playbackStatus = QStringLiteral("Paused"),
        .metadata = minimalMetadata(),
    });
    check(!mediaFromPayload(pausedPayload).value(QStringLiteral("playing")).toBool(true),
          "paused status maps to playing false");

    const auto unavailablePayload = yakkai::mpris::buildMediaPayload({
        .service = QString(),
        .playbackStatus = QStringLiteral("Playing"),
        .metadata = minimalMetadata(),
    });
    check(!mediaFromPayload(unavailablePayload).value(QStringLiteral("playing")).toBool(true),
          "playing status stays false when unavailable");
}

void testAvailableRequiresServiceAndMetadata()
{
    const auto availablePayload = yakkai::mpris::buildMediaPayload({
        .service = QStringLiteral("org.mpris.MediaPlayer2.test"),
        .playbackStatus = QStringLiteral("Playing"),
        .metadata = minimalMetadata(),
    });
    check(mediaFromPayload(availablePayload).value(QStringLiteral("available")).toBool(false),
          "service and metadata maps to available true");

    const auto missingServicePayload = yakkai::mpris::buildMediaPayload({
        .service = QString(),
        .playbackStatus = QStringLiteral("Playing"),
        .metadata = fullMetadata(),
        .positionUsec = 42000000LL,
    });
    checkUnavailableMediaShape(mediaFromPayload(missingServicePayload),
                               "empty service maps to unavailable fallback shape");

    const auto missingMetadataPayload = yakkai::mpris::buildMediaPayload({
        .service = QStringLiteral("org.mpris.MediaPlayer2.test"),
        .playbackStatus = QStringLiteral("Playing"),
        .metadata = QVariantMap(),
        .positionUsec = 42000000LL,
    });
    checkUnavailableMediaShape(mediaFromPayload(missingMetadataPayload),
                               "empty metadata maps to unavailable fallback shape");
}

void testMissingOptionalFieldsKeepMediaAvailable()
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("mpris:length"), 30000000LL);

    const auto payload = yakkai::mpris::buildMediaPayload({
        .service = QStringLiteral("org.mpris.MediaPlayer2.test"),
        .playbackStatus = QString(),
        .metadata = metadata,
        .positionUsec = 5000000LL,
    });

    const QJsonObject media = mediaFromPayload(payload);
    check(media.value(QStringLiteral("available")).toBool(false),
          "service with partial metadata stays available");
    check(!media.value(QStringLiteral("playing")).toBool(true),
          "empty playback status maps to playing false");
    check(media.value(QStringLiteral("title")).toString().isEmpty(),
          "missing title maps to empty string");
    check(media.value(QStringLiteral("artist")).toString().isEmpty(),
          "missing artist maps to empty string");
    check(media.value(QStringLiteral("album")).toString().isEmpty(),
          "missing album maps to empty string");
    check(media.value(QStringLiteral("duration")).toDouble() == 30.0,
          "duration still maps from partial metadata");
    check(media.value(QStringLiteral("position")).toDouble() == 5.0,
          "position still maps from partial metadata");
}

} // namespace

int main()
{
    testBuildsPlayingPayload();
    testStoppedPayloadKeepsClockFallback();
    testPositionClampsToDuration();
    testNormalizeArtUrlRejectsUnavailableUrls();
    testPlayingRequiresAvailablePlayingStatus();
    testAvailableRequiresServiceAndMetadata();
    testMissingOptionalFieldsKeepMediaAvailable();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
