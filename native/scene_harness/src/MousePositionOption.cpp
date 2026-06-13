#include "MousePositionOption.hpp"

#include <QtCore/QStringList>

#include <cmath>

namespace yakkai::harness
{

namespace
{

bool parseFiniteDouble(const QString& value, double* parsed)
{
    bool ok = false;
    const double result = value.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(result)) {
        return false;
    }
    *parsed = result;
    return true;
}

}

MousePositionOptionResult validateMousePositionOption(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return MousePositionOptionResult{true, false, 0.5, 0.5, QString(), QString()};
    }

    const QStringList parts = trimmed.split(',', Qt::KeepEmptyParts);
    if (parts.size() != 2) {
        return MousePositionOptionResult{
            false,
            false,
            0.5,
            0.5,
            QString(),
            QStringLiteral("--debug-mouse-position must use normalized x,y coordinates")
        };
    }

    double x = 0.5;
    double y = 0.5;
    if (!parseFiniteDouble(parts.at(0), &x) || !parseFiniteDouble(parts.at(1), &y)) {
        return MousePositionOptionResult{
            false,
            false,
            0.5,
            0.5,
            QString(),
            QStringLiteral("--debug-mouse-position values must be finite numbers")
        };
    }

    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0) {
        return MousePositionOptionResult{
            false,
            false,
            0.5,
            0.5,
            QString(),
            QStringLiteral("--debug-mouse-position values must be within 0..1")
        };
    }

    return MousePositionOptionResult{
        true,
        true,
        x,
        y,
        QStringLiteral("%1,%2").arg(QString::number(x, 'g', 9), QString::number(y, 'g', 9)),
        QString()
    };
}

}
