#include "ProfileTreeModel.h"

#include <QHash>
#include <algorithm>

namespace {

inline constexpr char16_t KeySeparatorChar = u'\u0001';

// 模型内部唯一组合键：home + '\u0001' + profile（与 missingKeys 的 key 约定一致）
QString profileKey(const QString &home, const QString &name) {
    QString key = home;
    key.append(QChar(KeySeparatorChar));
    key.append(name);
    return key;
}

} // namespace

ProfileTreeModel::ProfileTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

ProfileTreeModel::~ProfileTreeModel() {
    qDeleteAll(m_roots);
}

void ProfileTreeModel::rebuild(const QList<Instance> &items,
                               const QStringList &knownProfiles,
                               const QString &defaultHome,
                               const QSet<QString> &missingKeys) {
    beginResetModel();
    qDeleteAll(m_roots);
    m_roots.clear();

    // 1) 收集泊位行，按组合键归组；组顺序先不保证，最后统一排序
    struct Group {
        QString home;
        QString profile;
        QVector<Instance> berths;
    };
    QHash<QString, Group> groups;
    for (const Instance &item : items) {
        const QString key = profileKey(item.dshHome, item.profile);
        Group &group = groups[key];
        group.home = item.dshHome;
        group.profile = item.profile;
        group.berths.append(item);
    }

    // 2) 合并默认 home 下的已知 profile（与泊位组合去重）
    for (const QString &name : knownProfiles) {
        const QString key = profileKey(defaultHome, name);
        if (!groups.contains(key))
            groups.insert(key, Group{defaultHome, name, {}});
    }

    // 3) 建立 profile 行（按 home 再按 profile 名升序），再挂泊位子节点（保持 items 顺序）
    QVector<Group> sorted;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
        sorted.append(it.value());
    std::sort(sorted.begin(), sorted.end(), [](const Group &a, const Group &b) {
        if (a.home != b.home)
            return a.home < b.home;
        return a.profile < b.profile;
    });

    for (const Group &group : sorted) {
        auto *profileNode = new Node;
        profileNode->type = ProfileRow;
        profileNode->name = group.profile;
        profileNode->home = group.home;
        profileNode->dirExists = !missingKeys.contains(profileKey(group.home, group.profile));
        profileNode->profile = group.profile;
        m_roots.append(profileNode);

        for (const Instance &item : group.berths) {
            auto *berthNode = new Node;
            berthNode->type = InstanceRow;
            berthNode->name = item.name;
            berthNode->home = item.dshHome;
            berthNode->profile = item.profile;
            berthNode->id = item.id;
            berthNode->port = item.port;
            berthNode->status = item.status;
            berthNode->parent = profileNode;
            profileNode->children.append(berthNode);
        }
    }

    endResetModel();
}

QModelIndex ProfileTreeModel::index(int row, int column, const QModelIndex &parent) const {
    if (row < 0 || column != 0)
        return {};
    if (!parent.isValid()) {
        if (row >= m_roots.size())
            return {};
        return createIndex(row, column, m_roots.at(row));
    }
    const auto *parentNode = static_cast<const Node *>(parent.internalPointer());
    if (!parentNode || parentNode->type != ProfileRow || row >= parentNode->children.size())
        return {};
    return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex ProfileTreeModel::parent(const QModelIndex &child) const {
    if (!child.isValid())
        return {};
    const auto *node = static_cast<const Node *>(child.internalPointer());
    // 泊位行的父节点只会是 profile 行（顶层）；profile 行无父，返回无效索引
    if (!node || !node->parent || node->parent->type != ProfileRow)
        return {};
    const int row = m_roots.indexOf(node->parent);
    if (row < 0)
        return {};
    return createIndex(row, 0, node->parent);
}

int ProfileTreeModel::rowCount(const QModelIndex &parent) const {
    if (!parent.isValid())
        return m_roots.size();
    const auto *node = static_cast<const Node *>(parent.internalPointer());
    return node ? node->children.size() : 0;
}

int ProfileTreeModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return 1;
}

QVariant ProfileTreeModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.column() != 0)
        return {};
    const auto *node = static_cast<const Node *>(index.internalPointer());
    if (!node)
        return {};

    if (node->type == ProfileRow) {
        switch (role) {
        case NodeTypeRole: return ProfileRow;
        case NameRole: return node->name;
        case HomeRole: return node->home;
        case DirExistsRole: return node->dirExists;
        case InstanceCountRole: return node->children.size();
        default: return {}; // 泊位行专属角色对 profile 行无效
        }
    }

    switch (role) {
    case NodeTypeRole: return InstanceRow;
    case NameRole: return node->name;
    case HomeRole: return node->home;
    case ProfileRole: return node->profile;
    case IdRole: return node->id;
    case PortRole: return node->port;
    case StatusRole: return node->status;
    default: return {}; // profile 行专属角色对泊位行无效
    }
}

QHash<int, QByteArray> ProfileTreeModel::roleNames() const {
    return {
        {NodeTypeRole, "nodeType"},
        {NameRole, "name"},
        {HomeRole, "home"},
        {DirExistsRole, "dirExists"},
        {InstanceCountRole, "instanceCount"},
        {ProfileRole, "profile"},
        {IdRole, "id"},
        {PortRole, "port"},
        {StatusRole, "status"},
    };
}
