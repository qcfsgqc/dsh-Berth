#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class InstanceModel;

// 在 VS Code / Cursor / 资源管理器中打开泊位的 workspace（需求 10）。
// 每次调用 availability 都重新检测可执行文件并校验路径，不做缓存。
class WorkspaceOpener : public QObject {
    Q_OBJECT

public:
    explicit WorkspaceOpener(const InstanceModel *instances, QObject *parent = nullptr);

    // 三个操作的启用状态与禁用原因：
    // { vscode: {enabled, reason}, cursor: {enabled, reason}, explorer: {enabled, reason} }
    Q_INVOKABLE QVariantMap availability(const QString &id) const;
    // target 为 "vscode" | "cursor" | "explorer"；成功返回空串，失败返回说明哪个程序启动失败的错误
    Q_INVOKABLE QString open(const QString &id, const QString &target) const;

    // workspace 路径校验：通过返回空串并把规范化的绝对路径写入 absPath，否则返回原因
    static QString checkWorkspace(const QString &workspace, QString *absPath);
    // 查找可执行文件：target 为 vscode | cursor；找不到返回空
    static QString findEditor(const QString &target);

private:
    const InstanceModel *m_instances;
};
