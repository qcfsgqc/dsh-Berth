#include "core/InstanceMeta.h"

#include <QHash>

namespace InstanceMeta {

QString ungroupedName()
{
    return QStringLiteral("未分组");
}

int textLength(const QString &s)
{
    return int(s.toUcs4().size());
}

std::optional<QString> validateGroup(const QString &group)
{
    if (group.contains(QLatin1Char('\n')) || group.contains(QLatin1Char('\r')))
        return QStringLiteral("分组名不能包含换行");
    if (textLength(group.trimmed()) > kMaxGroupLen)
        return QStringLiteral("分组名最长 %1 个字符").arg(kMaxGroupLen);
    return std::nullopt;
}

static std::optional<QString> checkTagText(const QString &tag)
{
    const QString t = tag.trimmed();
    if (t.isEmpty())
        return QStringLiteral("标签不能为空");
    if (textLength(t) > kMaxTagLen)
        return QStringLiteral("标签最长 %1 个字符").arg(kMaxTagLen);
    return std::nullopt;
}

std::optional<QString> validateNewTag(const QStringList &existing, const QString &tag)
{
    if (auto err = checkTagText(tag))
        return err;
    const QString t = tag.trimmed();
    for (const QString &e : existing) {
        if (e.trimmed().compare(t, Qt::CaseInsensitive) == 0)
            return QStringLiteral("标签\"%1\"已存在").arg(t);
    }
    if (existing.size() >= kMaxTagCount)
        return QStringLiteral("最多 %1 个标签").arg(kMaxTagCount);
    return std::nullopt;
}

std::optional<QString> validateTags(const QStringList &tags)
{
    if (tags.size() > kMaxTagCount)
        return QStringLiteral("最多 %1 个标签").arg(kMaxTagCount);
    QStringList seen;
    for (const QString &tag : tags) {
        if (auto err = validateNewTag(seen, tag))
            return err;
        seen.append(tag.trimmed());
    }
    return std::nullopt;
}

std::optional<QString> validateNotes(const QString &notes)
{
    if (textLength(notes) > kMaxNotesLen)
        return QStringLiteral("备注最长 %1 个字符").arg(kMaxNotesLen);
    return std::nullopt;
}

std::optional<QString> validateImage(const QString &fileName, qint64 sizeBytes, bool readable)
{
    static const QStringList kExts = {QStringLiteral("png"), QStringLiteral("jpg"),
                                      QStringLiteral("jpeg"), QStringLiteral("ico"),
                                      QStringLiteral("svg")};
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    const int sep = qMax(fileName.lastIndexOf(QLatin1Char('/')), fileName.lastIndexOf(QLatin1Char('\\')));
    const QString ext = (dot > sep && dot >= 0) ? fileName.mid(dot + 1).toLower() : QString();
    if (!kExts.contains(ext))
        return QStringLiteral("图片格式只支持 PNG、JPG、JPEG、ICO、SVG");
    if (!readable)
        return QStringLiteral("图片文件无法读取");
    if (sizeBytes > kMaxImageBytes)
        return QStringLiteral("图片不能超过 1 MB（1,048,576 字节）");
    return std::nullopt;
}

QList<Group> groupBy(const QList<Instance> &items)
{
    QList<Group> groups;
    QHash<QString, int> pos;
    Group ungrouped;
    ungrouped.name = ungroupedName();
    ungrouped.ungrouped = true;

    for (int i = 0; i < items.size(); ++i) {
        const QString key = items[i].group.trimmed();
        if (key.isEmpty()) {
            ungrouped.indices.append(i);
            continue;
        }
        auto it = pos.constFind(key);
        if (it == pos.constEnd()) {
            Group g;
            g.name = key;
            g.indices.append(i);
            pos.insert(key, int(groups.size()));
            groups.append(g);
        } else {
            groups[*it].indices.append(i);
        }
    }
    if (!ungrouped.indices.isEmpty())
        groups.append(ungrouped);
    return groups;
}

bool matches(const Instance &item, const QString &keyword)
{
    const QString k = keyword.trimmed();
    if (k.isEmpty())
        return true;
    if (item.name.contains(k, Qt::CaseInsensitive))
        return true;
    for (const QString &tag : item.tags) {
        if (tag.contains(k, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QList<Group> search(const QList<Instance> &items, const QString &keyword)
{
    QList<Group> groups = groupBy(items);
    if (keyword.trimmed().isEmpty())
        return groups;
    QList<Group> out;
    for (Group &g : groups) {
        QList<int> kept;
        for (int i : g.indices) {
            if (matches(items[i], keyword))
                kept.append(i);
        }
        if (!kept.isEmpty()) {
            g.indices = kept;
            out.append(g);
        }
    }
    return out;
}

} // namespace InstanceMeta
