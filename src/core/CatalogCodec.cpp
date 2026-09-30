#include "core/CatalogCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

const char *const kChannels[] = {"stable", "beta", "alpha"};

QString *channelSlot(CatalogEntry &e, int i)
{
    switch (i) {
    case 0: return &e.stable;
    case 1: return &e.beta;
    default: return &e.alpha;
    }
}

CatalogCodec::ParseResult fail(const QString &msg)
{
    CatalogCodec::ParseResult r;
    r.ok = false;
    r.error = msg;
    return r;
}

} // namespace

QString CatalogEntry::version(const QString &channel) const
{
    if (channel == QLatin1String("stable"))
        return stable;
    if (channel == QLatin1String("beta"))
        return beta;
    if (channel == QLatin1String("alpha"))
        return alpha;
    return QString();
}

bool CatalogEntry::hasAnyVersion() const
{
    return !stable.isEmpty() || !beta.isEmpty() || !alpha.isEmpty();
}

namespace CatalogCodec {

ParseResult parse(const QByteArray &json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError)
        return fail(QStringLiteral("目录不是合法 JSON（偏移 %1）：%2").arg(err.offset).arg(err.errorString()));

    QJsonArray list;
    QString base;
    if (doc.isArray()) {
        list = doc.array();
        base = QStringLiteral("plugins");
    } else if (doc.isObject()) {
        const QJsonValue v = doc.object().value(QStringLiteral("plugins"));
        if (!v.isArray())
            return fail(QStringLiteral("plugins：缺少或不是数组"));
        list = v.toArray();
        base = QStringLiteral("plugins");
    } else {
        return fail(QStringLiteral("目录根节点应为对象或数组"));
    }

    ParseResult result;
    result.entries.reserve(list.size());
    for (qsizetype i = 0; i < list.size(); ++i) {
        const QString at = QStringLiteral("%1[%2]").arg(base).arg(i);
        const QJsonValue item = list.at(i);
        if (!item.isObject())
            return fail(QStringLiteral("%1：条目应为对象").arg(at));
        const QJsonObject obj = item.toObject();

        CatalogEntry e;
        const QJsonValue name = obj.value(QStringLiteral("name"));
        if (!name.isString() || name.toString().isEmpty())
            return fail(QStringLiteral("%1.name：缺少包名或不是非空字符串").arg(at));
        e.name = name.toString();

        const QJsonValue desc = obj.value(QStringLiteral("description"));
        if (!desc.isUndefined() && !desc.isNull()) {
            if (!desc.isString())
                return fail(QStringLiteral("%1.description：应为字符串").arg(at));
            e.description = desc.toString();
        }

        const QJsonValue versions = obj.value(QStringLiteral("versions"));
        if (versions.isObject()) {
            const QJsonObject vo = versions.toObject();
            for (int c = 0; c < 3; ++c) {
                const QJsonValue v = vo.value(QLatin1String(kChannels[c]));
                if (v.isUndefined() || v.isNull())
                    continue;
                if (!v.isString())
                    return fail(QStringLiteral("%1.versions.%2：应为字符串").arg(at, QLatin1String(kChannels[c])));
                *channelSlot(e, c) = v.toString();
            }
        } else if (!versions.isUndefined() && !versions.isNull()) {
            return fail(QStringLiteral("%1.versions：应为对象").arg(at));
        }
        if (!e.hasAnyVersion())
            return fail(QStringLiteral("%1.versions：stable / beta / alpha 至少需要一个版本号").arg(at));

        result.entries.append(e);
    }
    result.ok = true;
    return result;
}

QByteArray print(const QList<CatalogEntry> &entries)
{
    QJsonArray list;
    for (const CatalogEntry &e : entries) {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), e.name);
        if (!e.description.isEmpty())
            obj.insert(QStringLiteral("description"), e.description);
        QJsonObject vo;
        for (int c = 0; c < 3; ++c) {
            const QString v = e.version(QLatin1String(kChannels[c]));
            if (!v.isEmpty())
                vo.insert(QLatin1String(kChannels[c]), v);
        }
        obj.insert(QStringLiteral("versions"), vo);
        list.append(obj);
    }
    QJsonObject root;
    root.insert(QStringLiteral("plugins"), list);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QList<CatalogEntry> filter(const QList<CatalogEntry> &entries, const QString &keyword)
{
    if (keyword.isEmpty())
        return entries;
    QList<CatalogEntry> out;
    for (const CatalogEntry &e : entries) {
        if (e.name.contains(keyword, Qt::CaseInsensitive)
            || e.description.contains(keyword, Qt::CaseInsensitive))
            out.append(e);
    }
    return out;
}

} // namespace CatalogCodec
