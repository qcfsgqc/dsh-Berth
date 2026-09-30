#include "BerthTest.h"

#include "core/UsageScan.h"

#include <QTest>

// usage-scan.mjs 输出解析（任务 26.4，需求 22.1、22.5、22.8）
class TstUsageScan : public QObject {
    Q_OBJECT
private slots:
    void parsesRecordsAndSummary();
    void ignoresNoiseAndBadLines();
    void reportsScriptError();
    void incompleteWithoutSummary();
};

void TstUsageScan::parsesRecordsAndSummary() {
    const QString text = QStringLiteral(
        "@usage {\"day\":\"2025-03-01\",\"project\":\"--G-ws--\",\"model\":\"deepseek-chat\",\"input\":10,\"output\":5,\"cache\":2}\n"
        "@usage {\"day\":\"2025-03-02\",\"project\":\"--G-ws--\",\"model\":\"\",\"input\":1,\"output\":0,\"cache\":0}\n"
        "@summary {\"rootExists\":true,\"files\":3,\"skippedFiles\":1,\"skippedLines\":4,\"events\":2,\"zstd\":true}");
    const UsageScan::Output out = UsageScan::parse(text, QStringLiteral("b1"));
    QVERIFY(out.complete);
    QVERIFY(out.error.isEmpty());
    QVERIFY(out.rootExists);
    QCOMPARE(out.files, 3);
    QCOMPARE(out.skippedFiles, 1);
    QCOMPARE(out.skippedLines, 4);
    QCOMPARE(out.records.size(), 2);
    QCOMPARE(out.records[0].berth, QStringLiteral("b1"));
    QCOMPARE(out.records[0].day, QDate(2025, 3, 1));
    QCOMPARE(out.records[0].model, QStringLiteral("deepseek-chat"));
QCOMPARE(*out.records[0].input, qint64(10));
QCOMPARE(*out.records[0].output, qint64(5));
QCOMPARE(*out.records[0].cache, qint64(2));
    QVERIFY(out.records[1].model.isEmpty()); // 交给 UsageAgg 归入"未知模型"
}

void TstUsageScan::ignoresNoiseAndBadLines() {
    const QString text = QStringLiteral(
        "(node:123) ExperimentalWarning: zstd\n"
        "@usage {broken\n"
        "@usage {\"day\":\"not-a-day\",\"input\":1}\n"
        "@usage {\"day\":\"2025-03-01\",\"model\":\"m\"}\n"
        "@summary {\"rootExists\":false,\"zstd\":false}");
    const UsageScan::Output out = UsageScan::parse(text, QStringLiteral("b"));
    QVERIFY(out.complete);
    QVERIFY(!out.rootExists);
    QVERIFY(!out.zstdSupported);
    QCOMPARE(out.records.size(), 1);
    QVERIFY(!out.records[0].input.has_value()); // 缺失字段按 0 计由 UsageAgg 处理
}

void TstUsageScan::reportsScriptError() {
    const UsageScan::Output out =
        UsageScan::parse(QStringLiteral("@error {\"message\":\"missing --root\"}"), QStringLiteral("b"));
    QVERIFY(out.complete);
    QCOMPARE(out.error, QStringLiteral("missing --root"));
}

void TstUsageScan::incompleteWithoutSummary() {
    const UsageScan::Output out = UsageScan::parse(
        QStringLiteral("SyntaxError: Unexpected token\n@usage {\"day\":\"2025-03-01\",\"input\":1}"),
        QStringLiteral("b"));
    QVERIFY(!out.complete);
    QCOMPARE(out.records.size(), 1);
}

BERTH_TEST(TstUsageScan)
#include "tst_usage_scan.moc"
