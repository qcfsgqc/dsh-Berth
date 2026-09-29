#include "PluginOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStringList>
#include <QVariantList>

namespace {

QString packageJsonPath(const QString &profileDir) {
    return QDir(profileDir).filePath(QStringLiteral("package.json"));
}

// 读取并解析 package.json 根对象；失败时写入 error 并返回 false
bool loadPackageJson(const QString &profileDir, QJsonObject *root, QString *error) {
    if (profileDir.isEmpty() || !QFileInfo(profileDir).isDir()) {
        *error = QStringLiteral("profile 目录不存在：%1").arg(QDir::toNativeSeparators(profileDir));
        return false;
    }
    const QString path = packageJsonPath(profileDir);
    QFile file(path);
    if (!file.exists()) {
        *error = QStringLiteral("找不到 %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取 %1：%2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("package.json 解析失败：%1（偏移 %2）")
                     .arg(parseError.errorString())
                     .arg(parseError.offset);
        return false;
    }
    if (!doc.isObject()) {
        *error = QStringLiteral("package.json 根节点不是对象");
        return false;
    }
    *root = doc.object();
    return true;
}

QStringList bundlesOf(const QJsonObject &root) {
    QStringList result;
    const QJsonArray bundles = root.value(QStringLiteral("dsh")).toObject()
                                   .value(QStringLiteral("profile")).toObject()
                                   .value(QStringLiteral("bundles")).toArray();
    for (const QJsonValue &v : bundles) {
        if (v.isString())
            result.append(v.toString());
    }
    return result;
}

} // namespace

bool PluginOps::isBuiltinBundle(const QString &name) {
    static const QStringList builtins = {
        QStringLiteral("@deepseek-ai/dsh-base"),
        QStringLiteral("@deepseek-ai/dsh-web-app"),
        QStringLiteral("@deepseek-ai/dsh-headless"),
        QStringLiteral("@deepseek-ai/dsh-sdk-app"),
        QStringLiteral("@deepseek-ai/dsh-sdk-minimal"),
        QStringLiteral("@deepseek-ai/dsh-acp-app"),
    };
    return builtins.contains(name);
}

QVariantMap PluginOps::readPlugins(const QString &profileDir) {
    QVariantMap result;
    QJsonObject root;
    QString error;
    if (!loadPackageJson(profileDir, &root, &error)) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), error);
        result.insert(QStringLiteral("plugins"), QVariantList());
        return result;
    }

    const QJsonObject deps = root.value(QStringLiteral("dependencies")).toObject();
    const QStringList bundles = bundlesOf(root);

    QVariantList plugins;
    QStringList seen;
    auto append = [&](const QString &name) {
        if (name.isEmpty() || isBuiltinBundle(name) || seen.contains(name))
            return;
        seen.append(name);
        QVariantMap item;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("version"), deps.value(name).toString());
        item.insert(QStringLiteral("enabled"), bundles.contains(name));
        plugins.append(item);
    };
    for (auto it = deps.constBegin(); it != deps.constEnd(); ++it)
        append(it.key());
    // 只在 bundles 里、不在 dependencies 里的插件，version 留空
    for (const QString &name : bundles)
        append(name);

    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("error"), QString());
    result.insert(QStringLiteral("plugins"), plugins);
    return result;
}

bool PluginOps::removeFromBundles(const QString &profileDir, const QString &name, QString *error) {
    QString err;
    QJsonObject root;
    if (!loadPackageJson(profileDir, &root, &err)) {
        if (error)
            *error = err;
        return false;
    }

    QJsonObject dsh = root.value(QStringLiteral("dsh")).toObject();
    QJsonObject profile = dsh.value(QStringLiteral("profile")).toObject();
    const QJsonArray bundles = profile.value(QStringLiteral("bundles")).toArray();

    QJsonArray kept;
    bool removed = false;
    for (const QJsonValue &v : bundles) {
        if (v.isString() && v.toString() == name) {
            removed = true;
            continue;
        }
        kept.append(v);
    }
    if (!removed)
        return true;

    profile.insert(QStringLiteral("bundles"), kept);
    dsh.insert(QStringLiteral("profile"), profile);
    root.insert(QStringLiteral("dsh"), dsh);

    const QString path = packageJsonPath(profileDir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 %1：%2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("写入 %1 失败：%2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}
