#pragma once

#include <QString>
#include <QStringList>

#include <optional>

// SemVer 2.0 版本号的解析、比较与 npm 风格范围匹配（纯逻辑）。只依赖 Qt6::Core。
struct SemVer {
    quint64 major = 0;
    quint64 minor = 0;
    quint64 patch = 0;
    QStringList prerelease; // 点分预发布标识符，空表示正式版
    QStringList build;      // 构建元数据，不参与比较

    // 严格解析 SemVer 2.0（MAJOR.MINOR.PATCH[-pre][+build]），忽略开头的单个 v/V。
    // 数字部分不允许前导零，且不超过 2^53-1（与 npm 一致）；不裁剪空白；非法返回空。
    static std::optional<SemVer> parse(const QString &text);

    // 按 SemVer 2.0 优先级比较：返回 <0 / 0 / >0。构建元数据被忽略；
    // 预发布版本低于同号正式版本；数字标识符按数值比较且低于字母数字标识符。
    static int compare(const SemVer &a, const SemVer &b);

    // npm 范围匹配（与 node-semver 默认行为一致，不含 includePrerelease）：
    // 支持 `^ ~ ~> >= <= > < =`、x/X/* 通配与部分版本、`A - B` 连字符范围、`||` 并集；
    // 空串或 `*` 匹配任意正式版。带预发布标签的版本仅在同一比较器组中存在
    // 主次补丁号相同且带预发布标签的比较器时才可能匹配。范围非法返回 false。
    static bool satisfies(const SemVer &version, const QString &range);

    // 规范文本（不带 v 前缀，含预发布与构建元数据）
    QString toString() const;
};
