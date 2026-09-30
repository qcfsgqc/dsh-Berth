#include "core/BundleCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <climits>
#include <cmath>

namespace BundleCodec {

namespace {

const QString kManifest = QStringLiteral("manifest");
const QString kFiles = QStringLiteral("files");
const QString kPlugins = QStringLiteral("plugins");
const QString kInstance = QStringLiteral("instance");

const QString kFormatVersionKey = QStringLiteral("formatVersion");
const QString kExportedAt = QStringLiteral("exportedAt");
const QString kBerthVersion = QStringLiteral("berthVersion");
const QString kType = QStringLiteral("type");
const QString kProfileName = QStringLiteral("profileName");

const QString kPath = QStringLiteral("path");
const QString kEncoding = QStringLiteral("encoding");
const QString kContent = QStringLiteral("content");
const QString kUtf8 = QStringLiteral("utf8");
const QString kBase64 = QStringLiteral("base64");

const QString kName = QStringLiteral("name");
const QString kVersion = QStringLiteral("version");
const QString kEnabled = QStringLiteral("enabled");

const QString kPort = QStringLiteral("port");
const QString kProfile = QStringLiteral("profile");
const QString kWorkspace = QStringLiteral("workspace");
const QString kDshVersion = QStringLiteral("dshVersion");
const QString kEnv = QStringLiteral("env");
const QString kExtraArgs = QStringLiteral("extraArgs");
const QString kKey = QStringLiteral("key");
const QString kValue = QStringLiteral("value");
const QString kSecret = QStringLiteral("secret");

bool isValidUtf8(const QByteArray &data) {
    return QString::fromUtf8(data).toUtf8() == data;
}

std::optional<int> toInt(const QJsonValue &v) {
    if (!v.isDouble())
        return std::nullopt;
    const double d = v.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < INT_MIN || d > INT_MAX)
        return std::nullopt;
    return static_cast<int>(d);
}

// 可选字符串字段：缺失取空；存在但不是字符串时报错
bool readString(const QJsonObject &obj, const QString &key, QString *out) {
    const QJsonValue v = obj.value(key);
    if (v.isUndefined() || v.isNull()) {
        out->clear();
        return true;
    }
    if (!v.isString())
        return false;
    *out = v.toString();
    return true;
}

bool readBool(const QJsonObject &obj, const QString &key, bool *out) {
    const QJsonValue v = obj.value(key);
    if (v.isUndefined() || v.isNull()) {
        *out = false;
        return true;
    }
    if (!v.isBool())
        return false;
    *out = v.toBool();
    return true;
}

QString normalizedPath(const QString &relPath) {
    QString p = relPath;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return p;
}

// 相对路径且不含 "." / ".." / 空段，不含盘符
bool isSafeRelPath(const QString &path) {
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char(':')))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'));
    for (const QString &seg : parts) {
        if (seg.isEmpty() || seg == QLatin1String(".") || seg == QLatin1String(".."))
            return false;
    }
    return true;
}

QJsonObject manifestToJson(const Manifest &m) {
    QJsonObject o;
    o.insert(kFormatVersionKey, m.formatVersion);
    o.insert(kExportedAt, m.exportedAt);
    o.insert(kBerthVersion, m.berthVersion);
    o.insert(kType, m.type);
    o.insert(kProfileName, m.profileName);
    return o;
}

QJsonObject instanceToJson(const BundleInstance &inst, bool includeSecrets) {
    QJsonObject o;
    o.insert(kPort, inst.port);
    o.insert(kProfile, inst.profile);
    o.insert(kWorkspace, inst.workspace);
    o.insert(kDshVersion, inst.dshVersion);
    QJsonArray env;
    for (const BundleEnvVar &var : inst.env) {
        QJsonObject row;
        row.insert(kKey, var.key);
        // 敏感值不导出、或本身就是未补填的敏感值时，只写键名与标记
        if (!(var.secret && (!includeSecrets || var.valueOmitted)))
            row.insert(kValue, var.value);
        row.insert(kSecret, var.secret);
        env.append(row);
    }
    o.insert(kEnv, env);
    QJsonArray args;
    for (const QString &a : inst.extraArgs)
        args.append(a);
    o.insert(kExtraArgs, args);
    return o;
}

QString fieldError(const QString &path) {
    return QStringLiteral("Bundle 字段 %1 缺失或类型不正确").arg(path);
}

std::optional<QString> readManifest(const QJsonObject &o, int maxFormat, Manifest *m) {
    const std::optional<int> fv = toInt(o.value(kFormatVersionKey));
    if (!fv || *fv < 1)
        return fieldError(QStringLiteral("manifest.formatVersion"));
    if (*fv > maxFormat)
        return QStringLiteral("Bundle 格式版本 %1 高于当前支持的最高版本 %2，请升级 Berth").arg(*fv).arg(maxFormat);
    m->formatVersion = *fv;
    if (!readString(o, kExportedAt, &m->exportedAt))
        return fieldError(QStringLiteral("manifest.exportedAt"));
    if (!readString(o, kBerthVersion, &m->berthVersion))
        return fieldError(QStringLiteral("manifest.berthVersion"));
    const QJsonValue type = o.value(kType);
    if (!type.isString() || (type.toString() != kTypeInstance && type.toString() != kTypeProfile))
        return QStringLiteral("Bundle 类型不正确，应为 instance 或 profile");
    m->type = type.toString();
    if (!readString(o, kProfileName, &m->profileName))
        return fieldError(QStringLiteral("manifest.profileName"));
    return std::nullopt;
}

std::optional<QString> readFiles(const QJsonValue &v, QList<BundleFile> *out) {
    if (v.isUndefined() || v.isNull())
        return std::nullopt; // 没有配置文件也算合法
    if (!v.isArray())
        return fieldError(QStringLiteral("files"));
    const QJsonArray arr = v.toArray();
    for (int i = 0; i < arr.size(); ++i) {
        const QString at = QStringLiteral("files[%1]").arg(i);
        if (!arr.at(i).isObject())
            return fieldError(at);
        const QJsonObject o = arr.at(i).toObject();
        const QJsonValue path = o.value(kPath);
        const QJsonValue enc = o.value(kEncoding);
        const QJsonValue content = o.value(kContent);
        if (!path.isString())
            return fieldError(at + QStringLiteral(".path"));
        if (!content.isString())
            return fieldError(at + QStringLiteral(".content"));
        const QString p = normalizedPath(path.toString());
        if (!isSafeRelPath(p))
            return QStringLiteral("Bundle 中的文件路径不安全：%1").arg(path.toString());
        BundleFile f;
        f.path = p;
        const QString e = enc.isString() ? enc.toString() : QString();
        if (e == kUtf8) {
            f.data = content.toString().toUtf8();
        } else if (e == kBase64) {
            const auto r = QByteArray::fromBase64Encoding(content.toString().toLatin1(),
                                                          QByteArray::AbortOnBase64DecodingErrors);
            if (!r)
                return QStringLiteral("Bundle 文件 %1 的 base64 内容无法解码").arg(p);
            f.data = *r;
        } else {
            return fieldError(at + QStringLiteral(".encoding"));
        }
        if (excluded(p))
            continue; // 不导入 node_modules 与日志文件
        out->append(f);
    }
    return std::nullopt;
}

std::optional<QString> readPlugins(const QJsonArray &arr, QList<BundlePlugin> *out) {
    for (int i = 0; i < arr.size(); ++i) {
        const QString at = QStringLiteral("plugins[%1]").arg(i);
        if (!arr.at(i).isObject())
            return fieldError(at);
        const QJsonObject o = arr.at(i).toObject();
        const QJsonValue name = o.value(kName);
        if (!name.isString() || name.toString().isEmpty())
            return fieldError(at + QStringLiteral(".name"));
        BundlePlugin p;
        p.name = name.toString();
        if (!readString(o, kVersion, &p.version))
            return fieldError(at + QStringLiteral(".version"));
        if (!readBool(o, kEnabled, &p.enabled))
            return fieldError(at + QStringLiteral(".enabled"));
        out->append(p);
    }
    return std::nullopt;
}

std::optional<QString> readInstance(const QJsonObject &o, BundleInstance *inst) {
    const QJsonValue port = o.value(kPort);
    if (!port.isUndefined()) {
        const std::optional<int> p = toInt(port);
        if (!p)
            return fieldError(QStringLiteral("instance.port"));
        inst->port = *p;
    }
    if (!readString(o, kProfile, &inst->profile))
        return fieldError(QStringLiteral("instance.profile"));
    if (!readString(o, kWorkspace, &inst->workspace))
        return fieldError(QStringLiteral("instance.workspace"));
    if (!readString(o, kDshVersion, &inst->dshVersion))
        return fieldError(QStringLiteral("instance.dshVersion"));

    const QJsonValue env = o.value(kEnv);
    if (!env.isUndefined() && !env.isNull()) {
        if (!env.isArray())
            return fieldError(QStringLiteral("instance.env"));
        const QJsonArray arr = env.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            const QString at = QStringLiteral("instance.env[%1]").arg(i);
            if (!arr.at(i).isObject())
                return fieldError(at);
            const QJsonObject row = arr.at(i).toObject();
            BundleEnvVar var;
            if (!readString(row, kKey, &var.key))
                return fieldError(at + QStringLiteral(".key"));
            if (!readBool(row, kSecret, &var.secret))
                return fieldError(at + QStringLiteral(".secret"));
            const QJsonValue value = row.value(kValue);
            if (value.isUndefined() || value.isNull()) {
                var.valueOmitted = var.secret; // 敏感键只有键名：以空值导入，待补填
            } else if (value.isString()) {
                var.value = value.toString();
            } else {
                return fieldError(at + QStringLiteral(".value"));
            }
            inst->env.append(var);
        }
    }

    const QJsonValue args = o.value(kExtraArgs);
    if (!args.isUndefined() && !args.isNull()) {
        if (!args.isArray())
            return fieldError(QStringLiteral("instance.extraArgs"));
        for (const QJsonValue &a : args.toArray()) {
            if (!a.isString())
                return fieldError(QStringLiteral("instance.extraArgs"));
            inst->extraArgs.append(a.toString());
        }
    }
    return std::nullopt;
}

DecodeResult fail(const QString &error) {
    DecodeResult r;
    r.error = error;
    return r;
}

} // namespace

QByteArray encode(const Bundle &bundle, bool includeSecrets) {
    QJsonObject root;
    root.insert(kManifest, manifestToJson(bundle.manifest));

    QJsonArray files;
    for (const BundleFile &f : bundle.files) {
        const QString p = normalizedPath(f.path);
        if (excluded(p))
            continue;
        QJsonObject o;
        o.insert(kPath, p);
        if (isValidUtf8(f.data)) {
            o.insert(kEncoding, kUtf8);
            o.insert(kContent, QString::fromUtf8(f.data));
        } else {
            o.insert(kEncoding, kBase64);
            o.insert(kContent, QString::fromLatin1(f.data.toBase64()));
        }
        files.append(o);
    }
    root.insert(kFiles, files);

    QJsonArray plugins;
    for (const BundlePlugin &p : bundle.plugins) {
        QJsonObject o;
        o.insert(kName, p.name);
        o.insert(kVersion, p.version);
        o.insert(kEnabled, p.enabled);
        plugins.append(o);
    }
    root.insert(kPlugins, plugins);

    if (bundle.instance)
        root.insert(kInstance, instanceToJson(*bundle.instance, includeSecrets));

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

DecodeResult decode(const QByteArray &bytes, int maxFormat) {
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError)
        return fail(QStringLiteral("Bundle 文件无法解析：%1（偏移 %2）").arg(perr.errorString()).arg(perr.offset));
    if (!doc.isObject())
        return fail(QStringLiteral("Bundle 文件无法解析：根节点不是对象"));
    const QJsonObject root = doc.object();

    const QJsonValue manifest = root.value(kManifest);
    if (!manifest.isObject())
        return fail(QStringLiteral("Bundle 缺少清单（manifest）"));
    const QJsonValue plugins = root.value(kPlugins);
    if (!plugins.isArray())
        return fail(QStringLiteral("Bundle 缺少插件清单（plugins）"));

    Bundle b;
    if (auto err = readManifest(manifest.toObject(), maxFormat, &b.manifest))
        return fail(*err);
    if (auto err = readFiles(root.value(kFiles), &b.files))
        return fail(*err);
    if (auto err = readPlugins(plugins.toArray(), &b.plugins))
        return fail(*err);

    const QJsonValue instance = root.value(kInstance);
    if (instance.isObject()) {
        BundleInstance inst;
        if (auto err = readInstance(instance.toObject(), &inst))
            return fail(*err);
        b.instance = inst;
    } else if (!instance.isUndefined() && !instance.isNull()) {
        return fail(fieldError(QStringLiteral("instance")));
    }
    if (b.manifest.type == kTypeInstance && !b.instance)
        return fail(QStringLiteral("泊位类型的 Bundle 缺少泊位配置（instance）"));

    DecodeResult r;
    r.value = std::move(b);
    return r;
}

bool excluded(const QString &relPath) {
    const QString p = normalizedPath(relPath);
    const QStringList parts = p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &seg : parts) {
        if (seg.compare(QLatin1String("node_modules"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return p.endsWith(QLatin1String(".log"), Qt::CaseInsensitive);
}

QStringList missingSecrets(const Bundle &bundle) {
    QStringList out;
    if (!bundle.instance)
        return out;
    for (const BundleEnvVar &var : bundle.instance->env) {
        if (var.valueOmitted)
            out.append(var.key);
    }
    return out;
}

} // namespace BundleCodec
