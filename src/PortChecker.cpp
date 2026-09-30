#include "PortChecker.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QtEndian>

#include <vector>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <iphlpapi.h>
#endif

namespace {
const QString kUnknown = QStringLiteral("未知");

#ifdef Q_OS_WIN
// dwLocalPort 低 16 位是网络字节序
quint16 tablePort(DWORD raw) {
    return qFromBigEndian<quint16>(static_cast<quint16>(raw & 0xFFFF));
}

// 拉取一张 TCP 监听表（af = AF_INET / AF_INET6）；缓冲区不够时按返回的大小重试
bool fetchTable(ULONG af, std::vector<unsigned char> &buffer) {
    DWORD size = 0;
    DWORD rc = ERROR_INSUFFICIENT_BUFFER;
    for (int attempt = 0; attempt < 4 && rc == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
        buffer.resize(size > 0 ? size : 16 * 1024);
        size = static_cast<DWORD>(buffer.size());
        rc = GetExtendedTcpTable(buffer.data(), &size, FALSE, af, TCP_TABLE_OWNER_PID_LISTENER, 0);
    }
    return rc == NO_ERROR;
}

// 在监听表里找端口，返回是否找到；pid 写入 *pid
bool findListener(int port, DWORD *pid) {
    std::vector<unsigned char> buffer;
    if (fetchTable(AF_INET, buffer)) {
        const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            if (tablePort(table->table[i].dwLocalPort) == port) {
                *pid = table->table[i].dwOwningPid;
                return true;
            }
        }
    }
    if (fetchTable(AF_INET6, buffer)) {
        const auto *table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            if (tablePort(table->table[i].dwLocalPort) == port) {
                *pid = table->table[i].dwOwningPid;
                return true;
            }
        }
    }
    return false;
}

QString imagePath(DWORD pid) {
    if (pid == 0)
        return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t buffer[MAX_PATH * 4];
    DWORD length = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &length))
        path = QString::fromWCharArray(buffer, static_cast<int>(length));
    CloseHandle(process);
    return path;
}
#endif
} // namespace

QString Occupant::describe() const {
    return QStringLiteral("PID %1，进程 %2，路径 %3")
        .arg(pid > 0 ? QString::number(pid) : kUnknown,
             name.isEmpty() ? kUnknown : name,
             path.isEmpty() ? kUnknown : path);
}

PortChecker::PortChecker(QObject *parent, QNetworkAccessManager *nam)
    : QObject(parent), m_nam(nam) {}

PortChecker::~PortChecker() = default;

Occupant PortChecker::check(int port) {
    Occupant occupant;
    if (port < 1 || port > 65535)
        return occupant;
#ifdef Q_OS_WIN
    DWORD pid = 0;
    if (!findListener(port, &pid))
        return occupant;
    occupant.listening = true;
    occupant.pid = pid;
    occupant.path = imagePath(pid);
    if (!occupant.path.isEmpty())
        occupant.name = QFileInfo(occupant.path).fileName();
#endif
    return occupant;
}

bool PortChecker::isProcessAlive(qint64 pid) {
    if (pid <= 0)
        return false;
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        // 没有权限打开说明进程仍存在（如提权进程）；其它错误（参数无效）视为已退出
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    const DWORD wait = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait == WAIT_TIMEOUT;
#else
    return false;
#endif
}

QNetworkAccessManager *PortChecker::nam() {
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        // 只访问本机，绕开系统代理
        m_nam->setProxy(QNetworkProxy::NoProxy);
    }
    return m_nam;
}

void PortChecker::probeDsh(int port) {
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/manifest.webmanifest").arg(port)));
    request.setTransferTimeout(3000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = nam()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, port]() {
        reply->deleteLater();
        bool isDsh = false;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && code == 200) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                isDsh = obj.value(QStringLiteral("short_name")).toString() == QLatin1String("DSH")
                    || obj.value(QStringLiteral("name")).toString() == QLatin1String("DeepSeek Harness");
            }
        }
        emit dshProbed(port, isDsh);
    });
}

void PortChecker::watchExternal(const QString &id, qint64 pid, int port) {
    if (id.isEmpty())
        return;
    m_watched.insert(id, Watched{pid, port});
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(1000);
        connect(m_timer, &QTimer::timeout, this, &PortChecker::tick);
    }
    if (!m_timer->isActive())
        m_timer->start();
}

void PortChecker::unwatch(const QString &id) {
    m_watched.remove(id);
    if (m_watched.isEmpty() && m_timer)
        m_timer->stop();
}

void PortChecker::tick() {
    const auto ids = m_watched.keys();
    for (const QString &id : ids) {
        const auto it = m_watched.constFind(id);
        if (it == m_watched.constEnd())
            continue;
        const Watched w = it.value();
        // pid 未知（0）时只看端口
        const bool alive = w.pid <= 0 || isProcessAlive(w.pid);
        const bool listening = check(w.port).listening;
        if (alive && listening)
            continue;
        m_watched.remove(id);
        emit externalGone(id);
    }
    if (m_watched.isEmpty() && m_timer)
        m_timer->stop();
}
