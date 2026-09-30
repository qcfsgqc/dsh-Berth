#pragma once

// Bundle（.berthbundle）的编码与解码。文件格式为单个 UTF-8 JSON：
//   {manifest:{formatVersion, exportedAt, berthVersion, type:"instance|profile", profileName},
//    files:[{path, encoding:"utf8|base64", content}],
//    plugins:[{name, version, enabled}],
//    instance?:{port, profile, workspace, dshVersion, env:[{key, value?, secret}], extraArgs}}
// - encode 跳过 excluded() 命中的文件；includeSecrets=false 时敏感 env 只写键名与 secret 标记
// - decode 拒绝：无法解析、缺少 manifest 或 plugins、formatVersion 不合法或高于 maxFormat、
//   type 不合法、type 为 instance 却缺少 instance、文件路径为绝对路径或含 ".."
// 约束：只依赖 Qt6::Core；不读写磁盘（由 BundleIO 负责）。

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace BundleCodec {

// 当前 Berth 支持的最高格式版本
constexpr int kFormatVersion = 1;

inline const QString kTypeInstance = QStringLiteral("instance");
inline const QString kTypeProfile = QStringLiteral("profile");

struct Manifest {
    int formatVersion = kFormatVersion;
    QString exportedAt;      // ISO 8601
    QString berthVersion;
    QString type = kTypeProfile; // "instance" | "profile"
    QString profileName;

    bool operator==(const Manifest &) const = default;
};

// profile 目录下的一个文件；path 为相对 profile 目录、以 "/" 分隔的路径
struct BundleFile {
    QString path;
    QByteArray data;         // 原始字节；编码时合法 UTF-8 写为 utf8，否则写为 base64

    bool operator==(const BundleFile &) const = default;
};

struct BundlePlugin {
    QString name;
    QString version;
    bool enabled = false;

    bool operator==(const BundlePlugin &) const = default;
};

struct BundleEnvVar {
    QString key;
    QString value;
    bool secret = false;
    bool valueOmitted = false; // 解码时：敏感值未随 Bundle 导出（value 为空，需要用户补填）

    bool operator==(const BundleEnvVar &) const = default;
};

struct BundleInstance {
    int port = 3080;
    QString profile;
    QString workspace;
    QString dshVersion;
    QList<BundleEnvVar> env;
    QStringList extraArgs;

    bool operator==(const BundleInstance &) const = default;
};

struct Bundle {
    Manifest manifest;
    QList<BundleFile> files;
    QList<BundlePlugin> plugins;       // 含 Core_Package
    std::optional<BundleInstance> instance; // 仅 type 为 instance 时存在

    bool operator==(const Bundle &) const = default;
};

struct DecodeResult {
    std::optional<Bundle> value;
    QString error;           // 失败原因（中文，可直接展示）

    bool ok() const { return value.has_value(); }
};

// includeSecrets=false：secret 为 true 的 env 行不写 value
QByteArray encode(const Bundle &bundle, bool includeSecrets = true);

DecodeResult decode(const QByteArray &bytes, int maxFormat = kFormatVersion);

// relPath 任意层级含 node_modules 段（不区分大小写），或以 .log 结尾（不区分大小写）时返回 true。
// 同时接受 "/" 与 "\\" 分隔符。
bool excluded(const QString &relPath);

// 解码结果中需要用户补填的敏感键（valueOmitted 为 true 的行），保持原顺序
QStringList missingSecrets(const Bundle &bundle);

} // namespace BundleCodec
