#include "MediaStateTimelineOption.hpp"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>

#include <cmath>
#include <limits>

namespace yakkai::harness {
namespace {

MediaStateTimelineOptionResult invalid(const QString& error)
{
    return MediaStateTimelineOptionResult{false, false, QString(), error};
}

bool parseTimeMs(const QJsonValue& value, int* parsed)
{
    if (!value.isDouble()) {
        return false;
    }

    const double raw = value.toDouble();
    if (!std::isfinite(raw) || raw < 0.0 || raw > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }

    const double rounded = std::round(raw);
    if (std::abs(raw - rounded) > 1.0e-9) {
        return false;
    }

    *parsed = static_cast<int>(rounded);
    return true;
}

QJsonObject directMediaFields(const QJsonObject& keyframe)
{
    QJsonObject media;
    for (auto it = keyframe.constBegin(); it != keyframe.constEnd(); ++it) {
        if (it.key() == QStringLiteral("timeMs")) {
            continue;
        }
        media.insert(it.key(), it.value());
    }
    return media;
}

QJsonObject mediaObjectForKeyframe(const QJsonObject& keyframe)
{
    const QJsonValue mediaValue = keyframe.value(QStringLiteral("media"));
    if (mediaValue.isObject()) {
        return mediaValue.toObject();
    }

    const QJsonValue yakkaiMediaValue = keyframe.value(QStringLiteral("__yakkaiMedia"));
    if (yakkaiMediaValue.isObject()) {
        return yakkaiMediaValue.toObject();
    }

    return directMediaFields(keyframe);
}

} // namespace

MediaStateTimelineOptionResult validateMediaStateTimelineOption(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        return MediaStateTimelineOptionResult{true, false, QString(), QString()};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return invalid(QStringLiteral("invalid --media-state-timeline-json: %1").arg(parseError.errorString()));
    }
    if (!document.isArray()) {
        return invalid(QStringLiteral("--media-state-timeline-json must be a JSON array"));
    }

    const QJsonArray rawTimeline = document.array();
    if (rawTimeline.isEmpty()) {
        return invalid(QStringLiteral("--media-state-timeline-json requires at least one keyframe"));
    }

    QJsonArray normalizedTimeline;
    int previousTimeMs = -1;
    for (const QJsonValue& rawKeyframe : rawTimeline) {
        if (!rawKeyframe.isObject()) {
            return invalid(QStringLiteral("--media-state-timeline-json keyframes must be JSON objects"));
        }

        const QJsonObject keyframe = rawKeyframe.toObject();
        int timeMs = 0;
        if (!parseTimeMs(keyframe.value(QStringLiteral("timeMs")), &timeMs)) {
            return invalid(QStringLiteral("--media-state-timeline-json keyframes require non-negative integer timeMs"));
        }
        if (timeMs <= previousTimeMs) {
            return invalid(QStringLiteral("--media-state-timeline-json timeMs values must be strictly increasing"));
        }

        const QJsonObject media = mediaObjectForKeyframe(keyframe);
        if (media.isEmpty()) {
            return invalid(QStringLiteral("--media-state-timeline-json keyframes must include media fields"));
        }

        QJsonObject normalizedKeyframe;
        normalizedKeyframe.insert(QStringLiteral("timeMs"), timeMs);
        normalizedKeyframe.insert(QStringLiteral("media"), media);
        normalizedTimeline.push_back(normalizedKeyframe);
        previousTimeMs = timeMs;
    }

    return MediaStateTimelineOptionResult{
        true,
        true,
        QString::fromUtf8(QJsonDocument(normalizedTimeline).toJson(QJsonDocument::Compact)),
        QString(),
    };
}

} // namespace yakkai::harness
