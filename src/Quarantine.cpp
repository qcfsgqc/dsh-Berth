#include "Quarantine.h"

#include "DataStore.h"

#include <QDir>
#include <QJsonArray>
#include <QVariantMap>

namespace {
const QString kFile = QStringLiteral("quarantine.json");
const QString kEntries = QStringLiteral("entries");
constexpr int kSchema = 1;

QVariantMap toMap(const QuarantineEntry &e) {
    QVariantMap m;
    m.insert(QStringLiteral("home"), e.home);
    m.insert(QStringLiteral("profile"), e.profile);
    m.insert(QStringLiteral("name"), e.name);
    m.insert(QStringLiteral("at"), e.at);
    m.insert(QStringLiteral("reason"), e.reason);
    m.insert(QStringLiteral("detail"), e.detail);
    return m;
}
} // namespace

Quarantine::Quarantine(DataStore *store, QObject *parent) : QObject(parent), m_store(store) {}

QString Quarantine::normalizeHome(const QString &home) {
    if (home.trimmed().isEmpty())
        return home;
    return QDir::cleanPath(QDir(home.trimmed()).absolutePath());
}

bool Quarantine::sameKey(const QuarantineEntry &e, const QString &home, const QString &profile,
                         const QString &name) {
    return e.name == name && e.profile == profile
        && normalizeHome(e.home).compare(normalizeHome(home), Qt::CaseInsensitive) == 0;
}

void Quarantine::load() {
    m_entries.clear();
    const QJsonObject root = m_store->load(kFile, kSchema, QString());
    const QJsonArray arr = root.value(kEntries).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        QuarantineEntry e;
        e.home = o.value(QStringLiteral("home")).toString();
        e.profile = o.value(QStringLiteral("profile")).toString();
        e.name = o.value(QStringLiteral("name")).toString();
        e.at = o.value(QStringLiteral("at")).toString();
        e.reason = o.value(QStringLiteral("reason")).toString();
        e.detail = o.value(QStringLiteral("detail")).toString();
        if (e.name.isEmpty())
            continue;
        m_entries.append(e);
    }
}

QList<QuarantineEntry> Quarantine::entriesFor(const QString &home, const QString &profile) const {
    QList<QuarantineEntry> out;
    const QString h = normalizeHome(home);
    for (const QuarantineEntry &e : m_entries) {
        if (e.profile == profile && normalizeHome(e.home).compare(h, Qt::CaseInsensitive) == 0)
            out.append(e);
    }
    return out;
}

QStringList Quarantine::namesFor(const QString &home, const QString &profile) const {
    QStringList names;
    for (const QuarantineEntry &e : entriesFor(home, profile))
        names.append(e.name);
    return names;
}

QuarantineEntry Quarantine::find(const QString &home, const QString &profile, const QString &name) const {
    for (const QuarantineEntry &e : m_entries) {
        if (sameKey(e, home, profile, name))
            return e;
    }
    return {};
}

bool Quarantine::add(const QList<QuarantineEntry> &items) {
    if (items.isEmpty())
        return true;
    for (QuarantineEntry e : items) {
        e.home = normalizeHome(e.home);
        bool replaced = false;
        for (QuarantineEntry &old : m_entries) {
            if (sameKey(old, e.home, e.profile, e.name)) {
                old = e;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            m_entries.append(e);
    }
    emit changed();
    return save();
}

bool Quarantine::remove(const QString &home, const QString &profile, const QString &name) {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (sameKey(m_entries.at(i), home, profile, name)) {
            m_entries.removeAt(i);
            emit changed();
            return save();
        }
    }
    return false;
}

QVariantList Quarantine::listFor(const QString &home, const QString &profile) const {
    QVariantList out;
    for (const QuarantineEntry &e : entriesFor(home, profile))
        out.append(toMap(e));
    return out;
}

bool Quarantine::save() {
    QJsonArray arr;
    for (const QuarantineEntry &e : std::as_const(m_entries)) {
        QJsonObject o;
        o.insert(QStringLiteral("home"), e.home);
        o.insert(QStringLiteral("profile"), e.profile);
        o.insert(QStringLiteral("name"), e.name);
        o.insert(QStringLiteral("at"), e.at);
        o.insert(QStringLiteral("reason"), e.reason);
        o.insert(QStringLiteral("detail"), e.detail);
        arr.append(o);
    }
    QJsonObject known;
    known.insert(kEntries, arr);
    return m_store->save(kFile, known);
}
