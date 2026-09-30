#include "core/InstanceCodec.h"

#include "core/DataFile.h"

#include <QJsonValue>

namespace InstanceCodec {

namespace {

const QString kId = QStringLiteral("id");
const QString kName = QStringLiteral("name");
const QString kPort = QStringLiteral("port");
const QString kProfile = QStringLiteral("profile");
const QString kDshHome = QStringLiteral("dshHome");
const QString kWorkspace = QStringLiteral("workspace");
const QString kAutostart = QStringLiteral("autostart");
const QString kLogPath = QStringLiteral("logPath");
const QString kEnv = QStringLiteral("env");
const QString kExtraArgs = QStringLiteral("extraArgs");
const QString kDshVersion = QStringLiteral("dshVersion");
const QString kAutoRestart = QStringLiteral("autoRestart");
const QString kIcon = QStringLiteral("icon");
const QString kGroup = QStringLiteral("group");
const QString kTags = QStringLiteral("tags");
const QString kNotes = QStringLiteral("notes");

const QString kKey = QStringLiteral("key");
const QString kValue = QStringLiteral("value");
const QString kSecret = QStringLiteral("secret");
const QString kKind = QStringLiteral("kind");

void addError(QStringList *errors, const QString &path) {
    if (errors && !errors->contains(path))
        errors->append(path);
}

// obj 中不在 known 里的字段
QJsonObject unknownFields(const QJsonObject &obj, const QStringList &known) {
    QJsonObject out;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!known.contains(it.key()))
            out.insert(it.key(), it.value());
    }
    return out;
}

// 字符串数组；有任一元素不是字符串时整体取默认值（空列表）并记录错误
QStringList checkedStringList(const DataFile::FieldReader &r, const QString &key, QStringList *errors) {
    const QJsonArray arr = r.array(key);
    QStringList out;
    for (const QJsonValue &v : arr) {
        if (!v.isString()) {
            addError(errors, DataFile::fieldPath(r.prefix(), key));
            return {};
        }
        out.append(v.toString());
    }
    return out;
}

QJsonArray toArray(const QStringList &list) {
    QJsonArray arr;
    for (const QString &s : list)
        arr.append(s);
    return arr;
}

QList<InstanceEnvVar> readEnv(const DataFile::FieldReader &r, QStringList *errors) {
    const QJsonArray arr = r.array(kEnv);
    const QString envPrefix = DataFile::fieldPath(r.prefix(), kEnv);
    static const QStringList known = {kKey, kValue, kSecret};
    QList<InstanceEnvVar> out;
    for (int i = 0; i < arr.size(); ++i) {
        const QString rowPrefix = envPrefix + QStringLiteral("[%1]").arg(i);
        const QJsonValue v = arr.at(i);
        if (!v.isObject()) {
            addError(errors, rowPrefix);
            continue;
        }
        const QJsonObject obj = v.toObject();
        const DataFile::FieldReader row(obj, rowPrefix, errors);
        InstanceEnvVar var;
        var.key = row.string(kKey);
        var.value = row.string(kValue);
        var.secret = row.boolean(kSecret, false);
        var.extra = unknownFields(obj, known);
        out.append(var);
    }
    return out;
}

InstanceIcon readIcon(const DataFile::FieldReader &r, QStringList *errors) {
    static const QStringList known = {kKind, kValue};
    static const QStringList kinds = {QStringLiteral("none"), QStringLiteral("builtin"), QStringLiteral("file")};
    const DataFile::FieldReader icon = r.child(kIcon);
    InstanceIcon out;
    const QString kind = icon.string(kKind, out.kind);
    if (kinds.contains(kind))
        out.kind = kind;
    else // 取值不在允许范围内，按类型错误处理
        addError(errors, DataFile::fieldPath(icon.prefix(), kKind));
    out.value = icon.string(kValue);
    out.extra = unknownFields(icon.raw(), known);
    return out;
}

} // namespace

Instance fromJson(const QJsonObject &obj, QStringList *typeErrors, const QString &prefix) {
    static const QStringList known = {kId,         kName,      kPort,       kProfile,   kDshHome,
                                      kWorkspace,  kAutostart, kLogPath,    kEnv,       kExtraArgs,
                                      kDshVersion, kAutoRestart, kIcon,     kGroup,     kTags,
                                      kNotes};
    const DataFile::FieldReader r(obj, prefix, typeErrors);
    Instance item;
    item.id = r.string(kId);
    item.name = r.string(kName);
    item.port = r.integer(kPort, item.port);
    item.profile = r.string(kProfile, item.profile);
    item.dshHome = r.string(kDshHome);
    item.workspace = r.string(kWorkspace);
    item.autostart = r.boolean(kAutostart, item.autostart);
    item.logPath = r.string(kLogPath);
    item.env = readEnv(r, typeErrors);
    item.extraArgs = checkedStringList(r, kExtraArgs, typeErrors);
    item.dshVersion = r.string(kDshVersion);
    item.autoRestart = r.boolean(kAutoRestart, item.autoRestart);
    item.icon = readIcon(r, typeErrors);
    item.group = r.string(kGroup);
    item.tags = checkedStringList(r, kTags, typeErrors);
    item.notes = r.string(kNotes);
    item.extra = unknownFields(obj, known);
    return item;
}

QJsonObject toJson(const Instance &item) {
    QJsonObject obj = item.extra;
    obj.insert(kId, item.id);
    obj.insert(kName, item.name);
    obj.insert(kPort, item.port);
    obj.insert(kProfile, item.profile);
    obj.insert(kDshHome, item.dshHome);
    obj.insert(kWorkspace, item.workspace);
    obj.insert(kAutostart, item.autostart);
    obj.insert(kLogPath, item.logPath);

    QJsonArray env;
    for (const InstanceEnvVar &var : item.env) {
        QJsonObject row = var.extra;
        row.insert(kKey, var.key);
        row.insert(kValue, var.value);
        row.insert(kSecret, var.secret);
        env.append(row);
    }
    obj.insert(kEnv, env);
    obj.insert(kExtraArgs, toArray(item.extraArgs));
    obj.insert(kDshVersion, item.dshVersion);
    obj.insert(kAutoRestart, item.autoRestart);

    QJsonObject icon = item.icon.extra;
    icon.insert(kKind, item.icon.kind);
    icon.insert(kValue, item.icon.value);
    obj.insert(kIcon, icon);

    obj.insert(kGroup, item.group);
    obj.insert(kTags, toArray(item.tags));
    obj.insert(kNotes, item.notes);
    return obj;
}

QList<Instance> listFromJson(const QJsonArray &array, QStringList *typeErrors, const QString &prefix) {
    QList<Instance> items;
    for (int i = 0; i < array.size(); ++i) {
        const QString itemPrefix = prefix + QStringLiteral("[%1]").arg(i);
        const QJsonValue v = array.at(i);
        if (!v.isObject()) {
            addError(typeErrors, itemPrefix);
            continue;
        }
        items.append(fromJson(v.toObject(), typeErrors, itemPrefix));
    }
    return items;
}

QJsonArray listToJson(const QList<Instance> &items) {
    QJsonArray array;
    for (const Instance &item : items)
        array.append(toJson(item));
    return array;
}

} // namespace InstanceCodec
