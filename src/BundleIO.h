#pragma once

#include "Instance.h"
#include "core/BundleCodec.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class ProcessRunner;

// 泊位 / profile 的整包导出与导入（.berthbundle，格式见 core/BundleCodec）。
// - 读取源文件、编码、写出、解码、写入 profile 文件都在线程池执行，结果排队回主线程
// - 导出用 QSaveFile 写出：任一步失败不在目标位置留下不完整文件
// - 导入流程：inspect（校验 + 冲突检测，不写任何数据）→ importBundle（按 resolution 复核冲突 →
//   写入 profile 文件 → 恢复插件启用状态 → 泊位类型时新增泊位 → dsh plugin install 重建依赖）
// - 依赖重建失败：已写入的文件与泊位保留，该 (home, profile) 记入 depsPending（"依赖未就绪"），
//   retryDeps 重试成功后移除
// 同一时刻只允许一项导出/导入/重试。
class BundleIO : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // 依赖未就绪：key(= home + "|" + profile) → {key, home, profile, instanceId, error, tail}
    Q_PROPERTY(QVariantMap depsPending READ depsPending NOTIFY depsPendingChanged)

public:
    struct Ops {
        // 空串 → 默认 DSH_HOME；非空 → 规范化后的绝对路径
        std::function<QString(const QString &)> resolveHome;
        std::function<QList<Instance>()> instances;
        std::function<QString()> dshExecutable;
        // 端口是否处于监听（PortChecker::check(port).listening）
        std::function<bool(int)> isListening;
        // 新增泊位（调用方补 id / logPath / status、写 instances.json），返回新 id；失败返回空
        std::function<QString(const Instance &)> addInstance;
        // 导入写入了新 profile 目录后调用（刷新 profile 列表与树）
        std::function<void()> profilesChanged;
        std::function<QString()> berthVersion;
    };

    explicit BundleIO(ProcessRunner *runner, QObject *parent = nullptr);
    void setOps(Ops ops) { m_ops = std::move(ops); }

    bool busy() const { return m_busy; }
    QVariantMap depsPending() const { return m_depsPending; }

    // —— 导出 ——（path 可为本地路径或 file: URL）
    // 返回 {ok, error}：ok=false 表示被拒绝、未开始；开始后结束时发 exportFinished
    Q_INVOKABLE QVariantMap exportInstance(const QString &id, const QString &path, bool includeSecrets);
    Q_INVOKABLE QVariantMap exportProfile(const QString &home, const QString &profile, const QString &path);
    // 该泊位是否有标记为敏感的环境变量（界面据此显示"包含敏感值"勾选项）
    Q_INVOKABLE bool hasSecrets(const QString &id) const;

    // —— 导入 ——
    // 异步读取并校验 Bundle，检测目标 home 下的冲突，不写任何数据 → inspected(result)：
    // {ok, error, path, home, type, profileName, profileConflict, profileNameError,
    //  port, portConflict, portUsers, suggestedName, suggestedPort, missingSecrets, pluginCount, fileCount}
    // （type 为 profile 时 port 相关字段为 0 / false / 空）
    Q_INVOKABLE QVariantMap inspect(const QString &path, const QString &home);
    // resolution：{profileName?: 新名称（缺省用 Bundle 中的名称）, port?: 端口（缺省用 Bundle 中的端口）,
    //              autoPort?: true 时自动分配未被任何泊位使用且未监听的端口}
    // 冲突未解决时不写任何数据，importFinished(ok=false)。返回 {ok, error} 同导出
    Q_INVOKABLE QVariantMap importBundle(const QString &path, const QString &home, const QVariantMap &resolution);
    // 重试依赖重建（key 取自 depsPending）→ depsRetryFinished
    Q_INVOKABLE QVariantMap retryDeps(const QString &key);

    // 冲突对话框辅助：名称是否可用（合法且目标目录不存在），返回空串表示可用，否则为原因
    Q_INVOKABLE QString checkProfileName(const QString &home, const QString &name) const;
    // 从 from 之后找未被任何泊位使用且未监听的端口；找不到返回 0
    Q_INVOKABLE int suggestPort(int from) const;

signals:
    void busyChanged();
    void depsPendingChanged();
    // ok=true：message 为目标路径；ok=false：message 为"导出失败：<原因>"
    void exportFinished(bool ok, const QString &message);
    void inspected(const QVariantMap &result);
    // result：{ok, error, type, home, profile, instanceId, missingSecrets, depsReady, depsKey, depsError, tail}
    // ok=false 时未创建或修改任何 profile / 泊位；ok=true 且 depsReady=false 时已记入 depsPending
    void importFinished(bool ok, const QString &message, const QVariantMap &result);
    void depsRetryFinished(const QString &key, bool ok, const QString &message);

private:
    struct DepsJob {
        QString key;
        QString home;
        QString profile;
        QString instanceId;
    };

    static QString localPath(const QString &path);
    QString profileDir(const QString &home, const QString &profile) const;
    QString depsKey(const QString &home, const QString &profile) const;
    void setBusy(bool on);
    void startExport(const QString &dir, const QString &dest, BundleCodec::Bundle base,
                     const QString &skipAbs, bool includeSecrets);
    QVariantMap conflictInfo(const BundleCodec::Bundle &bundle, const QString &home) const;
    void finishImportWrite(const BundleCodec::Bundle &bundle, const QString &home, const QString &profile,
                           int port);
    // 运行 dsh plugin --profile <p> install；done(ok, message, tail)
    void runDeps(const DepsJob &job, std::function<void(bool, const QString &, const QString &)> done);

    ProcessRunner *m_runner;
    Ops m_ops;
    bool m_busy = false;
    QVariantMap m_depsPending;
    QHash<QString, DepsJob> m_depsJobs;
};
