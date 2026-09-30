#include "LogService.h"

#include "core/BatchPlan.h"
#include "core/Redact.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>

namespace {
constexpr int kPollMs = 2000;
constexpr qint64 kAppendCap = LogText::kChunkBytes * 16; // 单次追加读取上限，余下的下次刷新再读

// 在线程池执行 work；done 排队回主线程执行，且仅在 self 仍存在时执行
template <typename Result>
void runAsync(LogService *owner, std::function<Result()> work, std::function<void(const Result &)> done) {
    QPointer<LogService> self(owner);
    QThreadPool::globalInstance()->start([self, work = std::move(work), done = std::move(done)]() {
        Result result = work();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, done, result]() {
                if (self)
                    done(result);
            },
            Qt::QueuedConnection);
    });
}

bool readRange(QFile &file, qint64 start, qint64 len, QByteArray *out, QString *error) {
    if (!file.seek(start)) {
        *error = file.errorString();
        return false;
    }
    *out = file.read(len);
    if (out->size() != len) {
        *error = file.error() != QFileDevice::NoError ? file.errorString()
                                                        : QStringLiteral("读取到的字节数与预期不符");
        return false;
    }
    return true;
}

struct TailResult {
    QString error;
    bool exists = false;
    qint64 size = 0;
    bool baseReset = false;      // 清空偏移已超出文件大小（文件被截断过），按 0 处理
    LogText::TailCursor cursor;  // 相对 base 的坐标
    QByteArray bytes;            // 应显示（尾部加载）或前置（加载更多）的内容
};

// 从 cursor 起向前读取，直到得到非空内容或到达开头（整段都不是完整行时继续向前）
bool readBackward(QFile &file, qint64 base, TailResult *r) {
    QByteArray shown;
    while (shown.isEmpty() && !r->cursor.atFileStart()) {
        const LogText::Range range = LogText::nextRead(r->cursor);
        QByteArray bytes;
        if (!readRange(file, base + range.start, range.length(), &bytes, &r->error))
            return false;
        shown = LogText::feed(r->cursor, bytes);
    }
    r->bytes = shown;
    return true;
}

TailResult readTail(const QString &path, qint64 base) {
    TailResult r;
    const QFileInfo info(path);
    r.exists = !path.isEmpty() && info.exists();
    if (!r.exists)
        return r;
    r.size = info.size();
    if (base > r.size) {
        base = 0;
        r.baseReset = true;
    }
    r.cursor = LogText::begin(r.size - base);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        r.error = file.errorString();
        return r;
    }
    readBackward(file, base, &r);
    return r;
}

TailResult readMore(const QString &path, qint64 base, const LogText::TailCursor &cursor) {
    TailResult r;
    r.cursor = cursor;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        r.error = file.errorString();
        return r;
    }
    readBackward(file, base, &r);
    return r;
}

struct AppendResult {
    QString error;
    QByteArray bytes; // 只含完整行（除非整段都没有换行且已达上限）
};

AppendResult readAppend(const QString &path, qint64 from, qint64 to) {
    AppendResult r;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        r.error = file.errorString();
        return r;
    }
    const qint64 len = qMin(to - from, kAppendCap);
    QByteArray bytes;
    if (!readRange(file, from, len, &bytes, &r.error))
        return r;
    const qsizetype nl = bytes.lastIndexOf('\n');
    if (nl >= 0)
        r.bytes = bytes.left(nl + 1);
    else if (len >= kAppendCap)
        r.bytes = bytes;
    return r;
}

QString copyFile(const QString &src, const QString &dest) {
    QFile in(src);
    if (!in.exists())
        return QStringLiteral("日志文件不存在");
    if (!in.open(QIODevice::ReadOnly))
        return QStringLiteral("无法读取日志文件：") + in.errorString();
    QSaveFile out(dest);
    if (!out.open(QIODevice::WriteOnly))
        return QStringLiteral("无法写入目标文件：") + out.errorString();
    while (!in.atEnd()) {
        const QByteArray chunk = in.read(LogText::kChunkBytes);
        if (chunk.isEmpty() && in.error() != QFileDevice::NoError) {
            out.cancelWriting();
            return QStringLiteral("读取日志文件失败：") + in.errorString();
        }
        if (out.write(chunk) != chunk.size()) {
            out.cancelWriting();
            return QStringLiteral("写入目标文件失败：") + out.errorString();
        }
    }
    if (!out.commit())
        return QStringLiteral("保存目标文件失败：") + out.errorString();
    return {};
}
} // namespace

LogService::LogService(QObject *parent) : QObject(parent) {
    m_timer.setInterval(kPollMs);
    connect(&m_timer, &QTimer::timeout, this, &LogService::poll);
}

void LogService::setReading(bool on) {
    if (m_reading == on)
        return;
    m_reading = on;
    emit loadingChanged();
}

void LogService::setSize(qint64 size, bool exists) {
    if (m_size == size && m_exists == exists)
        return;
    m_size = size;
    m_exists = exists;
    emit fileSizeChanged();
}

void LogService::resetCursor(qint64 end) {
    m_cursor = LogText::begin(0);
    m_loadedEnd = end;
    emit canLoadMoreChanged();
}

void LogService::loadTail(const QString &id) {
    const quint64 gen = ++m_gen;
    if (m_id != id) {
        m_id = id;
        emit instanceIdChanged();
    }
    m_path = m_ops.logPath ? m_ops.logPath(id) : QString();
    m_timer.start();
    setReading(true);
    const QString path = m_path;
    const qint64 base = baseOffset();
    runAsync<TailResult>(
        this, [path, base]() { return readTail(path, base); },
        [this, gen, id](const TailResult &r) {
            if (gen != m_gen)
                return;
            setReading(false);
            if (!r.error.isEmpty()) {
                emit errorOccurred(QStringLiteral("读取"), r.error);
                return;
            }
            if (r.baseReset)
                m_clearOffsets.remove(id);
            m_cursor = r.cursor;
            m_loadedEnd = r.size;
            setSize(r.size, r.exists);
            emit canLoadMoreChanged();
            emit tailLoaded(QString::fromUtf8(r.bytes));
        });
}

void LogService::loadMore() {
    if (m_id.isEmpty() || m_reading || m_cursor.atFileStart())
        return;
    const quint64 gen = m_gen;
    setReading(true);
    const QString path = m_path;
    const qint64 base = baseOffset();
    const LogText::TailCursor cursor = m_cursor;
    runAsync<TailResult>(
        this, [path, base, cursor]() { return readMore(path, base, cursor); },
        [this, gen](const TailResult &r) {
            if (gen != m_gen)
                return;
            setReading(false);
            if (!r.error.isEmpty()) {
                emit errorOccurred(QStringLiteral("读取"), r.error);
                return;
            }
            m_cursor = r.cursor;
            emit canLoadMoreChanged();
            emit moreLoaded(QString::fromUtf8(r.bytes));
        });
}

void LogService::close() {
    ++m_gen;
    m_timer.stop();
    setReading(false);
    if (!m_id.isEmpty()) {
        m_id.clear();
        m_path.clear();
        emit instanceIdChanged();
        emit canLoadMoreChanged();
    }
}

void LogService::poll() {
    if (m_id.isEmpty())
        return;
    const QFileInfo info(m_path);
    const bool exists = !m_path.isEmpty() && info.exists();
    const qint64 size = exists ? info.size() : 0;
    setSize(size, exists);
    if (m_reading)
        return;
    if (size < m_loadedEnd) {
        // 文件被截断或替换：清空偏移失效，重新加载尾部
        if (size < baseOffset())
            m_clearOffsets.remove(m_id);
        loadTail(m_id);
    } else if (size > m_loadedEnd) {
        startAppendRead();
    }
}

void LogService::startAppendRead() {
    const quint64 gen = m_gen;
    setReading(true);
    const QString path = m_path;
    const qint64 from = m_loadedEnd;
    const qint64 to = m_size;
    runAsync<AppendResult>(
        this, [path, from, to]() { return readAppend(path, from, to); },
        [this, gen](const AppendResult &r) {
            if (gen != m_gen)
                return;
            setReading(false);
            if (!r.error.isEmpty()) {
                emit errorOccurred(QStringLiteral("读取"), r.error);
                return;
            }
            if (r.bytes.isEmpty())
                return;
            m_loadedEnd += r.bytes.size();
            emit appended(QString::fromUtf8(r.bytes));
        });
}

bool LogService::clearNeedsConfirm() const {
    return !m_id.isEmpty() && !(m_ops.isActive && m_ops.isActive(m_id));
}

void LogService::clear() {
    if (m_id.isEmpty())
        return;
    const QFileInfo info(m_path);
    const bool exists = !m_path.isEmpty() && info.exists();
    if (m_ops.isActive && m_ops.isActive(m_id)) {
        // 运行中：不动文件，只记录当前末尾，此后只显示之后的内容
        const qint64 end = exists ? info.size() : 0;
        m_clearOffsets.insert(m_id, end);
        ++m_gen;
        setReading(false);
        resetCursor(end);
        setSize(end, exists);
        emit cleared();
        return;
    }
    if (exists) {
        QFile file(m_path);
        if (!file.resize(0)) {
            emit errorOccurred(QStringLiteral("清空"), file.errorString());
            return;
        }
    }
    m_clearOffsets.remove(m_id);
    ++m_gen;
    setReading(false);
    resetCursor(0);
    setSize(0, exists);
    emit cleared();
}

void LogService::exportTo(const QString &path) {
    if (m_id.isEmpty() || m_exporting)
        return;
    const QString dest = path.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)
                             ? QUrl(path).toLocalFile()
                             : path;
    if (dest.isEmpty()) {
        emit exportFinished(false, QStringLiteral("导出失败：未指定保存路径"));
        return;
    }
    const QString src = m_path;
    if (QFileInfo(dest).absoluteFilePath().compare(QFileInfo(src).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        emit exportFinished(false, QStringLiteral("导出失败：目标与日志文件相同"));
        return;
    }
    m_exporting = true;
    emit exportingChanged();
    runAsync<QString>(
        this, [src, dest]() { return copyFile(src, dest); },
        [this, dest](const QString &error) {
            m_exporting = false;
            emit exportingChanged();
            if (error.isEmpty())
                emit exportFinished(true, QDir::toNativeSeparators(dest));
            else
                emit exportFinished(false, QStringLiteral("导出失败：") + error);
        });
}

void LogService::onStatusChanged(const QString &id, const QString &status) {
    if (!BatchPlan::isActiveState(status))
        m_clearOffsets.remove(id);
}

int LogService::lineLevel(const QString &line) const {
    switch (LogText::classify(line)) {
    case LogText::Level::Warning: return 1;
    case LogText::Level::Error: return 2;
    default: return 0;
    }
}

bool LogService::linePasses(const QString &line, int filter, bool onlyMatching, const QString &keyword) const {
    const LogText::LevelFilter f = filter == 1   ? LogText::LevelFilter::Errors
                                   : filter == 2 ? LogText::LevelFilter::Warnings
                                                 : LogText::LevelFilter::All;
    return LogText::linePasses(line, f, onlyMatching, QStringView(keyword).left(LogText::kMaxKeywordLength));
}

QVariantList LogService::find(const QString &text, const QString &keyword) const {
    QVariantList list;
    const auto matches = LogText::find(text, QStringView(keyword).left(LogText::kMaxKeywordLength));
    list.reserve(matches.size());
    for (const LogText::Match &m : matches)
        list.append(QVariantMap{{QStringLiteral("pos"), m.pos}, {QStringLiteral("len"), m.len}});
    return list;
}

void LogService::appendMarker(const QString &logPath, const QString &message, const QStringList &secrets) {
    if (logPath.isEmpty())
        return;
    QDir().mkpath(QFileInfo(logPath).absolutePath());
    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    const QString line = QStringLiteral("[Berth %1] %2\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                                  Redact::redact(message, secrets));
    file.write(line.toUtf8());
}
