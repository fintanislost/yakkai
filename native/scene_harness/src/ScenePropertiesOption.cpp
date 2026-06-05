#include "ScenePropertiesOption.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>

namespace yakkai::harness
{

ScenePropertiesJsonOptionResult validateScenePropertiesJsonOption(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return ScenePropertiesJsonOptionResult{true, QString(), QString()};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return ScenePropertiesJsonOptionResult{
            false,
            QString(),
            QStringLiteral("invalid --scene-properties-json: %1").arg(parseError.errorString())
        };
    }

    if (!document.isObject()) {
        return ScenePropertiesJsonOptionResult{
            false,
            QString(),
            QStringLiteral("--scene-properties-json must be a JSON object")
        };
    }

    return ScenePropertiesJsonOptionResult{
        true,
        QString::fromUtf8(document.toJson(QJsonDocument::Compact)),
        QString()
    };
}

}
