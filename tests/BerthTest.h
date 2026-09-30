#pragma once

#include <QObject>

#include <vector>

// 测试类自注册表。每个 tests/tst_*.cpp 在类定义之后写一行 BERTH_TEST(类名)，
// tests/main.cpp 会按类名顺序逐个执行，新增测试文件无需改 main。
//
// 用法：
//   class TstFoo : public QObject {
//       Q_OBJECT
//   private slots:
//       void bar();
//   };
//   void TstFoo::bar() { QVERIFY(true); }
//   BERTH_TEST(TstFoo)
//   #include "tst_foo.moc"
namespace BerthTest {

using Factory = QObject *(*)();

struct Entry {
    const char *name;
    Factory make;
};

// 函数内静态变量，避免跨翻译单元的静态初始化顺序问题
inline std::vector<Entry> &registry()
{
    static std::vector<Entry> entries;
    return entries;
}

inline bool registerTest(const char *name, Factory make)
{
    registry().push_back({name, make});
    return true;
}

} // namespace BerthTest

#define BERTH_TEST(Class)                                                          \
    namespace {                                                                    \
    [[maybe_unused]] const bool berthTestRegistered_##Class =                      \
        ::BerthTest::registerTest(#Class, []() -> QObject * { return new Class; }); \
    }
