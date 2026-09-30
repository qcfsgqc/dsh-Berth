#pragma once

// 泊位数据结构。只使用 QtCore 类型，berth_core（core/InstanceCodec）也会包含本文件。

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// 泊位环境变量表中的一行；保持行顺序
struct InstanceEnvVar {
    QString key;
    QString value;
    bool secret = false;   // 敏感值：明文存盘，只在 UI/日志/报告中掩码
    QJsonObject extra;     // 该行中无法识别的字段，写回时原样保留

    bool operator==(const InstanceEnvVar &) const = default;
};

// 泊位图标：kind 为 none | builtin | file；file 时 value 为相对 icons/ 目录的路径
struct InstanceIcon {
    QString kind = QStringLiteral("none");
    QString value;
    QJsonObject extra;

    bool operator==(const InstanceIcon &) const = default;
};

struct Instance {
    // ---- 持久化字段（instances.json）----
    QString id;
    QString name;
    int port = 3080;
    QString profile = QStringLiteral("web");
    QString dshHome;
    QString workspace;
    bool autostart = false;
    QString logPath;
    QList<InstanceEnvVar> env;
    QStringList extraArgs;
    QString dshVersion;        // 空表示使用系统 dsh
    bool autoRestart = false;
    InstanceIcon icon;
    QString group;
    QStringList tags;
    QString notes;
    QJsonObject extra;         // 无法识别的字段，写回时原样保留

    // ---- 运行时状态（不写盘）----
    QString status = QStringLiteral("stopped");
    qint64 pid = 0;
    QString lastError;

    bool operator==(const Instance &) const = default;
};
