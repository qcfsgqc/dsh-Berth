#pragma once

// Berth 数据文件（settings.json / instances.json / metrics.json / quarantine.json）的磁盘读写。
// schema 决策与写回计划由 core/DataFile 给出，这里只负责执行：
// - 读取时记住原字节与 LoadResult，写回时据此生成计划
// - 低版本文件第一次写回前先备份为 <file>.bak-<旧版本>（已存在不覆盖）；备份失败放弃写回
// - ReadOnly（schema 过高）或 Corrupt（无法解析）的文件拒绝保存，磁盘文件保持不变
// - 写入用 QSaveFile，不留下不完整文件
// - 读取阶段的提示（类型错误、只读、损坏）先攒着，flushWarnings() 时一次性发出 loadWarnings

#include "core/DataFile.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class DataStore : public QObject {
    Q_OBJECT

public:
    explicit DataStore(const QString &dir, QObject *parent = nullptr);

    QString dir() const;
    // file 为数据目录下的文件名（如 "instances.json"）
    QString path(const QString &file) const;

    // 读取 file 并返回顶层对象（已去掉 schemaVersion）。
    // supported：当前支持的 schema 版本；arrayKey：schema 0 顶层数组包装进的键名，空表示不接受顶层数组。
    // 文件不存在时返回空对象（Normal）；Corrupt 时返回空对象；ReadOnly 时照常返回数据。
    QJsonObject load(const QString &file, int supported, const QString &arrayKey = QString());

    // ReadOnly 或 Corrupt：禁止写回
    bool readOnly(const QString &file) const;

    // 把 known（当前版本识别的字段）合并回读取时的顶层对象并写盘。
    // 失败（未读取、只读、备份失败、写入失败）时发出 saveFailed 并返回 false。
    bool save(const QString &file, const QJsonObject &known);

    // 记录读取阶段的字段类型错误（"文件名.字段名"），flushWarnings 时一并提示
    void reportTypeErrors(const QStringList &fields);
    // 发出攒下的读取提示（没有则不发）
    void flushWarnings();

signals:
    void loadWarnings(const QStringList &messages);
    void saveFailed(const QString &message);

private:
    struct Entry {
        DataFile::LoadResult loaded;
        QByteArray bytes;   // 读取（或上次成功写入）时的原字节；文件不存在时为空
        int supported = 0;
        QString arrayKey;
    };

    static bool writeAtomically(const QString &target, const QByteArray &bytes, QString *error);

    QString m_dir;
    QHash<QString, Entry> m_entries;
    QStringList m_pendingWarnings;
    QStringList m_typeErrors;
};
