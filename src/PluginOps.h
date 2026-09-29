#pragma once

#include <QString>
#include <QVariantMap>

// profile 插件的纯逻辑操作：只读写 <profileDir>/package.json，不涉及 UI 与进程
class PluginOps {
public:
    // 返回 {ok, error, plugins: [{name, version, enabled}]}
    // 合并 dependencies 与 dsh.profile.bundles，过滤 dsh 自带 bundle
    static QVariantMap readPlugins(const QString &profileDir);

    // 只从 dsh.profile.bundles 中移除 name；不在其中时视为成功、不写文件
    static bool removeFromBundles(const QString &profileDir, const QString &name, QString *error);

    static bool isBuiltinBundle(const QString &name);
};
