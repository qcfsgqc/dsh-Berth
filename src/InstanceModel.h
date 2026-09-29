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
    void setItems(QList<Instance> items);
    void upsert(const Instance &item);
    void remove(const QString &id);
    Instance item(const QString &id) const;
    bool containsPort(int port, const QString &exceptId = {}) const;

private:
    int indexOf(const QString &id) const;

    QList<Instance> m_items;
};
