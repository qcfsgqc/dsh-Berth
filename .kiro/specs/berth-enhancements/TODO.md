# berth-enhancements 待定决策

设计阶段留下、尚未拍板的问题。当前 design.md 按每条的"暂用"方案写；改选另一项时，只需改 design.md 对应段落和受影响的任务。

## 1. Skills 启用/禁用方式（需求 8）

dsh 的 Skill 没有"禁用"字段，skill-filesystem 只扫描 skills 根目录的第一层。

- A（暂用）：禁用时把 Skill 移到同目录下的 `.berth-disabled/` 子目录，启用时移回。只管理 `<DSH_HOME>/skills` 与泊位 workspace 的 `.dsh/skills`、`.agents/skills`。
- B：Skills 只显示、不能开关；MCP 照常可以开关。

影响：design.md「关键流程约定 → Skills 启用/禁用」、`ExtensionManager`、需求 8 第 2、5 条。

## 2. Token 用量的读取方式（需求 22）

dsh 会话日志默认 zstd 压缩，Qt 读不了。

- A（暂用）：Berth 内置 Node 脚本 `usage-scan.mjs`（从 qrc 释放到 `%APPDATA%\dsh-Berth\helpers\`），只读解析日志并输出聚合 JSON。dsh 要求 Node ≥22.19，自带 zstd 解压，不需额外依赖。
- B：用量统计这期不做，按需求 22 第 5 条降级显示。

影响：design.md「依赖调研结论」第 2 行、「关键流程约定 → 用量统计」、`StatsService`、E 阶段的用量任务。

## 实施安排

这两项都属于后期阶段（Skills 在 A 管理阶段，用量在 E 统计阶段），前面的阶段不受影响。开始对应阶段前再定。
