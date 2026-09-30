#pragma once

#include "Instance.h"
#include "core/CatalogCodec.h"
#include "core/PluginSet.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class ProcessRunner;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// 按 (DSH_HOME, profile) 管理插件：列表、启用开关、批量启用/禁用/卸载。
// 读写 <home>/profiles/<profile>/package.json 的 dsh.profile.bundles；写入前先快照，
// 用 QSaveFile 写出，失败时把文件恢复为快照。同一 profile 同一时刻只允许一项操作。
class PluginManager : public QObject {
    Q_OBJECT
    // 进行中的批量操作：keyOf(home, profile) → {done, total, op}；界面据此显示 x/y 并禁用该 profile 的写操作
    Q_PROPERTY(QVariantMap progress READ progress NOTIFY busyChanged)
    // —— 插件市场目录 ——
    // 当前显示的目录：[{name, description, stable, beta, alpha}]（渠道无版本时为空串）
    Q_PROPERTY(QVariantList catalog READ catalog NOTIFY catalogChanged)
    // 拉取进行中
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY catalogChanged)
    // 最近一次拉取的错误（超时 / 网络 / 解析 / 地址非法）；成功时为空
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY catalogChanged)
    // catalog 来自 catalog-cache.json（或上一次成功结果）而不是本次拉取
    Q_PROPERTY(bool catalogFromCache READ catalogFromCache NOTIFY catalogChanged)

public:
    struct Ops {
        // 空串 → 默认 DSH_HOME；非空 → 规范化后的绝对路径
        std::function<QString(const QString &)> resolveHome;
        std::function<QList<Instance>()> instances;
        // 默认 DSH_HOME 下探测到的 profile 名
        std::function<QStringList()> knownProfiles;
        std::function<QString()> dshExecutable;
        // 其它会修改 profile 的操作（单个卸载、profile 创建/复制等）是否在进行
        std::function<bool()> externalBusy;
        // 目录源地址（Settings::catalogUrl）；每次 fetchCatalog 时读取，新地址即时生效
        std::function<QString()> catalogUrl;
        // Berth 数据目录（存放 catalog-cache.json）
        std::function<QString()> dataDir;
        // 记录安装渠道：key = keyOf(home, profile) + "|" + 包名
        std::function<void(const QString &key, const QString &channel)> recordChannel;
        // 该 (home, profile) 下被隔离的插件：包名 → 原因描述（给 list 标记"已隔离"）
        std::function<QHash<QString, QString>(const QString &home, const QString &profile)> quarantined;
    };

    explicit PluginManager(ProcessRunner *runner, QObject *parent = nullptr);
    void setOps(Ops ops);
    // 共享的 QNetworkAccessManager（由 AppController 持有）
    void setNetwork(QNetworkAccessManager *nam);

    // 全部已知 (DSH_HOME, profile) 组合，去重（home 规范化后不区分大小写）：
    // [{home, profile, label, key}]；先默认 home 下的 profile，再补泊位引用的组合
    Q_INVOKABLE QVariantList targets() const;
    Q_INVOKABLE QString keyOf(const QString &home, const QString &profile) const;
    // {ok, error, writable, plugins: [{name, version, displayVersion, enabled, core, quarantined, quarantineReason}]}；
    // package.json 无法读取或解析时 ok=false、writable=false
    Q_INVOKABLE QVariantMap list(const QString &home, const QString &profile, bool showCore) const;
    // 单个启用开关，同步执行：{ok, error}。成功后发 pluginsChanged，必要时发 restartHintRequested
    Q_INVOKABLE QVariantMap setEnabled(const QString &home, const QString &profile, const QString &name, bool on);
    // 批量操作 op = "enable" / "disable" / "uninstall"，按 names 顺序逐个处理（异步）。
    // 返回 {ok, error}：ok=false 表示被拒绝、未开始；开始后结束时发 batchFinished
    Q_INVOKABLE QVariantMap batch(const QString &home, const QString &profile, const QString &op,
                                  const QStringList &names);
    Q_INVOKABLE bool isBusy(const QString &home, const QString &profile) const;
    QVariantMap progress() const;

    // —— 插件市场 ——
    // 异步拉取目录：15 秒未完成即 abort；失败或解析出错时设置 catalogError，
    // 并退回上一次成功的目录（内存中没有则读 catalog-cache.json，没有缓存时为空列表）。
    // 成功时写回 catalog-cache.json。拉取进行中再次调用不重复发起。结束时发 catalogFetched
    Q_INVOKABLE void fetchCatalog();
    // 按包名或描述过滤当前目录（不区分大小写，关键字为空返回全部），元素格式同 catalog
    Q_INVOKABLE QVariantList filterCatalog(const QString &keyword) const;
    // 安装输入校验（先去首尾空白）：合法返回空串，否则返回错误描述
    Q_INVOKABLE QString validateInstallInput(const QString &input) const;
    // 渠道 → dist-tag：stable→latest，beta→beta，alpha→alpha；其它返回空串
    Q_INVOKABLE QString distTag(const QString &channel) const;
    // 安装到 (home, profile)（异步）。spec 为 npm 规格或 git URL；
    // npm 规格不带 @后缀且 channel 非空时追加 @<dist-tag>。
    // 流程：快照 package.json → dsh plugin --profile <p> add <spec> → 确保包名在 bundles 中；
    // 任一步失败恢复快照。返回 {ok, error}：ok=false 表示被拒绝（格式错误 / 忙 / 缺 pnpm 等）、
    // 未调用包管理器；开始后进度出现在 progress[key]（op="install"），结束时发 installFinished
    Q_INVOKABLE QVariantMap install(const QString &home, const QString &profile, const QString &spec,
                                    const QString &channel);

    QVariantList catalog() const;
    bool catalogLoading() const { return m_catalogReply != nullptr; }
    QString catalogError() const { return m_catalogError; }
    bool catalogFromCache() const { return m_catalogFromCache; }

signals:
    void busyChanged();
    // 某 profile 的插件配置已成功变更（至少 1 项），界面据此刷新
    void pluginsChanged(const QString &home, const QString &profile);
    // summary: {op, total, succeeded, failed, succeededNames, failures: [{name, reason}]}
    void batchFinished(const QString &home, const QString &profile, const QVariantMap &summary);
    // 给 RestartHintController（15.1）：一次单个或批量变更结束后至多发 1 次；
    // 仅当至少 1 项成功且该 profile 有启动中/运行中的泊位时发出，instanceIds 按泊位列表顺序
    void restartHintRequested(const QString &home, const QString &profile, const QStringList &instanceIds);
    void catalogChanged();
    // 一次 fetchCatalog 结束：ok=false 时 error 为失败原因（catalog 已退回缓存）
    void catalogFetched(bool ok, const QString &error);
    // 安装结束。ok=true：message 为成功提示，name 为写入 bundles 的包名；
    // ok=false：message 为失败原因，tail 为包管理器输出最后最多 20 行，package.json 已恢复为安装前
    void installFinished(const QString &home, const QString &profile, const QString &spec, bool ok,
                         const QString &name, const QString &message, const QString &tail);

private:
    struct Job {
        QString home;
        QString profile;
        QString dir;
        QString op;
        QStringList names;
        QString channel; // 仅 install
        int index = 0;
        PluginSet::BatchSummary summary;
    };

    QString profileDir(const QString &home, const QString &profile) const;
    QStringList affected(const QString &home, const QString &profile) const;
    void step(const QString &key);
    void advance(const QString &key, const QString &name, bool ok, const QString &reason);
    void uninstallOne(const QString &key, const QString &name);
    void finishJob(const QString &key);
    void installOne(const QString &key);
    void finishInstall(const QString &key, bool ok, const QString &name, const QString &message,
                       const QString &tail);
    void onCatalogReply();
    QString cachePath() const;
    // 拉取失败时：内存中已有目录则保留，否则读 catalog-cache.json
    void fallBackToCache();

    ProcessRunner *m_runner;
    Ops m_ops;
    QHash<QString, Job> m_jobs;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_catalogReply = nullptr;
    QTimer *m_catalogTimer = nullptr;
    bool m_catalogTimedOut = false;
    QList<CatalogEntry> m_catalog;
    QString m_catalogError;
    bool m_catalogFromCache = false;
};
