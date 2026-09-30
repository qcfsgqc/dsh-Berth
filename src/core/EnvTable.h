#pragma once

// 泊位环境变量表与额外启动参数的校验（纯逻辑）。
// 约束：只依赖 Qt6::Core。

#include "Instance.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace EnvTable {

constexpr int kMaxRows = 100;    // 环境变量表最多行数
constexpr int kMaxArgs = 50;     // 额外启动参数最多项数
constexpr int kMaxKeyLength = 256;

// 键名合法：非空、不含 '='、不含任何空白字符（QChar::isSpace）、长度 ≤ 256
bool isValidKey(const QString &key);

// 键名不区分大小写等于 "DSH_HOME"（该键由 Berth 注入，用户值不会生效；只提示，不阻止保存）
bool isDshHomeKey(const QString &key);

struct Validation {
    QList<int> invalidRows;   // 键名非法的行号（0 起，升序）
    QList<int> duplicateRows; // 键名不区分大小写重复的行号（同组全部行都列出；0 起，升序）
    QList<int> dshHomeRows;   // 键为 DSH_HOME 的行号（仅提示用，不影响 ok）
    bool tooManyRows = false; // 行数 > kMaxRows
    bool tooManyArgs = false; // 参数项数 > kMaxArgs

    // 违规行号并集（invalidRows ∪ duplicateRows，升序无重复）
    QList<int> badRows() const;

    bool ok() const
    {
        return invalidRows.isEmpty() && duplicateRows.isEmpty() && !tooManyRows && !tooManyArgs;
    }
};

// 校验环境变量表（仅看键）与额外参数数量。
// 重复判定只在非空键之间进行；空键已计入 invalidRows。
Validation validate(const QList<InstanceEnvVar> &rows, const QStringList &extraArgs = {});

} // namespace EnvTable
