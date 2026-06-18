#pragma once

#include <QtCore/QString>
#include <QtCore/QtTypes>
#include <QtCore/QVariantMap>

namespace yakkai::mpris {

struct PlayerState {
    QString service;
    QString playbackStatus;
    QVariantMap metadata;
    qint64 positionUsec = 0;
};

QString normalizeArtUrl(const QString& artUrl);
QString buildMediaPayload(const PlayerState& state);
QString buildUnavailableMediaPayload();

} // namespace yakkai::mpris
