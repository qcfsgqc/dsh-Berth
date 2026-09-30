#include "DataStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

DataStore::DataStore(const QString &dir, QObject *parent) : QObject(parent), m_dir(dir) {}

QString DataStore::dir() const { return m_dir; }

QString DataStore::path(const QString &file) const {
    return m_dir + QLatin1Char('/') + file;
}

QJsonObject DataStore::load(const QString &file, int supported, const QString &arrayKey) {
    Entry entry;
    entry.supported = supported;
    entry.arrayKey = arrayKey;

    const QString target = path(file);
    if (!QFileInfo::exists(target)) {
        entry.loaded = DataFile::fresh(supported);
    } else {
        QFile in(target);
        if (!in.open(QIODevice::ReadOnly)) {
            // 读不到就无法保证写回不丢数据，按 Corrupt 处理：不覆盖原文件
            entry.loaded.mode = DataFile::Mode::Corrupt;
            entry.loaded.version = -1;
            entry.loaded.error = in.errorString();
        } else {
            entry.bytes = in.readAll();
            entry.loaded = DataFile::load(entry.bytes, supported, arrayKey);
        }
    }

    switch (entry.loaded.mode) {
    case DataFile::Mode::ReadOnly:
        m_pendingWarnings.append(QStringLiteral("%1 的 schema 版本 %2 高于当前支持的 %3，已以只读模式加载，修改不会保存，请升级 Berth")
                                     .arg(file)
                                     .arg(entry.loaded.version)
                                     .arg(supported));
        break;
    case DataFile::Mode::Corrupt:
        m_pendingWarnings.append(QStringLiteral("%1 无法解析（%2），已以只读模式加载，原文件未改动")
                                     .arg(file, entry.loaded.error));
        break;
    case DataFile::Mode::Normal:
        break;
    }

    const QJsonObject root = entry.loaded.root;
    m_entries.insert(file, entry);
    return root;
}

bool DataStore::readOnly(const QString &file) const {
    const auto it = m_entries.constFind(file);
    if (it == m_entries.constEnd())
        return false;
    return it->loaded.mode != DataFile::Mode::Normal;
}

bool DataStore::save(const QString &file, const QJsonObject &known) {
    const auto it = m_entries.find(file);
    if (it == m_entries.end()) {
        emit saveFailed(QStringLiteral("%1 尚未读取，未保存").arg(file));
        return false;
    }
    Entry &entry = it.value();

    const QString target = path(file);
    const bool backupExists = QFileInfo::exists(DataFile::backupName(target, entry.loaded.version));
    const DataFile::WritePlan plan =
        DataFile::planWrite(target, entry.loaded, entry.bytes, known, entry.supported, backupExists);
    if (!plan.allowed) {
        emit saveFailed(QStringLiteral("%1：%2，未保存").arg(file, plan.reason));
        return false;
    }

    QDir().mkpath(m_dir);
    QByteArray written;
    for (const DataFile::WriteStep &step : plan.steps) {
        QString error;
        if (!writeAtomically(step.path, step.bytes, &error)) {
            if (step.kind == DataFile::StepKind::Backup) {
                // 备份失败：放弃写回，原文件保持不变
                emit saveFailed(QStringLiteral("备份 %1 失败（%2），已放弃保存 %3")
                                    .arg(QFileInfo(step.path).fileName(), error, file));
            } else {
                emit saveFailed(QStringLiteral("写入 %1 失败（%2）").arg(file, error));
            }
            return false;
        }
        if (step.kind == DataFile::StepKind::Write)
            written = step.bytes;
    }

    // 写入成功后以新内容为基准，下次写回不再备份
    entry.bytes = written;
    entry.loaded = DataFile::load(written, entry.supported, entry.arrayKey);
    return true;
}

void DataStore::reportTypeErrors(const QStringList &fields) {
    for (const QString &field : fields) {
        if (!m_typeErrors.contains(field))
            m_typeErrors.append(field);
    }
}

void DataStore::flushWarnings() {
    QStringList messages = m_pendingWarnings;
    if (!m_typeErrors.isEmpty())
        messages.append(QStringLiteral("以下字段类型不正确，已使用默认值：%1").arg(m_typeErrors.join(QStringLiteral("、"))));
    m_pendingWarnings.clear();
    m_typeErrors.clear();
    if (!messages.isEmpty())
        emit loadWarnings(messages);
}

bool DataStore::writeAtomically(const QString &target, const QByteArray &bytes, QString *error) {
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly)) {
        *error = out.errorString();
        return false;
    }
    if (out.write(bytes) != bytes.size()) {
        *error = out.errorString();
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) {
        *error = out.errorString();
        return false;
    }
    return true;
}
