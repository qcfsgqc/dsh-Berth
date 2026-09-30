#include "ProcessRunner.h"

#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {
// 没有换行的超长输出只保留末尾这么多字符，避免尾部缓冲无限增长
constexpr qsizetype kMaxPendingChars = 64 * 1024;
}

// ---------------------------------------------------------------------------
// ProcessTask
// ---------------------------------------------------------------------------

ProcessTask::ProcessTask(int tailLines, QObject *parent)
    : QObject(parent), m_tailLimit(tailLines), m_decoder(QStringConverter::System) {}

ProcessTask::~ProcessTask() {
    // 兜底：无论经由哪条路径销毁，都不留下 Job 句柄
    closeJob();
}

bool ProcessTask::isRunning() const {
    return m_process && m_process->state() != QProcess::NotRunning;
}

QString ProcessTask::tail() const {
    return m_lines.join(QLatin1Char('\n'));
}

void ProcessTask::start(const QString &program, const QStringList &args, const QProcessEnvironment &env,
                        const QString &cwd, int timeoutMs) {
    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments(args);
    m_process->setProcessEnvironment(env);
    if (!cwd.isEmpty())
        m_process->setWorkingDirectory(cwd);
    // stdout/stderr 合并，尾部摘要按实际输出顺序
    m_process->setProcessChannelMode(QProcess::MergedChannels);

#ifdef Q_OS_WIN
    // 不弹控制台黑窗
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *procArgs) {
        procArgs->flags |= CREATE_NO_WINDOW;
        procArgs->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        procArgs->startupInfo->wShowWindow = SW_HIDE;
    });
#endif

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        appendOutput(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        appendOutput(m_process->readAllStandardOutput());
        flushPending();
        const bool crashed = status == QProcess::CrashExit;
        if (m_timedOut)
            m_errorString = QStringLiteral("超时");
        else if (crashed)
            m_errorString = m_process->errorString();
        finish(!m_timedOut && !crashed && code == 0, code);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        // 其它错误（崩溃、读写失败）之后仍会有 finished，统一在那里收尾
        if (err != QProcess::FailedToStart)
            return;
        m_failedToStart = true;
        m_errorString = m_process->errorString();
        // FailedToStart 可能在 start() 内同步发出，此时调用方还没来得及连接 finished；
        // 排队到事件循环里再通知
        QMetaObject::invokeMethod(this, [this]() { finish(false, -1); }, Qt::QueuedConnection);
    });

    m_process->start();
    if (m_process->state() == QProcess::NotRunning)
        return;
    attachJob();
    if (timeoutMs > 0) {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, &ProcessTask::onTimeout);
        m_timer->start(timeoutMs);
    }
}

void ProcessTask::appendOutput(const QByteArray &bytes) {
    if (bytes.isEmpty())
        return;
    // 有状态解码：跨块截断的多字节字符留到下一块拼接
    const QString chunk = m_decoder.decode(bytes);
    m_pending += chunk;
    qsizetype from = 0;
    for (;;) {
        const qsizetype nl = m_pending.indexOf(QLatin1Char('\n'), from);
        if (nl < 0)
            break;
        pushLine(m_pending.mid(from, nl - from));
        from = nl + 1;
    }
    m_pending.remove(0, from);
    if (m_pending.size() > kMaxPendingChars)
        m_pending = m_pending.right(kMaxPendingChars);
}

void ProcessTask::pushLine(QString line) {
    if (line.endsWith(QLatin1Char('\r')))
        line.chop(1);
    // 与原先 split("\r?\n", SkipEmptyParts) 一致：空行不计入
    if (line.isEmpty() || m_tailLimit <= 0)
        return;
    m_lines.append(line);
    while (m_lines.size() > m_tailLimit)
        m_lines.removeFirst();
}

void ProcessTask::flushPending() {
    if (m_pending.isEmpty())
        return;
    pushLine(m_pending);
    m_pending.clear();
}

void ProcessTask::finish(bool ok, int code) {
    if (m_done)
        return;
    m_done = true;
    if (m_timer)
        m_timer->stop();
    // 主进程已结束：关闭 Job，顺手清掉它留下的子孙进程
    closeJob();
    emit finished(ok, code, tail(), m_timedOut);
}

void ProcessTask::onTimeout() {
    if (m_done || !isRunning())
        return;
    m_timedOut = true;
    kill();
}

void ProcessTask::kill() {
    terminateJob();
    if (isRunning())
        m_process->kill();
}

void ProcessTask::abandon() {
    m_done = true;
    disconnect();
    if (m_timer)
        m_timer->stop();
    if (!m_process)
        return;
    m_process->disconnect(this);
    // KILL_ON_JOB_CLOSE：关句柄即连带结束整棵进程树
    terminateJob();
    closeJob();
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

void ProcessTask::attachJob() {
#ifdef Q_OS_WIN
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    HANDLE handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                static_cast<DWORD>(m_process->processId()));
    if (handle && AssignProcessToJobObject(job, handle))
        m_job = job;
    else
        CloseHandle(job);
    if (handle)
        CloseHandle(handle);
#endif
}

void ProcessTask::terminateJob() {
#ifdef Q_OS_WIN
    if (m_job)
        TerminateJobObject(static_cast<HANDLE>(m_job), 1);
#endif
}

void ProcessTask::closeJob() {
#ifdef Q_OS_WIN
    if (m_job)
        CloseHandle(static_cast<HANDLE>(m_job));
#endif
    m_job = nullptr;
}

// ---------------------------------------------------------------------------
// ProcessRunner
// ---------------------------------------------------------------------------

ProcessRunner::ProcessRunner(QObject *parent) : QObject(parent) {}

ProcessRunner::~ProcessRunner() {
    // 析构时不再回调任何人，只管关闭全部 Job 句柄、把进程树杀干净
    const QList<ProcessTask *> tasks = m_tasks;
    m_tasks.clear();
    for (ProcessTask *task : tasks)
        task->abandon();
}

void ProcessRunner::setEnvHook(EnvHook hook) {
    m_envHook = std::move(hook);
}

ProcessTask *ProcessRunner::run(const QString &program, const QStringList &args,
                                const ProcessOptions &options) {
    auto *task = new ProcessTask(options.tailLines, this);
    m_tasks.append(task);
    // 先于调用方连接：收尾时从列表移除，调用方的槽执行完后再销毁
    connect(task, &ProcessTask::finished, this, [this, task]() {
        m_tasks.removeOne(task);
        task->deleteLater();
    });

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (m_envHook) {
        const QHash<QString, QString> injected = m_envHook();
        for (auto it = injected.constBegin(); it != injected.constEnd(); ++it)
            env.insert(it.key(), it.value());
    }
    for (auto it = options.env.constBegin(); it != options.env.constEnd(); ++it)
        env.insert(it.key(), it.value());

    task->start(program, args, env, options.cwd, options.timeoutMs);
    return task;
}
