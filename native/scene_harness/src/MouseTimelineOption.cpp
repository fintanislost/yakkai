#include "MouseTimelineOption.hpp"

#include <QStringList>

#include <cmath>
#include <optional>

namespace yakkai::harness {

namespace {

std::optional<int> parseNonNegativeInt(const QString& value)
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    if (!ok || parsed < 0) {
        return std::nullopt;
    }
    return parsed;
}

bool parseFiniteNormalizedDouble(const QString& value, double* parsed)
{
    bool ok = false;
    const double result = value.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(result) || result < 0.0 || result > 1.0) {
        return false;
    }
    *parsed = result;
    return true;
}

MouseTimelineOptionResult invalid(const QString& error)
{
    return MouseTimelineOptionResult{false, false, QString(), error};
}

} // namespace

MouseTimelineOptionResult validateMouseTimelineOption(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        return MouseTimelineOptionResult{true, false, QString(), QString()};
    }

    const QStringList rawKeyframes = trimmed.split(';', Qt::KeepEmptyParts);
    if (rawKeyframes.size() < 2) {
        return invalid(QStringLiteral("--debug-mouse-timeline requires at least two timeMs:x,y keyframes"));
    }

    QStringList normalizedKeyframes;
    normalizedKeyframes.reserve(rawKeyframes.size());
    std::optional<int> previousTimeMs;

    for (const QString& rawKeyframe : rawKeyframes) {
        const QString keyframe = rawKeyframe.trimmed();
        const QStringList timeAndPosition = keyframe.split(':', Qt::KeepEmptyParts);
        if (timeAndPosition.size() != 2) {
            return invalid(QStringLiteral("--debug-mouse-timeline must use timeMs:x,y;timeMs:x,y"));
        }

        const std::optional<int> timeMs = parseNonNegativeInt(timeAndPosition.at(0));
        if (!timeMs) {
            return invalid(QStringLiteral("--debug-mouse-timeline timeMs values must be non-negative integers"));
        }
        if (previousTimeMs && *timeMs <= *previousTimeMs) {
            return invalid(QStringLiteral("--debug-mouse-timeline timeMs values must be strictly increasing"));
        }

        const QStringList position = timeAndPosition.at(1).split(',', Qt::KeepEmptyParts);
        if (position.size() != 2) {
            return invalid(QStringLiteral("--debug-mouse-timeline positions must use normalized x,y coordinates"));
        }

        double x = 0.5;
        double y = 0.5;
        if (!parseFiniteNormalizedDouble(position.at(0), &x) || !parseFiniteNormalizedDouble(position.at(1), &y)) {
            return invalid(QStringLiteral("--debug-mouse-timeline positions must be finite numbers within 0..1"));
        }

        normalizedKeyframes.push_back(QStringLiteral("%1:%2,%3")
                                          .arg(QString::number(*timeMs),
                                               QString::number(x, 'g', 9),
                                               QString::number(y, 'g', 9)));
        previousTimeMs = *timeMs;
    }

    return MouseTimelineOptionResult{true, true, normalizedKeyframes.join(QLatin1Char(';')), QString()};
}

} // namespace yakkai::harness
