#pragma once

#include "core/LogText.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <functional>

// 日志页后端：一次只服务一个打开的日志页（当前泊位）。
// - 读取（尾部 / 加载更多 / 新增内容）与导出都在线程池执行，结果排队回主线程
// - 清空：泊位处于 Active_State 时只记录文件末尾偏移（内存中保存到泊位离开 Active_State），
//   否则把日志文件截断为 0 字节（界面应先确认，见 clearNeedsConfirm）
// - 打开期间每 2 秒刷新文件大小，文件变长时追加读取新内容，变短时重新加载尾部
class LogService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString instanceId READ instanceId NOTIFY instanceIdChanged)
    Q_PROPERTY(qint64 fileSize READ fileSize NOTIFY fileSizeChanged)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY fileSizeChanged)
    Q_PROPERTY(bool fileExists READ fileExists NOTIFY fileSizeChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY canLoadMoreChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)

public:
    struct Ops {
        std::function<QString(const QString &id)> logPath;   // 泊位日志路径，找不到泊位时为空
        std::function<bool(const QString &id)> isActive;     // 是否处于 Active_State
    };

    explicit LogService(QObject *parent = nullptr);
    void setOps(Ops ops) { m_ops = std::move(ops); }

    QString instanceId() const { return m_id; }
    qint64 fileSize() const { return m_size; }
    QString sizeText() const { return LogText::humanSize(m_size); }
    bool fileExists() const { return m_exists; }
    bool canLoadMore() const { return !m_id.isEmpty() && !m_cursor.atFileStart(); }
    bool loading() const { return m_reading; }
    bool exporting() const { return m_exporting; }

    // 打开某泊位的日志页：异步加载末尾 64KB（从第一个完整行开始；有清空偏移时只加载偏移之后）
    // → tailLoaded。重复打开同一泊位也会重新加载。
    Q_INVOKABLE void loadTail(const QString &id);
    // 向前再加载 64KB → moreLoaded（文本应前置到当前内容）；已到开头或正在读取时忽略
    Q_INVOKABLE void loadMore();
    // 关闭日志页：停止刷新并丢弃在途读取结果
    Q_INVOKABLE void close();
    // 清空前是否需要确认（泊位不处于 Active_State 时清空会截断文件）
    Q_INVOKABLE bool clearNeedsConfirm() const;
    // 清空当前日志页 → cleared；失败 → errorOccurred("清空", 原因)，文件与界面不变
    Q_INVOKABLE void clear();
    // 把完整日志文件（不受清空偏移与过滤影响）逐字节复制到 path（本地路径或 file: URL）
    // → exportFinished(ok, 路径或原因)；失败时不留下不完整文件
    Q_INVOKABLE void exportTo(const QString &path);

    // —— 供 QML 调用的纯逻辑（core/LogText 的包装）——
    // 行级别：0 普通、1 警告、2 错误
    Q_INVOKABLE int lineLevel(const QString &line) const;
    // filter：0 全部、1 错误、2 警告
    Q_INVOKABLE bool linePasses(const QString &line, int filter, bool onlyMatching,
                                const QString &keyword) const;
    // 全部匹配 [{pos, len}, ...]（UTF-16 下标，不区分大小写，不重叠）；keyword 超长时截断到 256
    Q_INVOKABLE QVariantList find(const QString &text, const QString &keyword) const;
    Q_INVOKABLE int nextIndex(int current, int total) const { return LogText::nextIndex(current, total); }
    Q_INVOKABLE int prevIndex(int current, int total) const { return LogText::prevIndex(current, total); }

    // 往泊位日志追加一行 Berth 标记 "[Berth <时间>] <message>"，写出前用 secrets 脱敏
    static void appendMarker(const QString &logPath, const QString &message,
                             const QStringList &secrets = {});

public slots:
    // 泊位状态变化：离开 Active_State 时丢弃其清空偏移
    void onStatusChanged(const QString &id, const QString &status);

signals:
    void instanceIdChanged();
    void fileSizeChanged();
    void canLoadMoreChanged();
    void loadingChanged();
    void exportingChanged();
    // 尾部加载完成：text 替换当前全部内容
    void tailLoaded(const QString &text);
    // 加载更多完成：text 前置到当前内容
    void moreLoaded(const QString &text);
    // 新增内容：text 追加到当前内容末尾
    void appended(const QString &text);
    // 清空完成：界面清空
    void cleared();
    // 导出结果：成功时 message 为目标路径，失败时为"导出失败：<原因>"（导出错误只走本信号）
    void exportFinished(bool ok, const QString &message);
// operation：读取 / 清空；失败时文件与界面保持不变
    void errorOccurred(const QString &operation, const QString &message);

private:
    void poll();
    void startAppendRead();
    void setReading(bool on);
    void setSize(qint64 size, bool exists);
    void resetCursor(qint64 end);
    qint64 baseOffset() const { return m_clearOffsets.value(m_id, 0); }

    Ops m_ops;
    QString m_id;
    QString m_path;
    quint64 m_gen = 0;           // 打开 / 清空 / 关闭时递增，丢弃过期的异步结果
    LogText::TailCursor m_cursor; // 相对 baseOffset 的坐标
    qint64 m_loadedEnd = 0;      // 已显示内容在文件中的末尾（绝对偏移）
    qint64 m_size = 0;
    bool m_exists = false;
    bool m_reading = false;
    bool m_exporting = false;
    QHash<QString, qint64> m_clearOffsets; // Active_State 下清空时记录的文件末尾位置
    QTimer m_timer;
};
