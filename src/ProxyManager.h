#pragma once

// 代理设置、自动探测、测试连接、npm 镜像与 DPAPI 密码（需求 16）。
// - 已保存设置经 Settings::setProxyData 写入；保存后立即应用到共享 QNetworkAccessManager，
//   ProcessRunner 每次 run 经 childEnv() 取环境变量，因此只对新请求、新子进程生效
// - 明文密码只在内存里（m_password），settings.json 中只有 passwordDpapi
// - 探测与测试连接都不修改已保存设置

#include "core/SettingsCodec.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QNetworkProxy>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpSocket;
class QTimer;
class Settings;

class ProxyManager : public QObject {
    Q_OBJECT
    // —— 已保存设置（只读；修改经 save(draft)） ——
    Q_PROPERTY(QString mode READ mode NOTIFY changed)             // none | system | manual
    Q_PROPERTY(QString scheme READ scheme NOTIFY changed)         // http | https | socks5
    Q_PROPERTY(QString host READ host NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)                 // 0 表示未填
    Q_PROPERTY(QString user READ user NOTIFY changed)
    // 已存有密码（界面据此显示固定长度掩码，不回显明文）
    Q_PROPERTY(bool hasPassword READ hasPassword NOTIFY changed)
    Q_PROPERTY(QString registryKind READ registryKind NOTIFY changed) // official | preset | custom
    Q_PROPERTY(QString registryUrl READ registryUrl NOTIFY changed)   // 所存地址（official 时可为空）
    Q_PROPERTY(QString effectiveRegistry READ registry NOTIFY changed) // 实际使用的 registry
    Q_PROPERTY(bool mirrorFallback READ mirrorFallback NOTIFY changed)
    Q_PROPERTY(bool inheritProxy READ inheritProxy NOTIFY changed)
    // 预置镜像 [{name, url}]
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    // DPAPI 解密失败时的提示（已清空凭据）；保存新密码后清空
    Q_PROPERTY(QString credentialNotice READ credentialNotice NOTIFY credentialNoticeChanged)
    // —— 自动探测 ——
    Q_PROPERTY(bool detecting READ detecting NOTIFY detectingChanged)
    // [{scheme, host, port, source, sourceText}]；source 为 system | probe
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY candidatesChanged)
    // —— 测试连接 ——
    Q_PROPERTY(bool testing READ testing NOTIFY testingChanged)
    // [{target: npm|github, name, url, ok, ms, status, category, categoryText, error}]
    // category：ok | timeout | refused | proxyAuth | other
    Q_PROPERTY(QVariantList testResults READ testResults NOTIFY testResultsChanged)

public:
    ProxyManager(Settings *settings, QNetworkAccessManager *network, QObject *parent = nullptr);
    ~ProxyManager() override;

    QString mode() const;
    QString scheme() const;
    QString host() const;
    int port() const;
    QString user() const;
    bool hasPassword() const;
    QString registryKind() const;
    QString registryUrl() const;
    bool mirrorFallback() const;
    bool inheritProxy() const;
    QVariantList presets() const;
    QString credentialNotice() const { return m_credentialNotice; }
    bool detecting() const { return m_detecting; }
    QVariantList candidates() const { return m_candidates; }
    bool testing() const { return m_testing; }
    QVariantList testResults() const { return m_testResults; }

    // 保存界面草稿。draft 键：
    //   mode, scheme, host, port（数字或文本）, user,
    //   password（可选：缺省表示沿用已存密码；空串表示清除）,
    //   registryKind, registryUrl, mirrorFallback, inheritProxy
    // 返回 {ok, errors:[非法字段名: host|port|registryUrl], error}；ok=false 时已保存设置不变
    Q_INVOKABLE QVariantMap save(const QVariantMap &draft);
    // 只校验不保存，返回值同 save
    Q_INVOKABLE QVariantMap validate(const QVariantMap &draft) const;
    // 自动探测：读系统代理 + 逐个 TCP 探测 127.0.0.1 的常见端口（每个 ≤1 秒，整次 <10 秒）。
    // 结束时发 detectFinished；不修改已保存设置
    Q_INVOKABLE void detect();
    // 用草稿（含未保存修改）分别访问 npm registry 与 GitHub API（各 10 秒）；结果写 testResults 并发 testFinished
    Q_INVOKABLE void test(const QVariantMap &draft);

    // —— 供其它组件使用 ——
    // 已保存设置对应的 QNetworkProxy（none 或系统代理未启用时为 NoProxy）
    QNetworkProxy proxy() const;
    // 当前代理 URL（含凭据；为空表示不使用代理）。不要写入日志，需要时经 Redact::redactProxyUrl
    QString proxyUrl() const;
    // 传给 LaunchEnv 的代理环境变量（不含 registry）；不使用代理时为空
    QHash<QString, QString> proxyEnv() const;
    // 传给 ProcessRunner 的子进程环境：代理 + npm_config_registry
    QHash<QString, QString> childEnv() const;
    // 当前实际使用的 registry
    QString registry() const;
    // 镜像兜底用的镜像：开启兜底且当前使用官方源时返回所配置镜像（未配置则取第一个预置镜像），否则为空
    QString fallbackRegistry() const;
    // 需脱敏的原始值（解密后的密码、用户名），供诊断报告
    QStringList secrets() const;

    // 系统代理（HKCU Internet Settings）；未启用或无法解析时 enabled=false
    struct SystemProxy {
        bool enabled = false;
        QString scheme = QStringLiteral("http");
        QString host;
        int port = 0;
    };
    static SystemProxy readSystemProxy();

    // DPAPI（当前用户范围）加密/解密，密文为 base64；protect 失败返回空串，unprotect 失败返回 false
    static QString protect(const QString &plain);
    static bool unprotect(const QString &cipher, QString *plain);

signals:
    void changed();
    void credentialNoticeChanged();
    void detectingChanged();
    void candidatesChanged();
    void detectFinished(bool found, const QString &message);
    void testingChanged();
    void testResultsChanged();
    void testFinished();

private:
    struct Draft {
        ProxySettings proxy;
        QString portText;
        bool passwordGiven = false;
        QString password;
        NpmRegistrySettings registry;
        bool mirrorFallback = false;
        bool inheritProxy = false;
    };
    Draft parseDraft(const QVariantMap &draft) const;
    QStringList invalidFields(const Draft &d) const;
    // 由设置与明文密码得到代理 URL / QNetworkProxy
    static QString urlFor(const ProxySettings &p, const QString &password);
    static QNetworkProxy proxyFor(const ProxySettings &p, const QString &password);

    // 设置变化时：解密密码（失败则清空凭据）、应用到共享 QNAM
    void reload();
    void apply();

    void probeNext();
    void finishDetect();

    void startTestRequest(int index, const QString &url);
    void onTestReply(int index, QNetworkReply *reply, bool timedOut, qint64 ms);

    Settings *m_settings;
    QNetworkAccessManager *m_network;
    QString m_password; // DPAPI 解密后的明文，只在内存中
    QString m_credentialNotice;

    bool m_detecting = false;
    QVariantList m_candidates;
    QList<int> m_probePorts;
    QTcpSocket *m_probeSocket = nullptr; // 当前探测中的连接；回调据此忽略已结束的旧连接
    QElapsedTimer m_detectClock;

    bool m_testing = false;
    int m_testPending = 0;
    QVariantList m_testResults;
    QNetworkAccessManager *m_testNetwork = nullptr;
};
