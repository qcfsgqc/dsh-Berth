#include "core/DataFile.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>
#include <limits>

namespace DataFile {

namespace {

const QString schemaKey() { return QString::fromLatin1(kSchemaKey); }

// 严格整数判定：JSON 数值、有限、无小数部分、在 int 范围内
bool toStrictInt(const QJsonValue &v, int *out) {
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d)
        return false;
    if (d < double(std::numeric_limits<int>::min()) || d > double(std::numeric_limits<int>::max()))
        return false;
    *out = int(d);
    return true;
}

LoadResult corrupt(const QString &error) {
    LoadResult r;
    r.mode = Mode::Corrupt;
    r.version = -1;
    r.error = error;
    return r;
}

} // namespace

LoadResult load(const QByteArray &bytes, int supported, const QString &arrayKey) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError)
        return corrupt(QStringLiteral("JSON 解析失败（偏移 %1）：%2").arg(err.offset).arg(err.errorString()));

    LoadResult r;
    if (doc.isArray()) {
        // schema 0：顶层是数组（旧版 instances.json）
        if (arrayKey.isEmpty())
            return corrupt(QStringLiteral("顶层结构应为对象"));
        r.version = 0;
        r.root.insert(arrayKey, doc.array());
    } else if (doc.isObject()) {
        r.root = doc.object();
        const QJsonValue v = r.root.value(schemaKey());
        if (v.isUndefined()) {
            r.version = 0;
        } else {
            int ver = 0;
            if (!toStrictInt(v, &ver) || ver < 0)
                return corrupt(QStringLiteral("schemaVersion 不是非负整数"));
            r.version = ver;
        }
        r.root.remove(schemaKey());
    } else {
        return corrupt(QStringLiteral("顶层结构应为对象"));
    }

    r.mode = r.version > supported ? Mode::ReadOnly : Mode::Normal;
    return r;
}

LoadResult fresh(int supported) {
    LoadResult r;
    r.mode = Mode::Normal;
    r.version = supported;
    return r;
}

QString backupName(const QString &file, int oldVer) {
    return file + QStringLiteral(".bak-") + QString::number(oldVer);
}

QJsonObject merge(const QJsonObject &original, const QJsonObject &known, int supported) {
    QJsonObject out = original;
    for (auto it = known.constBegin(); it != known.constEnd(); ++it)
        out.insert(it.key(), it.value());
    out.insert(schemaKey(), supported);
    return out;
}

WritePlan planWrite(const QString &file, const LoadResult &loaded, const QByteArray &originalBytes,
                    const QJsonObject &known, int supported, bool backupExists) {
    WritePlan plan;
    if (loaded.mode == Mode::Corrupt) {
        plan.reason = QStringLiteral("文件无法解析，不覆盖原文件");
        return plan;
    }
    if (loaded.mode == Mode::ReadOnly || loaded.version > supported) {
        plan.reason = QStringLiteral("文件 schema 版本 %1 高于支持的版本 %2，请升级 Berth")
                          .arg(loaded.version)
                          .arg(supported);
        return plan;
    }

    plan.allowed = true;
    // 低版本且原文件存在：先按原字节备份；同名备份已存在则不覆盖
    if (loaded.version < supported && !originalBytes.isEmpty() && !backupExists) {
        WriteStep backup;
        backup.kind = StepKind::Backup;
        backup.path = backupName(file, loaded.version);
        backup.bytes = originalBytes;
        plan.steps.append(backup);
    }

    WriteStep write;
    write.kind = StepKind::Write;
    write.path = file;
    write.bytes = toBytes(merge(loaded.root, known, supported));
    plan.steps.append(write);
    return plan;
}

QByteArray toBytes(const QJsonObject &obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Indented);
}

QString fieldPath(const QString &prefix, const QString &field) {
    if (prefix.isEmpty())
        return field;
    return prefix + QLatin1Char('.') + field;
}

FieldReader::FieldReader(const QJsonObject &obj, const QString &prefix, QStringList *errors)
    : m_obj(obj), m_prefix(prefix), m_errors(errors) {}

bool FieldReader::has(const QString &key) const { return m_obj.contains(key); }

void FieldReader::report(const QString &key) const {
    if (!m_errors)
        return;
    const QString path = fieldPath(m_prefix, key);
    if (!m_errors->contains(path))
        m_errors->append(path);
}

QString FieldReader::string(const QString &key, const QString &def) const {
    const QJsonValue v = m_obj.value(key);
    if (v.isUndefined())
        return def;
    if (!v.isString()) {
        report(key);
        return def;
    }
    return v.toString();
}

bool FieldReader::boolean(const QString &key, bool def) const {
    const QJsonValue v = m_obj.value(key);
    if (v.isUndefined())
        return def;
    if (!v.isBool()) {
        report(key);
        return def;
    }
    return v.toBool();
}

int FieldReader::integer(const QString &key, int def) const {
    const QJsonValue v = m_obj.value(key);
    if (v.isUndefined())
        return def;
    int out = 0;
    if (!toStrictInt(v, &out)) {
        report(key);
        return def;
    }
    return out;
}

double FieldReader::number(const QString &key, double def) const {
    const QJsonValue v = m_obj.value(key);
    if (v.isUndefined())
        return def;
    if (!v.isDouble() || !std::isfinite(v.toDouble())) {
        report(key);
        return def;
    }
    return v.toDouble();
}

QJsonObject FieldReader::object(const QString &key) const {
    const QJsonValue v = m_obj.value(key);
    if (v.isUndefined())
        return {};
    if (!v.isObject()) {
        report(key);
        return {};
    }
    return v.toObject();
}

QJsonArray FieldReader::array(const QString &key) const {
    const QJsonValue v = m_obj.value(key);
    if (v.isUndefined())
        return {};
    if (!v.isArray()) {
        report(key);
        return {};
    }
    return v.toArray();
}

FieldReader FieldReader::child(const QString &key) const {
    return FieldReader(object(key), fieldPath(m_prefix, key), m_errors);
}

} // namespace DataFile
