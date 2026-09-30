#pragma once

#include "Instance.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

class ProcessRunner;
class ProcessTask;

// 多版本 dsh 仓库（Dsh_Version_Store）：<dataDir>/dsh-versions/<semver>/。
// 安装：npm install -g --prefix <ver>.partial @deepseek-ai/dsh@<ver>，成功且找到可执行文件后改名为 <ver>；
// 失败时删除 .partial 并给出 npm 输出摘要。删除时引用计数 ≥ 1 拒绝。列表按 SemVer 降序。
class DshVersionStore : public QObject {
    Q_OBJECT
    // [{version, path, refCount, installing:false}]，已安装完成的版本，SemVer 降序
    Q_PROPERTY(QVariantList versions READ versions NOTIFY versionsChanged)
    // 正在安装的版本号
    Q_PROPERTY(QStringList installing READ installing NOTIFY installingChanged)
    Q_PROPERTY(QString rootDir READ rootDir CONSTANT)

public:
    struct Ops {
        std::function<QString()> dataDir;              // Berth 数据目录
        std::function<QList<Instance>()> instances;    // 当前泊位列表（引用计数）
        // 镜像兜底：官方源安装失败后用它重试一次；返回空表示不兜底（由 ProxyManager 提供）
        std::function<QString()> fallbackRegistry;
    };

    DshVersionStore(ProcessRunner *runner, QObject *parent = nullptr);

    // AppController 构造后注入；注入后立即扫描一次
    void setOps(Ops ops);

    QVariantList versions() const;
    QStringList installing() const { return m_installing.keys(); }
    QString rootDir() const;

    // 版本目录（不检查存在性）与其中的 dsh 可执行文件（Windows 为 <dir>\dsh.cmd）
    QString versionDir(const QString &version) const;
    // 可执行文件存在时返回其绝对路径，否则返回空
    Q_INVOKABLE QString executableFor(const QString &version) const;
    // 启动流水线用：成功写 *exe 并返回无值；目录或可执行文件缺失时返回含版本号与原因的错误
    std::optional<QString> resolve(const QString &version, QString *exe) const;

    Q_INVOKABLE bool isInstalled(const QString &version) const;
    Q_INVOKABLE bool isInstalling(const QString &version) const { return m_installing.contains(version); }
    // 引用该版本的泊位名称（按列表顺序）
    Q_INVOKABLE QStringList referencingInstances(const QString &version) const;

    // 开始安装；返回 {ok, error, version}（ok 表示已开始，结果经 installFinished 通知）。
    // 版本号非 SemVer、已安装、正在安装、找不到 npm 时 ok=false
    Q_INVOKABLE QVariantMap install(const QString &version);
    // 删除版本目录；返回 {ok, error, instances:[引用该版本的泊位名]}。
    // 引用计数 ≥ 1、正在安装、未安装或删除失败时 ok=false 且目录保持不变
    Q_INVOKABLE QVariantMap remove(const QString &version);
    // 重新扫描版本目录并重算引用计数
    Q_INVOKABLE void refresh();
    // 泊位绑定保存或泊位删除后调用：只重算引用计数，有变化才发 versionsChanged
    void recountReferences();

signals:
    void versionsChanged();
    void installingChanged();
    // 安装结束：ok 为 true 时 message 为成功说明，否则为失败原因（含 npm 输出摘要）
    void installFinished(const QString &version, bool ok, const QString &message);

private:
    struct Entry {
        QString version;
        QString path;
        int refCount = 0;
    };

    void onInstallFinished(const QString &version, ProcessTask *task, bool ok, int code,
                           const QString &tail, bool timedOut);
    void finishInstall(const QString &version, bool ok, const QString &message);
    // registry 非空时覆盖子进程的 npm_config_registry（镜像重试）
    void startNpm(const QString &ver, const QString &npm, const QString &partial,
                  const QString &registry);
    QHash<QString, int> countReferences() const;

    ProcessRunner *m_runner = nullptr;
    Ops m_ops;
    QList<Entry> m_entries;                    // SemVer 降序
    QHash<QString, ProcessTask *> m_installing; // 版本号 → 安装任务
    QHash<QString, QString> m_retryMirror;      // 已用镜像重试过的版本 → 镜像地址
};
