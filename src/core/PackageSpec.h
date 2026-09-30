#pragma once

// 插件安装输入与目录源地址的格式校验（纯逻辑）。只依赖 Qt6::Core。
// 每个 validate* 函数：合法返回 std::nullopt，不合法返回错误描述。
// 校验不做 trim，调用方按需先去掉首尾空白。

#include <QString>

#include <optional>

namespace PackageSpec {

// npm 包名（不含 @版本 / @tag 后缀）：
// 长度 1–214（含 scope）、全小写、不以 . 或 _ 开头、不含空格，
// 字符限于 a-z 0-9 - . _ ~，可带 @scope/ 前缀（scope 同样遵守以上字符与首字符规则）
std::optional<QString> validateNpmName(const QString &name);

// npm 安装规格：包名，可带 @<版本号> 或 @<dist-tag> 后缀（后缀非空、不含空白、@、/）
std::optional<QString> validateNpmSpec(const QString &spec);

// git URL：以 git+https://、https:// 或 git@ 开头，前缀后内容非空且不含空白
std::optional<QString> validateGitUrl(const QString &url);

// 安装输入：npm 规格或 git URL 任一合法即通过
std::optional<QString> validateInstallInput(const QString &input);

// 目录源地址：以 http:// 或 https:// 开头（大小写敏感），且前缀后非空、不含空白
std::optional<QString> validateCatalogUrl(const QString &url);

enum class Kind { Invalid, Npm, Git };

struct Parsed {
    Kind kind = Kind::Invalid;
    QString name;    // Npm：包名（含 scope）；Git：完整 URL
    QString suffix;  // Npm：@ 后的版本号或 dist-tag（无后缀为空）；Git：空
    QString error;   // Invalid 时的错误描述
};

// 拆分安装输入。git URL 优先于 npm 规格判定。
Parsed parse(const QString &input);

} // namespace PackageSpec
