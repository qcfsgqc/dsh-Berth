#include "core/LogText.h"

#include <algorithm>

namespace LogText {

Level classify(QStringView line)
{
    static const QLatin1StringView kErrorTokens[] = {
        QLatin1StringView("error"), QLatin1StringView("ERR"),   QLatin1StringView("Error"),
        QLatin1StringView("fatal"), QLatin1StringView("panic"), QLatin1StringView("Unhandled"),
    };
    for (const auto &token : kErrorTokens) {
        if (line.contains(token, Qt::CaseSensitive))
            return Level::Error;
    }
    if (line.contains(QLatin1StringView("warn"), Qt::CaseInsensitive))
        return Level::Warning;
    return Level::Normal;
}

bool levelPasses(Level level, LevelFilter filter)
{
    switch (filter) {
    case LevelFilter::Errors:
        return level == Level::Error;
    case LevelFilter::Warnings:
        return level == Level::Warning;
    case LevelFilter::All:
        break;
    }
    return true;
}

bool linePasses(QStringView line, LevelFilter filter, bool onlyMatching, QStringView keyword)
{
    if (!levelPasses(classify(line), filter))
        return false;
    if (onlyMatching && !keyword.isEmpty())
        return line.contains(keyword, Qt::CaseInsensitive);
    return true;
}

QList<Match> find(QStringView text, QStringView keyword)
{
    QList<Match> matches;
    if (keyword.isEmpty())
        return matches;
    qsizetype from = 0;
    while (from <= text.size() - keyword.size()) {
        const qsizetype pos = text.indexOf(keyword, from, Qt::CaseInsensitive);
        if (pos < 0)
            break;
        matches.append(Match{pos, keyword.size()});
        from = pos + keyword.size(); // 不重叠
    }
    return matches;
}

int nextIndex(int current, int total)
{
    if (total <= 0)
        return -1;
    if (current < 0 || current >= total)
        return 0;
    return (current + 1) % total;
}

int prevIndex(int current, int total)
{
    if (total <= 0)
        return -1;
    if (current < 0 || current >= total)
        return total - 1;
    return (current - 1 + total) % total;
}

qsizetype firstLineStart(const QByteArray &chunk, bool atFileStart)
{
    if (atFileStart)
        return 0;
    const qsizetype nl = chunk.indexOf('\n');
    return nl < 0 ? chunk.size() : nl + 1;
}

TailCursor begin(qint64 fileSize)
{
    TailCursor cursor;
    cursor.readStart = std::max<qint64>(0, fileSize);
    return cursor;
}

Range nextRead(const TailCursor &cursor, qint64 chunk)
{
    if (chunk <= 0)
        chunk = kChunkBytes;
    const qint64 end = std::max<qint64>(0, cursor.readStart);
    return Range{std::max<qint64>(0, end - chunk), end};
}

QByteArray feed(TailCursor &cursor, const QByteArray &bytes)
{
    const qint64 newStart = std::max<qint64>(0, cursor.readStart - bytes.size());
    const QByteArray combined = bytes + cursor.carry;
    const qsizetype cut = firstLineStart(combined, newStart == 0);

    cursor.readStart = newStart;
    cursor.carry = combined.left(cut);
    return combined.mid(cut);
}

QString humanSize(qint64 bytes)
{
    constexpr qint64 kKB = 1024;
    constexpr qint64 kMB = 1024 * 1024;
    if (bytes < 0)
        bytes = 0;
    if (bytes < kKB)
        return QString::number(bytes) + QLatin1String(" B");
    if (bytes < kMB)
        return QString::number(double(bytes) / kKB, 'f', 1) + QLatin1String(" KB");
    return QString::number(double(bytes) / kMB, 'f', 1) + QLatin1String(" MB");
}

} // namespace LogText
