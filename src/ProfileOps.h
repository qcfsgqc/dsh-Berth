#pragma once

#include <QString>
#include <QVariantMap>

// $DSH_HOME/profiles/<name> 的纯逻辑操作：名字校验、目录删除、配置复制、信息读取。
// 与 PluginOps 同一范式：纯静态方法、不涉及 UI 与进程；错误通过 out 参数写回。
class ProfileOps {
public:
    // 返回空串表示合法；否则返回中文错误说明（供 UI 直接显示）。
    // 规则与 dsh 一致（非空、不含路径分隔符、不等于 '.'/'..'/'node_modules'，后三者大小写不敏感），
    // 并额外禁止保留名 desktop（大小写不敏感，desktop profile 由 DSH Desktop 独占管理）。
    // AppController 调用前会先 trim；本方法按传入原样判断，不再做 trim。
    static QString validateProfileName(const QString &name);

    // junction/symlink 安全的递归删除：
    // 链接条目只删除链接本身（不跟随链接去删目标内容），普通文件 QFile::remove、普通目录递归；
    // 最后删除 dirPath 本身。目录不存在（或路径为空）视为成功。
    // 失败时把遇到的第一个错误写入 error 并返回 false。
    static bool removeProfileTree(const QString &dirPath, QString *error);

    // 复制 profile 的用户配置文件：package.json、pnpm-lock.yaml、cordis.patch.yml、pnpm-workspace.yaml
    // （各自存在才拷；目标目录会自动创建）。跳过 cordis.yml（每次启动都会重写）与 node_modules
    // （由 pnpm install 重建）；只复制上述四个文件，不递归拷贝其它路径。
    static bool copyProfileFiles(const QString &srcDir, const QString &dstDir, QString *error);

    // 读取 profile 信息：{ok, error, name, dir, exists, bundles, pluginCount}
    // - bundles = package.json 中 dsh.profile.bundles 的字符串条目（只读、不去重、不去内置 bundle）。
    // - 目录不存在：ok=true、exists=false、bundles 为空列表。
    // - 目录存在但 package.json 缺失：exists=true、bundles 为空列表、不报错。
    // - package.json 存在但无法读取/解析：ok=false，error 写原因（文案沿用 PluginOps 的格式）。
    // - pluginCount = bundles 中去掉 dsh 自带 bundle 的数量（判断直接调 PluginOps::isBuiltinBundle）。
    static QVariantMap readProfileInfo(const QString &profileDir);
};
