#pragma once

// 代理与 npm 镜像的纯逻辑：手动字段校验、代理 URL 组装、子进程环境变量生成。
// 约束：只依赖 Qt6::Core。系统代理读取、DPAPI、探测与测试连接在 ProxyManager（30.3）。

#include "core/SettingsCodec.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace ProxyConfig {

// 官方 npm registry
inline constexpr char kOfficialRegistry[] = "https://registry.npmjs.org/";

// 解析端口文本：去首尾空白后须为纯十进制整数且在 1–65535，否则返回 nullopt
std::optional<int> parsePort(const QString &text);

// 校验手动代理字段，返回非法字段名列表（"host"、"port"，按此顺序）；空列表表示可保存。
// 主机去首尾空白后非空即合法；端口规则同 parsePort。
QStringList validateManual(const QString &host, const QString &portText);

// 同上，端口取已解析的整数（1–65535 合法）
QStringList validateManual(const QString &host, int port);

// 组装代理 URL：scheme://[user[:password]@]host:port
// - scheme 为空时取 "http"，统一转小写（http、https、socks5）
// - 用户名/密码做百分号编码；用户名为空时忽略密码
// - host 为 IPv6 字面量时加方括号
// - host 为空或端口非法时返回空串
QString buildUrl(const QString &scheme, const QString &host, int port,
                 const QString &user = QString(), const QString &password = QString());

// 由已保存设置组装手动代理 URL（password 为 DPAPI 解密后的明文，可为空）。
// mode 不是 "manual" 或字段非法时返回空串。系统代理 URL 由调用方读取后直接传给 proxyEnv。
QString manualUrl(const ProxySettings &p, const QString &password = QString());

// 代理环境变量：HTTP_PROXY、HTTPS_PROXY、http_proxy、https_proxy 都取 url；url 为空时返回空表
QHash<QString, QString> proxyEnv(const QString &url);

// registry 地址是否合法：以 http:// 或 https://（不区分大小写）开头且之后非空
bool isValidRegistryUrl(const QString &url);

// 实际使用的 registry：kind 为 "official"、url 为空或非法时返回官方源，否则返回去首尾空白的 url
QString registryUrl(const NpmRegistrySettings &r);

// registry 环境变量：npm_config_registry = url（npm 与 pnpm 都识别）；url 为空时返回空表
QHash<QString, QString> registryEnv(const QString &url);

// 合并代理与 registry 环境变量，供 ProcessRunner 注入 npm/pnpm/git 子进程。
// proxyUrl 为空表示不使用代理；registry 恒注入（官方源也显式写入，保证兜底重试可切换）。
QHash<QString, QString> childEnv(const QString &proxyUrl, const QString &registry);

} // namespace ProxyConfig
