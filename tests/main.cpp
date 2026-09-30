#include "BerthTest.h"

#include <QCoreApplication>
#include <QTest>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

// berth_tests 入口：依次执行所有经 BERTH_TEST 注册的测试类。
// 任一类失败则返回 1；没有注册任何类时返回 0。
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::vector<BerthTest::Entry> entries = BerthTest::registry();
    // 注册顺序取决于链接顺序，按类名排序保证每次运行顺序一致
    std::sort(entries.begin(), entries.end(),
              [](const BerthTest::Entry &a, const BerthTest::Entry &b) {
                  return std::strcmp(a.name, b.name) < 0;
              });

    if (entries.empty()) {
        std::printf("berth_tests: no test classes registered\n");
        return 0;
    }

    int failedClasses = 0;
    for (const BerthTest::Entry &entry : entries) {
        std::unique_ptr<QObject> test(entry.make());
        if (QTest::qExec(test.get(), argc, argv) != 0)
            ++failedClasses;
    }

    std::printf("berth_tests: %d test class(es), %d failed\n",
                int(entries.size()), failedClasses);
    return failedClasses == 0 ? 0 : 1;
}
