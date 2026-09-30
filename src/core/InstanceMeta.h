#pragma once

// 泊位图标、分组、标签、备注的纯逻辑（需求 9）。只依赖 Qt6::Core。
// - 校验函数返回 std::nullopt 表示通过，否则返回可直接展示的中文原因
// - 长度按 Unicode 码点计（toUcs4().size()），不按 UTF-16 单元
// - groupBy/search 返回的是输入列表中的下标，不复制 Instance

#include "Instance.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace InstanceMeta {

constexpr int kMaxGroupLen = 32;
constexpr int kMaxTagCount = 10;
constexpr int kMaxTagLen = 20;
constexpr int kMaxNotesLen = 2000;
constexpr qint64 kMaxImageBytes = 1048576;

// "未分组"的显示名
QString ungroupedName();

// 码点长度
int textLength(const QString &s);

// 分组名：单行，最长 kMaxGroupLen（按 trim 后计）；空白视为未分组，合法
std::optional<QString> validateGroup(const QString &group);

// 向已有标签列表添加一个标签：trim 后不能为空、不超过 kMaxTagLen、
// 不与已有标签重复（trim 后不区分大小写）、总数不超过 kMaxTagCount
std::optional<QString> validateNewTag(const QStringList &existing, const QString &tag);

// 整个标签列表（保存前）：数量、每项非空与长度、两两不重复（不区分大小写）
std::optional<QString> validateTags(const QStringList &tags);

// 备注：最长 kMaxNotesLen
std::optional<QString> validateNotes(const QString &notes);

// 本地图片：扩展名为 png/jpg/jpeg/ico/svg（不区分大小写），大小不超过 kMaxImageBytes，且可读
std::optional<QString> validateImage(const QString &fileName, qint64 sizeBytes, bool readable);

struct Group {
    QString name;        // 显示名；未分组时为 ungroupedName()
    bool ungrouped = false;
    QList<int> indices;  // 输入列表中的下标，保持原顺序

    bool operator==(const Group &o) const
    {
        return name == o.name && ungrouped == o.ungrouped && indices == o.indices;
    }
};

// 分组名 trim 后为空的泊位归入"未分组"；其余按 trim 后的分组名（区分大小写）归组。
// 分组按首次出现顺序排列，"未分组"固定放最后；只产生非空分组，每个泊位只出现一次。
QList<Group> groupBy(const QList<Instance> &items);

// 名称或任一标签包含 trim(keyword)（不区分大小写）即匹配
bool matches(const Instance &item, const QString &keyword);

// 在 groupBy 结果上过滤，去掉过滤后为空的分组；keyword trim 后为空时等于 groupBy
QList<Group> search(const QList<Instance> &items, const QString &keyword);

} // namespace InstanceMeta
