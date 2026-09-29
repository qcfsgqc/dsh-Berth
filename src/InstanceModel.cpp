#include "InstanceModel.h"

InstanceModel::InstanceModel(QObject *parent) : QAbstractListModel(parent) {}

int InstanceModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant InstanceModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Instance &item = m_items.at(index.row());
    switch (role) {
    case IdRole: return item.id;
    case NameRole: return item.name;
    case PortRole: return item.port;
    case ProfileRole: return item.profile;
    case DshHomeRole: return item.dshHome;
    case WorkspaceRole: return item.workspace;
    case AutostartRole: return item.autostart;
    case StatusRole: return item.status;
    case PidRole: return item.pid;
    case LastErrorRole: return item.lastError;
    case LogPathRole: return item.logPath;
    default: return {};
    }
}

QHash<int, QByteArray> InstanceModel::roleNames() const {
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {PortRole, "port"},
        {ProfileRole, "profile"},
        {DshHomeRole, "dshHome"},
        {WorkspaceRole, "workspace"},
        {AutostartRole, "autostart"},
        {StatusRole, "status"},
        {PidRole, "pid"},
        {LastErrorRole, "lastError"},
        {LogPathRole, "logPath"}
    };
}

QList<Instance> InstanceModel::items() const { return m_items; }

void InstanceModel::setItems(QList<Instance> items) {
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
}

void InstanceModel::upsert(const Instance &item) {
    const int row = indexOf(item.id);
    if (row < 0) {
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.push_back(item);
        endInsertRows();
        return;
    }
    m_items[row] = item;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

void InstanceModel::remove(const QString &id) {
    const int row = indexOf(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
}

Instance InstanceModel::item(const QString &id) const {
    const int row = indexOf(id);
    if (row < 0)
        return {};
    return m_items.at(row);
}

bool InstanceModel::containsPort(int port, const QString &exceptId) const {
    for (const Instance &item : m_items) {
        if (item.id != exceptId && item.port == port)
            return true;
    }
    return false;
}

int InstanceModel::indexOf(const QString &id) const {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}
