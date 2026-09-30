#pragma once

// 插件目录（Plugin_Catalog）JSON 的解析、打印与搜索过滤（纯逻辑）。只依赖 Qt6::Core。
//
// 目录格式（print 输出；parse 另接受根为条目数组的形式）：
// {
//   "plugins": [
//     { "name": "@scope/pkg", "description": "...",
//       "versions": { "stable": "1.2.0", "beta": "1.3.0-beta.1", "alpha": "..." } }
//   ]
// }
// - name：必需，非空字符串
// - description：可选，缺失视为空字符串；存在时必须是字符串
// - versions：stable / beta / alpha 至少一个非空字符串；空字符串视为该渠道缺失
// - 未识别的字段忽略

#include <QByteArray>
#include <QList>
#include <QString>

struct CatalogEntry {
    QString name;
    QString description;
    QString stable; // 空 = 该渠道无版本
    QString beta;
    QString alpha;

    // channel："stable" / "beta" / "alpha"；其它值返回空字符串
    QString version(const QString &channel) const;
    bool hasAnyVersion() const;

    bool operator==(const CatalogEntry &o) const
    {
        return name == o.name && description == o.description && stable == o.stable
            && beta == o.beta && alpha == o.alpha;
    }
    bool operator!=(const CatalogEntry &o) const { return !(*this == o); }
};

namespace CatalogCodec {

struct ParseResult {
    bool ok = false;
    QList<CatalogEntry> entries; // ok 时有效，保持源顺序
    QString error;               // !ok 时的错误描述，含出错位置（偏移）或字段路径（如 plugins[3].name）
};

ParseResult parse(const QByteArray &json);

// 序列化为与目录源一致的格式（根对象 + "plugins" 数组）；空描述与空渠道版本不写出
QByteArray print(const QList<CatalogEntry> &entries);

// 返回包名或描述包含 keyword（不区分大小写）的条目，保持原顺序；keyword 为空返回全部
QList<CatalogEntry> filter(const QList<CatalogEntry> &entries, const QString &keyword);

} // namespace CatalogCodec
