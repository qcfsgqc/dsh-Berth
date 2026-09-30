#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;

// 端口占用者信息；取不到的字段在 describe() 里显示为"未知"（pid 为 0 表示未知）
struct Occupant {
    bool listening = false;  // 端口是否处于监听状态（IPv4 或 IPv6）
    qint64 pid = 0;
    QString name;            // 进程名（可执行文件名）
    QString path;            // 可执行文件完整路径

    // 冲突提示用的一行描述：PID / 进程名 / 路径
    QString describe() const;
};

// 端口检查、占用进程查询、dsh 特征探测、外部进程监视
class PortChecker : public QObject {
    Q_OBJECT

public:
    // nam 为空时按需创建自己的 QNetworkAccessManager
    explicit PortChecker(QObject *parent = nullptr, QNetworkAccessManager *nam = nullptr);
    ~PortChecker() override;

    // 同步查询：GetExtendedTcpTable(TCP_TABLE_OWNER_PID_LISTENER)，IPv4 与 IPv6 都查；只查系统表，远低于 1 秒
    static Occupant check(int port);
    // 进程是否仍存活（OpenProcess(SYNCHRONIZE) + WaitForSingleObject(0)）
    static bool isProcessAlive(qint64 pid);

    // 异步：3 秒内 GET http://127.0.0.1:<port>/manifest.webmanifest，结果经 dshProbed 返回
    void probeDsh(int port);

    // 每秒检查 pid 存活与端口监听，任一失效时发 externalGone(id) 并自动停止监视
    void watchExternal(const QString &id, qint64 pid, int port);
    void unwatch(const QString &id);
    bool isWatching(const QString &id) const { return m_watched.contains(id); }

signals:
    void dshProbed(int port, bool isDsh);
    void externalGone(const QString &id);

private:
    struct Watched {
        qint64 pid = 0;
        int port = 0;
    };

    void tick();
    QNetworkAccessManager *nam();

    QNetworkAccessManager *m_nam = nullptr;  // 外部传入或按需创建（后者以 this 为 parent）
    QTimer *m_timer = nullptr;
    QHash<QString, Watched> m_watched;
};
