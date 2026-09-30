#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringView>

// 日志页的纯逻辑：行级别判定、搜索、尾部分段加载、大小格式化。只依赖 Qt6::Core。
namespace LogText {

constexpr int kMaxKeywordLength = 256;          // 搜索关键字上限（由界面截断）
constexpr qint64 kChunkBytes = 64 * 1024;       // 尾部 / 加载更多的分段大小

// ---- 行级别 ----------------------------------------------------------------

enum class Level { Normal, Warning, Error };

// 错误行：包含 error / ERR / Error / fatal / panic / Unhandled 之一（区分大小写）；
// 警告行：不是错误行且包含 warn（不区分大小写）；其余为 Normal。
Level classify(QStringView line);

enum class LevelFilter { All, Errors, Warnings };

bool levelPasses(Level level, LevelFilter filter);

// 单行是否通过过滤。onlyMatching 为 true 且 keyword 非空时，要求该行不区分大小写包含 keyword；
// 与级别过滤同时生效时取交集。
bool linePasses(QStringView line, LevelFilter filter, bool onlyMatching, QStringView keyword);

// ---- 搜索 ------------------------------------------------------------------

struct Match {
    qsizetype pos = 0; // 在 text 中的起始下标（UTF-16 单元）
    qsizetype len = 0;
};

// 按纯文本、不区分大小写、不重叠地从左到右查找 keyword 的全部出现；keyword 为空时返回空列表。
QList<Match> find(QStringView text, QStringView keyword);

// 循环跳转：total <= 0 时返回 -1；current 越界（含 -1）时 next 回到 0、prev 回到 total-1。
int nextIndex(int current, int total);
int prevIndex(int current, int total);

// ---- 尾部分段加载 ------------------------------------------------------------

// chunk 中第一个完整行的起始下标：atFileStart 时为 0；否则为第一个 '\n' 之后；
// chunk 中没有 '\n' 时返回 chunk.size()（整个 chunk 都不是完整行）。
qsizetype firstLineStart(const QByteArray &chunk, bool atFileStart = false);

struct Range {
    qint64 start = 0; // 含
    qint64 end = 0;   // 不含
    qint64 length() const { return end - start; }
};

// 从文件末尾向前分段读取的游标。
// readStart：已读取区间的起点；carry：已读取但尚未显示的前缀（不完整行），会与下一段拼接。
struct TailCursor {
    qint64 readStart = 0;
    QByteArray carry;

    // 已到文件开头：没有更多可加载内容（用于隐藏 / 禁用"加载更多"）
    bool atFileStart() const { return readStart <= 0 && carry.isEmpty(); }
};

// 从文件末尾开始的游标（fileSize < 0 视为 0）
TailCursor begin(qint64 fileSize);

// 下一次需要读取的字节区间 [max(0, readStart - chunk), readStart)；chunk <= 0 时用 kChunkBytes
Range nextRead(const TailCursor &cursor, qint64 chunk = kChunkBytes);

// 喂入按 nextRead 读到的字节，更新游标，返回应拼接到当前显示内容前面的部分。
// 返回值总从行首开始；读到文件开头时把剩余 carry 全部返回，
// 因此从 begin 起反复 nextRead/feed 直到 atFileStart，所有返回值依次前置拼接后等于整个文件。
QByteArray feed(TailCursor &cursor, const QByteArray &bytes);

// ---- 大小格式化 --------------------------------------------------------------

// b < 1024 → "<b> B"；b < 1024² → "x.y KB"；否则 "x.y MB"（1KB = 1024B，保留 1 位小数）。负数按 0 处理。
QString humanSize(qint64 bytes);

} // namespace LogText
