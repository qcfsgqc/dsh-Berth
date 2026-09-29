#include "Supervisor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {
// 往实例日志里追加一行 Berth 自己的标记，终端面板据此区分每次运行
void appendMarker(const QString &logPath, const QString &message) {
    if (logPath.isEmpty())
        return;
    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    const QString line = QStringLiteral("[Berth %1] %2\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), message);
    file.write(line.toUtf8());
}
}

Supervisor::Supervisor(QObject *parent) : QObject(parent) {}

Supervisor::~Supervisor() {
    // 析构时不再发 statusChanged，只管把进程树杀干净
    const auto ids = m_processes.keys();
    for (const QString &id : ids)
        killTree(id, m_processes.take(id));
    const auto jobIds = m_jobs.keys();
    for (const QString &id : jobIds)
        releaseJob(id);
}

void Supervisor::start(const Instance &instance, const QString &dshExecutable) {
    if (m_processes.contains(instance.id))
        return;

    auto *process = new QProcess(this);
    process->setProgram(dshExecutable.isEmpty() ? QStringLiteral("dsh") : dshExecutable);
    process->setArguments({
        // dsh --profile <name> 启动指定 profile；其后首个启动器不认识的参数起，全部交给 web app
        QStringLiteral("--profile"), instance.profile.isEmpty() ? QStringLiteral("web") : instance.profile,
        QStringLiteral("--port"), QString::number(instance.port),
        QStringLiteral("--no-open")
    });
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!instance.dshHome.isEmpty())
        env.insert(QStringLiteral("DSH_HOME"), instance.dshHome);
    process->setProcessEnvironment(env);
    if (!instance.workspace.isEmpty())
        process->setWorkingDirectory(instance.workspace);

    const QString logPath = instance.logPath;
    QDir().mkpath(QFileInfo(logPath).absolutePath());
    process->setStandardOutputFile(logPath, QIODevice::Append);
    process->setStandardErrorFile(logPath, QIODevice::Append);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    appendMarker(logPath, QStringLiteral("启动: %1 %2").arg(process->program(), process->arguments().join(QLatin1Char(' '))));

#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
#endif

    connect(process, &QProcess::errorOccurred, this, [this, process, logPath, id = instance.id](QProcess::ProcessError) {
        const QString error = process->errorString();
        appendMarker(logPath, QStringLiteral("错误: %1").arg(error));
        emit statusChanged(id, QStringLiteral("failed"), 0, error);
    });
    connect(process, &QProcess::finished, this, [this, logPath, id = instance.id](int code, QProcess::ExitStatus status) {
        appendMarker(logPath, status == QProcess::CrashExit
            ? QStringLiteral("进程崩溃")
            : QStringLiteral("进程退出，exit %1").arg(code));
        m_processes.remove(id);
        // dsh 自己退出时，顺手清掉它留下的子进程
        releaseJob(id);
        if (auto *timer = m_probes.take(id))
            timer->deleteLater();
        const QString error = status == QProcess::CrashExit
            ? QStringLiteral("process crashed")
            : (code == 0 ? QString() : QStringLiteral("exit %1").arg(code));
        emit statusChanged(id, code == 0 ? QStringLiteral("stopped") : QStringLiteral("failed"), 0, error);
    });

    emit statusChanged(instance.id, QStringLiteral("starting"), 0, {});
    process->start();
    if (!process->waitForStarted(5000)) {
        const QString error = process->errorString();
        process->deleteLater();
        emit statusChanged(instance.id, QStringLiteral("failed"), 0, error);
        return;
    }
    process->setProperty("berthLogPath", logPath);
    m_processes.insert(instance.id, process);
    attachJob(instance.id, process);
    emit statusChanged(instance.id, QStringLiteral("starting"), process->processId(), {});
    probe(instance.id, instance.port);
}

void Supervisor::stop(const QString &id) {
    auto *process = m_processes.take(id);
    if (auto *timer = m_probes.take(id))
        timer->deleteLater();
    if (!process) {
        releaseJob(id);
        return;
    }
    emit statusChanged(id, QStringLiteral("stopping"), process->processId(), {});
    const QString logPath = process->property("berthLogPath").toString();
    killTree(id, process);
    appendMarker(logPath, QStringLiteral("已手动停止"));
    emit statusChanged(id, QStringLiteral("stopped"), 0, {});
}

void Supervisor::attachJob(const QString &id, QProcess *process) {
#ifdef Q_OS_WIN
    // KILL_ON_JOB_CLOSE：句柄关闭（包括 Berth 崩溃/被杀）时，系统连带结束整棵进程树
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    HANDLE handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                static_cast<DWORD>(process->processId()));
    if (handle && AssignProcessToJobObject(job, handle))
        m_jobs.insert(id, job);
    else
        CloseHandle(job);
    if (handle)
        CloseHandle(handle);
#else
    Q_UNUSED(id);
    Q_UNUSED(process);
#endif
}

void Supervisor::releaseJob(const QString &id) {
#ifdef Q_OS_WIN
    if (void *job = m_jobs.take(id))
        CloseHandle(static_cast<HANDLE>(job));
#else
    Q_UNUSED(id);
#endif
}

void Supervisor::killTree(const QString &id, QProcess *process) {
    if (!process) {
        releaseJob(id);
        return;
    }
    // 先断开信号，避免主动停止被 finished 回调误报成 failed
    process->disconnect(this);
#ifdef Q_OS_WIN
    // dsh 可能是 .cmd 包装，真正占端口的是子进程 node；只杀直接子进程会留下孤儿
    if (void *job = m_jobs.take(id)) {
        TerminateJobObject(static_cast<HANDLE>(job), 1);
        CloseHandle(static_cast<HANDLE>(job));
    }
#endif
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(3000);
    }
    process->deleteLater();
}

bool Supervisor::isRunning(const QString &id) const {
    return m_processes.contains(id);
}

void Supervisor::probe(const QString &id, int port) {
    auto *timer = new QTimer(this);
    timer->setInterval(500);
    int attempts = 0;
    connect(timer, &QTimer::timeout, this, [this, id, port, timer, attempts]() mutable {
        if (!m_processes.contains(id)) {
            timer->stop();
            return;
        }
        QTcpSocket socket;
        socket.connectToHost(QStringLiteral("127.0.0.1"), port);
        if (socket.waitForConnected(200)) {
            socket.disconnectFromHost();
            timer->stop();
            m_probes.remove(id);
            timer->deleteLater();
            emit statusChanged(id, QStringLiteral("running"), m_processes.value(id)->processId(), {});
            return;
        }
        if (++attempts >= 40) {
            timer->stop();
            m_probes.remove(id);
            timer->deleteLater();
            emit statusChanged(id, QStringLiteral("failed"), m_processes.value(id)->processId(),
                               QStringLiteral("port %1 did not open").arg(port));
        }
    });
    m_probes.insert(id, timer);
    timer->start();
}
