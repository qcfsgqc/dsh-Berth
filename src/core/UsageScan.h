#pragma once

#include <QList>
#include <QString>

#include "UsageAgg.h"

// 解析 assets/helpers/usage-scan.mjs 的输出（纯逻辑，只依赖 Qt6::Core）。
// 输出逐行：
//   @usage {"day":"YYYY-MM-DD","project":"...","model":"...","input":N,"output":N,"cache":N}
//   @summary {"rootExists":bool,"files":N,"skippedFiles":N,"skippedLines":N,"events":N,"zstd":bool}
//   @error {"message":"..."}
// 其他行（Node 警告等）忽略。
namespace UsageScan {

struct Output {
    bool complete = false;      // 见到 @summary 或 @error
    QString error;              // @error 的内容；为空表示成功
    bool rootExists = false;
    bool zstdSupported = true;
    int files = 0;
    int skippedFiles = 0;
    int skippedLines = 0;
    int events = 0;             // 含 usage 的事件数（区间内）
    QList<UsageAgg::Record> records; // berth 由调用方填写；day 已由脚本按本地日给出
};

// berth：写入每条 Record 的泊位 id
Output parse(const QString &text, const QString &berth);

} // namespace UsageScan
