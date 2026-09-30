#pragma once

#include "DshUpdate.h"
#include "InstanceModel.h"
#include "ProfileTreeModel.h"
#include "Settings.h"
#include "Supervisor.h"

#include <QObject>

class QProcess;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(InstanceModel *instances READ instances CONSTANT)
    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString defaultHome READ defaultDshHome NOTIFY profilesChanged)
    Q_PROPERTY(QStringList knownProfiles READ knownProfiles NOTIFY profilesChanged)
    Q_PROPERTY(ProfileTreeModel *profileTree READ profileTree CONSTANT)
    Q_PROPERTY(bool profileBusy READ profileBusy NOTIFY profileBusyChanged)
    Q_PROPERTY(DshUpdate *dshUpdate READ dshUpdate CONSTANT)
    Q_PROPERTY(bool pluginBusy READ pluginBusy NOTIFY pluginBusyChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    InstanceModel *instances();
    Settings *settings();
    QString version() const;

    Q_INVOKABLE QString createInstance();
    // 从已有泊位复制配置新建（端口自动避让，id/状态/日志路径重新生成）
    Q_INVOKABLE QString createInstanceFrom(const QString &sourceId);
    Q_INVOKABLE void removeInstance(const QString &id);
    Q_INVOKABLE void updateInstance(const QString &id, const QString &name, int port,
                                    const QString &profile, const QString &dshHome,
                                    const QString &workspace, bool autostart);
    Q_INVOKABLE void startInstance(const QString &id);
    Q_INVOKABLE void stopInstance(const QString &id);
    Q_INVOKABLE void restartInstance(const QString &id);
    // 在内嵌 WebView2 窗口里打开（发 uiRequested 给 QML）
    Q_INVOKABLE void openUi(const QString &id);
    Q_INVOKABLE void openInBrowser(const QString &id);
    // 带 token 的界面地址；本次运行还没打印时退回 http://127.0.0.1:<port>/
    Q_INVOKABLE QString uiUrl(const QString &id) const;
    Q_INVOKABLE void openLog(const QString &id);
    // 读取日志尾部（默认 64KB），供终端面板显示
    Q_INVOKABLE QString readLog(const QString &id, int maxBytes = 65536) const;

    Q_INVOKABLE QString statusText(const QString &status) const;
    Q_INVOKABLE QVariantMap instance(const QString &id) const;
    Q_INVOKABLE QString defaultDshHome() const;
    Q_INVOKABLE QStringList detectProfiles(const QString &dshHome) const;
    Q_INVOKABLE int importDetectedProfiles();
    // —— profile 管理 ——
    // profile 信息：{ok, error, name, dir, exists, bundles, pluginCount}
    Q_INVOKABLE QVariantMap profileInfo(const QString &home, const QString &name) const;
    // 从模板创建 profile（dsh --profile <n> --from-default-profile <t> --dump-config，boot-free），异步
    Q_INVOKABLE void createProfile(const QString &name, const QString &fromTemplate);
    // 删除 profile 目录（被任何泊位引用时拒绝）
    Q_INVOKABLE void deleteProfile(const QString &home, const QString &name);
    // 重命名 profile 目录并同步更新引用它的泊位
    Q_INVOKABLE void renameProfile(const QString &home, const QString &oldName, const QString &newName);
    // 复制配置文件 + pnpm install 重建依赖，异步
    Q_INVOKABLE void copyProfile(const QString &home, const QString &srcName, const QString &newName);
    // 为指定 profile 新建泊位（默认 home 时 dshHome 留空），返回新泊位 id
    Q_INVOKABLE QString createInstanceFor(const QString &home, const QString &profile);
    Q_INVOKABLE void openProfileDir(const QString &home, const QString &name);
    // 按 (home, profile) 直接读写插件，不经过泊位
    Q_INVOKABLE QVariantMap listPlugins(const QString &id) const;
    Q_INVOKABLE QVariantMap listPluginsForProfile(const QString &home, const QString &profile) const;
    Q_INVOKABLE void uninstallPluginForProfile(const QString &home, const QString &profile,
                                               const QString &name, bool removePackage);
    Q_INVOKABLE void uninstallPlugin(const QString &id, const QString &name, bool removePackage);
    QStringList knownProfiles() const;
    ProfileTreeModel *profileTree() { return &m_profileTree; }
    DshUpdate *dshUpdate() { return &m_dshUpdate; }
    bool profileBusy() const;
    bool pluginBusy() const;

signals:
    void notice(const QString &message);
    void uiRequested(const QString &id, const QString &url);
    void profilesChanged();
    void profileBusyChanged();
    void profileOpFinished(bool ok, const QString &message, const QString &profileName);
    void pluginBusyChanged();
    void pluginUninstallFinished(const QString &id, bool ok, const QString &message);

private:
    void load();
    void save() const;
    QString instancesPath() const;
    void applyStatus(const QString &id, const QString &status, qint64 pid, const QString &error);
    // 从日志里取本次运行 dsh web 打印的 URL（含 token），没有则返回空
    QString tokenUrl(const QString &id) const;
    // token 行可能比端口就绪晚一点，短暂轮询后再发 uiRequested
    void requestUi(const QString &id, int attempt);

    QString resolveHome(const QString &dshHome) const;
    // 泊位 id → <DSH_HOME>/profiles/<profile>；找不到泊位时返回空并写 error
    QString profileDirFor(const QString &id, QString *error) const;
    void refreshProfiles();
    void setPluginBusy(bool busy);
    void setProfileBusy(bool busy);
    // profile 目录路径：home 不给时用默认 home
    QString profileDirForHome(const QString &home, const QString &profile) const;
    // 全量重建泊位树（home 归一 + 目录存在性 + knownProfiles 合并）
    void rebuildTree();
    // 卸载插件共用流程：id 为空表示按 (home, profile) 操作（不经泊位）
    void uninstallFromProfile(const QString &id, const QString &home, const QString &profile,
                              const QString &name, bool removePackage);
    // 插件/profile 包操作的异步 QProcess 收尾（CREATE_NO_WINDOW + 摘要 + busy 复位）
    void startProfileProcess(const QString &program, const QStringList &args, const QString &dshHome,
                             const QString &workingDir, const QString &failPrefix,
                             const QString &finishProfileName);

    InstanceModel m_instances;
    Settings m_settings;
    Supervisor m_supervisor;
    ProfileTreeModel m_profileTree;
    DshUpdate m_dshUpdate;
    QStringList m_knownProfiles;
    QProcess *m_pluginProcess = nullptr;
    QProcess *m_profileProcess = nullptr;
    QString m_profileFinishName;
    bool m_pluginBusy = false;
    bool m_profileBusy = false;
};
