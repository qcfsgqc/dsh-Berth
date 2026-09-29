# Requirements Document

> [!note] Spec 模式：tiny
> 本 spec 全程保持精简，后续 `design.md` 与 `tasks.md` 同样按 tiny 尺度产出：
> - `design.md`：只写关键技术决定、涉及的文件路径、数据结构或接口的变化点。不写架构论述、目录规划、测试策略、技术选型对比。控制在 60 行左右。
> - `tasks.md`：3 到 8 条任务，每条是一次能做完并单独验证的动作，带上要改的文件路径，末条为手动验证步骤。不拆出多级子任务，不加自动化测试任务（用户明确要求才加）。
> - 两份文件都不得扩大本文档界定的需求范围。

## Introduction

dsh 的插件通过官方 Web UI 的插件管理器安装到 profile 目录（写入 profile 的 `package.json` 依赖和 `dsh.profile.bundles`，由 pnpm 装进 `node_modules`）。某个插件装坏、导致实例起不来时，就没法再进 Web UI 卸载它。

本特性让用户在 DSH Berth 里直接查看某个泊位所用 profile 已安装的插件，并把插件卸载掉。只处理用户自己装的插件（profile `package.json` 的 `dependencies`），不碰 dsh 自带的 bundle。卸载时可以只从 profile 配置中移除插件，也可以连插件包一起卸掉。

## Requirements

### Requirement 1: 查看已安装插件

**User Story:** As a Berth 用户, I want 看到选中泊位的 profile 装了哪些插件, so that 我能找出要卸载的那个。

#### Acceptance Criteria

1. WHEN 用户选中一个泊位 THEN the system SHALL 列出该泊位 profile 的 `package.json` 中 `dependencies` 与 `dsh.profile.bundles` 里的用户插件（不含 dsh 自带 bundle），每项显示插件名、版本、是否在 `dsh.profile.bundles` 中
2. WHEN 插件列表为空 THEN the system SHALL 显示"没有已安装的插件"
3. IF profile 目录或 `package.json` 不存在、无法解析 THEN the system SHALL 显示读取失败原因，不显示列表
4. WHEN 用户在列表处点刷新 THEN the system SHALL 重新读取 `package.json` 并更新列表

### Requirement 2: 卸载插件

**User Story:** As a Berth 用户, I want 在 Berth 里卸载一个插件, so that 插件坏掉时不用进 Web UI 也能恢复实例。

#### Acceptance Criteria

1. WHEN 用户对某个插件点卸载 THEN the system SHALL 先弹出确认，写明插件名和 profile 名
2. WHEN 弹出卸载确认 THEN the system SHALL 提供"同时卸载插件包"勾选框，默认不勾选
3. WHEN 用户确认 AND 未勾选 THEN the system SHALL 只从 `dsh.profile.bundles` 中移除该插件，`dependencies` 与 `node_modules` 保持原样
4. WHEN 用户确认 AND 已勾选 THEN the system SHALL 从 `dsh.profile.bundles` 中移除该插件，并在该 profile 目录调用本机 pnpm 卸载该插件包
5. WHEN 卸载完成 THEN the system SHALL 刷新插件列表并提示卸载成功
6. IF 卸载过程中任一步失败 THEN the system SHALL 停在失败的那一步，不执行后续步骤、不把插件加回 `dsh.profile.bundles`，并提示失败原因和当前实际状态
7. WHILE 卸载进行中 the system SHALL 禁用该列表的卸载按钮，界面不卡死

### Requirement 3: 卸载插件包时的 pnpm 处理

**User Story:** As a Berth 用户, I want 勾选卸载插件包时连同它带进来、已经没人用的 node 包一起清掉, so that profile 里不残留没用的依赖。

#### Acceptance Criteria

1. WHEN 调用 pnpm 卸载插件包 THEN the system SHALL 按 pnpm 默认行为处理其依赖包：不再被 profile 中任何包用到的移除，仍被使用的保留
2. WHEN pnpm 卸载完成 THEN the system SHALL 在结果提示中显示 pnpm 输出的变更摘要
3. IF 已勾选 AND 本机找不到 pnpm THEN the system SHALL 提示需要安装 pnpm，不做任何修改
### Requirement 4: 运行中实例的保护

**User Story:** As a Berth 用户, I want 卸载前不会误伤正在跑的实例, so that 不会出现进程占用文件或状态错乱。

#### Acceptance Criteria

1. IF 任一使用同一 `DSH_HOME` + profile 的泊位处于运行、启动中或停止中 THEN the system SHALL 拒绝卸载，并提示先停止这些泊位
2. WHEN 所有相关泊位都已停止或失败 THEN the system SHALL 允许卸载
