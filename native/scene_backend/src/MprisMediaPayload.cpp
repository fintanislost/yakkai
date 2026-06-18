#include "MprisMediaPayload.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtCore/QVariantList>

#include <algorithm>

namespace yakkai::mpris {
namespace {

constexpr double usecToSeconds(qint64 value)
{
    return static_cast<double>(value) / 1000000.0;
}

QString firstArtist(const QVariant& value)
{
    if (value.canConvert<QStringList>()) {
        const QStringList artists = value.toStringList();
        if (!artists.isEmpty()) {
            return artists.first();
        }
    }

    const QVariantList artists = value.toList();
    if (!artists.isEmpty()) {
        return artists.first().toString();
    }

    return value.toString();
}

QString compactMediaPayload(const QJsonObject& media)
{
    QJsonObject root;
    root.insert(QStringLiteral("__yakkaiMedia"), media);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QJsonObject unavailableMediaObject()
{
    QJsonObject media;
    media.insert(QStringLiteral("available"), false);
    media.insert(QStringLiteral("playing"), false);
    media.insert(QStringLiteral("title"), QString());
    media.insert(QStringLiteral("artist"), QString());
    media.insert(QStringLiteral("album"), QString());
    media.insert(QStringLiteral("duration"), 0.0);
    media.insert(QStringLiteral("position"), 0.0);
    media.insert(QStringLiteral("albumArtPath"), QString());
    return media;
}

} // namespace

QString normalizeArtUrl(const QString& artUrl)
{
    if (artUrl.isEmpty()) {
        return {};
    }

    const QUrl url(artUrl);
    if (!url.isLocalFile()) {
        return {};
    }

    return url.toLocalFile();
}

QString buildMediaPayload(const PlayerState& state)
{
    const bool available = !state.service.isEmpty() && !state.metadata.isEmpty();
    if (!available) {
        return buildUnavailableMediaPayload();
    }

    const double duration = usecToSeconds(state.metadata.value(QStringLiteral("mpris:length")).toLongLong());
    double position = usecToSeconds(state.positionUsec);
    if (duration > 0.0) {
        position = std::min(position, duration);
    }

    QJsonObject media = unavailableMediaObject();
    media.insert(QStringLiteral("available"), true);
    media.insert(QStringLiteral("playing"),
                 state.playbackStatus.compare(QStringLiteral("Playing"), Qt::CaseInsensitive) == 0);
    media.insert(QStringLiteral("title"), state.metadata.value(QStringLiteral("xesam:title")).toString());
    media.insert(QStringLiteral("artist"), firstArtist(state.metadata.value(QStringLiteral("xesam:artist"))));
    media.insert(QStringLiteral("album"), state.metadata.value(QStringLiteral("xesam:album")).toString());
    media.insert(QStringLiteral("duration"), duration);
    media.insert(QStringLiteral("position"), position);
    media.insert(QStringLiteral("albumArtPath"),
                 normalizeArtUrl(state.metadata.value(QStringLiteral("mpris:artUrl")).toString()));

    return compactMediaPayload(media);
}

QString buildUnavailableMediaPayload()
{
    return compactMediaPayload(unavailableMediaObject());
}

} // namespace yakkai::mpris
