#pragma once

#include "Instance.h"

#include <QAbstractItemModel>
#include <QList>
#include <QSet>
#include <QVector>

// 两层树模型：profile 为父节点，引用该 profile 的泊位（instance）为子节点。
// 单列，供 QML 的 QtQuick.Controls TreeView 使用。
class ProfileTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    // 0=profile 行（父节点） 1=泊位行（子节点）
    enum ProfileNode { ProfileRow, InstanceRow };

    enum Roles {
        NodeTypeRole = Qt::UserRole + 1, // int: 0=profile 行 1=泊位行
        NameRole,       // profile 行=profile 名；泊位行=泊位名
        HomeRole,       // profile 行=该组 DSH_HOME；泊位行=该泊位的 dshHome
        DirExistsRole,  // 仅 profile 行有效：对应 profile 目录是否存在
        InstanceCountRole, // 仅 profile 行：子泊位数
        ProfileRole,    // 泊位行：该泊位的 profile 名
        IdRole,         // 泊位行：泊位 id（其余泊位属性 QML 可通过 berth.instance(id) 再取）
        PortRole,       // 泊位行：端口
        StatusRole,     // 泊位行：状态（stopped/running/...）
    };
    Q_ENUM(Roles)

    explicit ProfileTreeModel(QObject *parent = nullptr);
    ~ProfileTreeModel() override;

    // 全量重建（beginResetModel/endResetModel），先 qDeleteAll 释放旧节点。
    // - profile 行集合 = items 中每个 (dshHome, profile) 组合 ∪ knownProfiles 生成的 (defaultHome, name)，
    //   按 key "home\u0001profile" 去重，按 home 再按 profile 名升序排序。
    // - 模型不做 home 的 resolve/替换：调用者传来的 dshHome 若为空，树 key 与 HomeRole 原样为空串，
    //   显示"默认 home"由调用者预先把 defaultHome 归一（本方法只负责经 defaultHome 归一 knownProfiles 行）。
    // - missingKeys：key=home+"\u0001"+name 的集合，命中的 profile 行 DirExistsRole=false，否则 true。
    // - 子泊位保持 items 的传入顺序。
    void rebuild(const QList<Instance> &items,
                 const QStringList &knownProfiles,
                 const QString &defaultHome,
                 const QSet<QString> &missingKeys);

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Node {
        int type = ProfileRow;
        QString name;
        QString home;
        bool dirExists = true;
        QString profile;   // 泊位行：所属 profile 名
        QString id;        // 泊位行：泊位 id
        int port = 0;
        QString status;
        QVector<Node *> children;
        Node *parent = nullptr;

        // qDeleteAll 只删节点本身；递归析构子链，保证两层树整棵释放
        ~Node() { qDeleteAll(children); }
    };

    QVector<Node *> m_roots;
};
