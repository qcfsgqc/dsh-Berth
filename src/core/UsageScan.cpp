#include "UsageScan.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace UsageScan {

namespace {

std::optional<qint64> tokenField(const QJsonObject &obj, const QString &key) {
    const QJsonValue v = obj.value(key);
    if (!v.isDouble())
        return std::nullopt;
    return static_cast<qint64>(v.toDouble());
}

bool parseObject(QStringView json, QJsonObject *out) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    *out = doc.object();
    return true;
}

} // namespace

Output parse(const QString &text, const QString &berth) {
    Output out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        const qsizetype sp = line.indexOf(QLatin1Char(' '));
        if (sp <= 0 || !line.startsWith(QLatin1Char('@')))
            continue;
        const QStringView tag = QStringView(line).left(sp);
        QJsonObject obj;
        if (!parseObject(QStringView(line).mid(sp + 1), &obj))
            continue;

        if (tag == u"@usage") {
            UsageAgg::Record r;
            r.day = QDate::fromString(obj.value(QStringLiteral("day")).toString(), Qt::ISODate);
            if (!r.day.isValid())
                continue;
            r.berth = berth;
            r.model = obj.value(QStringLiteral("model")).toString();
            r.input = tokenField(obj, QStringLiteral("input"));
            r.output = tokenField(obj, QStringLiteral("output"));
            r.cache = tokenField(obj, QStringLiteral("cache"));
            out.records.append(r);
        } else if (tag == u"@summary") {
            out.complete = true;
            out.rootExists = obj.value(QStringLiteral("rootExists")).toBool();
            out.zstdSupported = obj.value(QStringLiteral("zstd")).toBool(true);
            out.files = obj.value(QStringLiteral("files")).toInt();
            out.skippedFiles = obj.value(QStringLiteral("skippedFiles")).toInt();
            out.skippedLines = obj.value(QStringLiteral("skippedLines")).toInt();
            out.events = obj.value(QStringLiteral("events")).toInt();
        } else if (tag == u"@error") {
            out.complete = true;
            out.error = obj.value(QStringLiteral("message")).toString();
            if (out.error.isEmpty())
                out.error = QStringLiteral("unknown error");
        }
    }
    return out;
}

} // namespace UsageScan
