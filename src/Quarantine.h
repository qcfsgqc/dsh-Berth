#pragma once

// quarantine.json 的读写：{schemaVersion:1, entries:[{home, profile, name, at, reason, detail}]}。
// home 存规范化的绝对路径，比较时不区分大小写；同一 (home, profile, name) 只保留一条。
// 磁盘读写经 DataStore（备份、只读模式、QSaveFile）；只在主线程使用。

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class DataStore;

struct QuarantineEntry {
    QString home;
    QString profile;
    QString name;
    QString at;     // ISO 8601 时间（本地时区偏移）
    QString reason; // PreflightDecision::reasonString 的取值
    QString detail;
};

class Quarantine : public QObject {
    Q_OBJECT

public:
    explicit Quarantine(DataStore *store, QObject *parent = nullptr);

    // 读取 quarantine.json（不存在时为空）；AppController 构造时调用一次
    void load();

    // 规范化 home：绝对路径 + cleanPath；空串原样返回
    static QString normalizeHome(const QString &home);

    QList<QuarantineEntry> entries() const { return m_entries; }
    QList<QuarantineEntry> entriesFor(const QString &home, const QString &profile) const;
    // 该 (home, profile) 下被隔离的插件名（给 PluginSet::list 的 quarantined 参数）
    QStringList namesFor(const QString &home, const QString &profile) const;
    // 单条记录（找不到时 name 为空）
    QuarantineEntry find(const QString &home, const QString &profile, const QString &name) const;

    // 追加（同键覆盖）并保存；保存失败返回 false（内存中仍已更新）
    bool add(const QList<QuarantineEntry> &items);
    // 删除一条记录并保存；不存在时返回 false、不写盘
    bool remove(const QString &home, const QString &profile, const QString &name);

    // QML 用：[{home, profile, name, at, reason, detail}]
    Q_INVOKABLE QVariantList listFor(const QString &home, const QString &profile) const;

signals:
    // 记录有变化（add / remove 成功改动内存后发出）
    void changed();

private:
    static bool sameKey(const QuarantineEntry &e, const QString &home, const QString &profile,
                        const QString &name);
    bool save();

    DataStore *m_store;
    QList<QuarantineEntry> m_entries;
};
