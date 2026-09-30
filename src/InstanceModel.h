#pragma once

#include "Instance.h"

#include <QAbstractListModel>
#include <QList>

class InstanceModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PortRole,
        ProfileRole,
        DshHomeRole,
        WorkspaceRole,
        AutostartRole,
        StatusRole,
        PidRole,
        LastErrorRole,
        LogPathRole
    };
    Q_ENUM(Roles)

    explicit InstanceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QList<Instance> items() const;
    // 直接改内部数据的入口（如重命名 profile 时批量同步泊位引用）；
    // 不发任何信号，调用者改完后自行触发刷新与持久化
    QList<Instance> &mutableItems() { return m_items; }
    void setItems(QList<Instance> items);
    void upsert(const Instance &item);
    void remove(const QString &id);
    Instance item(const QString &id) const;
    bool containsPort(int port, const QString &exceptId = {}) const;

private:
    int indexOf(const QString &id) const;

    QList<Instance> m_items;
};
