#pragma once

#include "Instance.h"
#include "core/SettingsCodec.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class ProcessRunner;

// 环境检测与诊断报告（Env_Checker）。
// 6 项：node / npm / pnpm / git / dsh / webview2。每项异步检测、10 秒超时，互不影响；
// Node 按 `^22.19.0 || >=24.0.0` 判定（dsh 的 engines）；WebView2 查 EdgeUpdate 注册表 pv。
// winget 可用时，"缺失"项可一键安装（QML 先用 installCommand 显示完整命令确认，再调 install）；
// 安装结束后只重测该项。exportReport 写出经 Redact 脱敏的诊断报告（QSaveFile），未检测过时先检测。
class EnvChecker : public QObject {
    Q_OBJECT
    // [{id, name, status, statusText, version, path, minVersion, reason, url, guide,
    //   wingetId, installCommand, canInstall, installing,
    //   hasInstallResult, installOk, installCode, installMessage}]
    // status：unchecked | checking | ok | missing | outdated | failed
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    // 任一项正在检测
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    // 6 项都至少有过一次结果
    Q_PROPERTY(bool checkedOnce READ checkedOnce NOTIFY itemsChanged)
    Q_PROPERTY(bool wingetAvailable READ wingetAvailable NOTIFY wingetChanged)
    Q_PROPERTY(QString wingetVersion READ wingetVersion NOTIFY wingetChanged)
    Q_PROPERTY(bool wingetChecking READ wingetChecking NOTIFY wingetChanged)
    // 正在等待检测完成或写出报告
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)

public:
    struct Ops {
        std::function<QString()> dshExecutable;                  // Settings::resolvedDshExecutable
        std::function<QString()> nodeExecutable;                 // Settings::nodeExecutable（空 = PATH 中的 node）
        std::function<QList<Instance>()> instances;              // 泊位列表（报告摘要与敏感 env 值）
        std::function<QString(const QString &)> statusText;      // 泊位状态文案
        std::function<ProxySettings()> proxy;                    // 代理设置
        std::function<QStringList()> extraSecrets;               // 额外要脱敏的原始值（如解密后的代理密码）
        std::function<QString(const QString &)> lastExitCode;    // 泊位 id → 最近退出码文本；空 = 从未退出
    };

    explicit EnvChecker(ProcessRunner *runner, QObject *parent = nullptr);

    void setOps(Ops ops);

    QVariantList items() const;
    bool checking() const { return m_checking; }
    bool checkedOnce() const;
    bool wingetAvailable() const { return m_wingetAvailable; }
    QString wingetVersion() const { return m_wingetVersion; }
    bool wingetChecking() const { return m_wingetChecking; }
    bool exporting() const { return !m_pendingReport.isEmpty(); }

    // 重新检测全部 6 项并探测 winget（正在进行的旧结果会被丢弃）
    Q_INVOKABLE void checkAll();
    // 只重新检测一项
    Q_INVOKABLE void checkItem(const QString &id);
    // 该项的完整 winget 命令（供确认对话框显示）；不支持一键安装时返回空
    Q_INVOKABLE QString installCommand(const QString &id) const;
    // 确认后调用：开始 winget 安装。返回 {ok, error}；结果经 installFinished 通知，结束后只重测该项。
    // 仅"缺失"且 winget 可用、有对应包 id 且未在安装中时可执行
    Q_INVOKABLE QVariantMap install(const QString &id);
    // 导出诊断报告到 path（本地路径或 file:// URL）。返回 {ok, error, pending}；
    // pending=true 表示先检测，检测完成后写出。结果经 reportExported 通知
    Q_INVOKABLE QVariantMap exportReport(const QString &path);

signals:
    void itemsChanged();
    void checkingChanged();
    void wingetChanged();
    void exportingChanged();
    // ok=false 时 message 含退出码与最后 20 行输出
    void installFinished(const QString &id, bool ok, int exitCode, const QString &message);
    // 写出失败时不留下不完整文件，message 为失败原因
    void reportExported(bool ok, const QString &path, const QString &message);

private:
    struct Item {
        QString id;
        QString name;
        QString status = QStringLiteral("unchecked");
        QString version;
        QString path;
        QString minRange;     // SemVer 范围；空 = 无最低要求
        QString minText;      // 界面显示的最低版本
        QString reason;
        QString url;
        QString guide;
        QString wingetId;
        bool installing = false;
        bool hasInstallResult = false;
        bool installOk = false;
        int installCode = 0;
        QString installMessage;
        int generation = 0;   // 每次重测 +1，丢弃过期结果
    };

    int indexOf(const QString &id) const;
    void startCheck(int index);
    void onVersionResult(const QString &id, int generation, bool ok, int code, const QString &tail,
                         bool timedOut, bool failedToStart, const QString &error);
    void detectWinget();
    QString resolveProgram(const Item &item) const;
    QStringList wingetArgs(const QString &packageId) const;
    void updateChecking();
    void writeReport(const QString &path);
    QString buildReport() const;
    QStringList collectSecrets() const;

    ProcessRunner *m_runner = nullptr;
    Ops m_ops;
    QList<Item> m_items;
    bool m_checking = false;
    bool m_wingetAvailable = false;
    bool m_wingetChecking = false;
    int m_wingetGeneration = 0;
    QString m_wingetVersion;
    QString m_wingetPath;
    QString m_pendingReport;          // 等待检测完成后写出的报告路径
};
