#pragma once

#include <QString>
#include <QStringList>

// HKCU Run 启动项值的生成与解析（纯逻辑）。只依赖 Qt6::Core。
// 值形如 "<exe>" --autostart；比较路径时不区分大小写，/ 与 \ 视为相同。
namespace RunValue {

inline constexpr char kAutostartArg[] = "--autostart";

struct Parsed {
    QString exe;       // 可执行文件路径（去掉引号，原样保留分隔符）
    QStringList args;  // 路径之后的参数
    bool ok = false;   // 值为空或引号不闭合时为 false
};

// 生成 "<exe>" --autostart，路径转为 Windows 原生分隔符
QString make(const QString &exePath);

// 解析启动项值：带引号时取第一对引号内为路径；不带引号时，
// 路径为第一个以 '-' 开头的空白分隔片段之前的全部内容（允许路径含空格）
Parsed parse(const QString &value);

// 两个路径是否指向同一文件（清理 . / ..、统一分隔符、不区分大小写）
bool samePath(const QString &a, const QString &b);

// 启动项值是否指向 exePath 且带 --autostart
bool matches(const QString &value, const QString &exePath);

} // namespace RunValue
