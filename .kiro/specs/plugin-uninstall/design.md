# Design Document

> [!note] Spec 模式：tiny（尺度约定见 requirements.md 开头）

## Overview

在泊位操作栏加"插件"入口，打开对话框列出该泊位 profile 里用户安装的插件，支持只从 `dsh.profile.bundles` 移除，或连插件包一起经 `dsh plugin remove` 卸载。

## Architecture

`PluginsDialog.qml` → `AppController`（`listPlugins` / `uninstallPlugin`）→ `PluginOps`（读写 `package.json`）+ `QProcess`（执行 `dsh plugin ... remove`）。

关键决定：

- 卸载插件包用 `dsh plugin --profile <profile> remove <pkg>`，不直接调 pnpm。它在 profile 目录里转调 pnpm，效果就是 pnpm 默认行为（Req 3.1）。它和 dsh 的插件管理器共用 profile 写锁。可执行文件沿用 `Settings::dshExecutable()`，环境里的 `DSH_HOME` 按泊位设置注入，和 `Supervisor::start` 一致。
- 调用前用 `QStandardPaths::findExecutable("pnpm")` 检查 pnpm，找不到就直接返回错误（Req 3.3）。
- 步骤顺序照官方：先改 `dsh.profile.bundles`，再跑 pnpm。任一步失败就停下，不回滚（Req 2.6）。
- 自带 bundle 固定为 6 个：`@deepseek-ai/dsh-base`、`dsh-web-app`、`dsh-headless`、`dsh-sdk-app`、`dsh-sdk-minimal`、`dsh-acp-app`（均带 `@deepseek-ai/` 前缀），列表里过滤掉（Req 1.1）。
- 改 `package.json` 用 `QJsonDocument` 读写，只动 `dsh.profile.bundles` 数组。副作用：键会按字母序排列、缩进变成 4 空格，之后 pnpm 会再按自己的格式重写一次。这个格式变化可以接受。
- 运行保护：相关泊位指 `resolveHome(dshHome)` 和 `profile` 都相同的泊位，看它们的 `status` 是否在 running/starting/stopping 里（Req 4）。

## Components and Interfaces

- 新增 `src/PluginOps.h/.cpp`：不带 UI 的纯逻辑，都是静态函数。
  - `QVariantMap readPlugins(const QString &profileDir)`
  - `bool removeFromBundles(const QString &profileDir, const QString &name, QString *error)`
- `src/AppController.h/.cpp`：
  - `Q_PROPERTY(bool pluginBusy READ pluginBusy NOTIFY pluginBusyChanged)`
  - `Q_INVOKABLE QVariantMap listPlugins(const QString &id) const`
  - `Q_INVOKABLE void uninstallPlugin(const QString &id, const QString &name, bool removePackage)`
  - `signal pluginUninstallFinished(const QString &id, bool ok, const QString &message)`
  - 私有 `QProcess *m_pluginProcess`，异步执行，`CREATE_NO_WINDOW`，stdout 和 stderr 合并采集（Req 2.7）。进程结束后取输出末尾约 20 行作为 message（Req 3.2）。
  - profile 目录为 `resolveHome(item.dshHome) + "/profiles/" + item.profile`。
- 新增 `qml/PluginsDialog.qml`：模态 Dialog。
  - 顶部是插件列表，每行显示名称、版本、"已加载/未加载"和卸载按钮，右上角有刷新按钮。
  - 空列表显示提示（Req 1.2），读取失败显示错误（Req 1.3）。
  - 点卸载会弹出二级确认，写明插件名和 profile，带一个"同时卸载插件包"勾选框，默认不勾（Req 2.1、2.2）。
  - `pluginBusy` 为真时禁用所有卸载按钮。收到 `pluginUninstallFinished` 后刷新列表，并在对话框里显示 message。
- `qml/InstancePane.qml`：操作按钮行在"日志"后加一个"插件"按钮，点击打开 PluginsDialog。
- `CMakeLists.txt`：加入 `src/PluginOps.h`、`src/PluginOps.cpp` 和 `qml/PluginsDialog.qml`。

uninstallPlugin 流程：

1. `pluginBusy` 为真时直接返回。
2. 查相关泊位，有正在运行的就发出 finished(false, "先停止：<泊位名…>") 并返回。
3. 如果 `removePackage` 为真且找不到 pnpm，发出 finished(false, "需要安装 pnpm") 并返回，不做任何修改。
4. 如果插件在 bundles 里，调用 `removeFromBundles`，失败就发出 finished(false, error)。
5. 如果 `removePackage` 为假，发出 finished(true, "已从配置移除")。
6. 否则把 busy 置为真，启动 `dsh plugin --profile <p> remove <name>`，工作目录设为 profile 目录。结束后 busy 置为假，按退出码发出 finished：成功时 message 为"已卸载"加 pnpm 输出摘要，失败时为"bundles 已移除，pnpm 失败"加输出摘要。

## Data Models

- `readPlugins` 返回 `{ok, error, plugins: [{name, version, enabled}]}`。`version` 取自 `dependencies`，只在 bundles 里的插件留空；`enabled` 表示它在 `dsh.profile.bundles` 中。
- 涉及的 `package.json` 字段只有 `dependencies`（只读）和 `dsh.profile.bundles`（读写）。

## Correctness Properties

- 列表永不包含 6 个自带 bundle（Req 1.1）。
- 不勾选时只有 `dsh.profile.bundles` 变化，`dependencies` 与 `node_modules` 不变（Req 2.3）。
- 相关泊位在运行、找不到 pnpm 时，磁盘上任何文件都不改动（Req 3.3、4.1）。
- 失败后不把插件加回 bundles（Req 2.6）。

## Error Handling

均通过 `pluginUninstallFinished(ok=false, message)` 报告：相关泊位在运行、缺 pnpm、`package.json` 读写失败、`dsh plugin remove` 非零退出（附输出摘要）。读取失败由 `listPlugins` 返回 `ok=false` 和 `error`。

## Testing Strategy

不加自动化测试。由用户编译运行后手动验证，步骤见 tasks.md 第 6 条。
