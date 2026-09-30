#pragma once

// Berth 数据文件（settings.json / instances.json / metrics.json / quarantine.json）的 schema 规则。
// 纯逻辑：不读写磁盘，只根据字节内容和调用方给出的状态做决策，磁盘操作由 DataStore 执行。
// 约束：只依赖 Qt6::Core。

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace DataFile {

// 顶层版本字段名
inline constexpr const char *kSchemaKey = "schemaVersion";

enum class Mode {
    Normal,   // 可正常读写
    ReadOnly, // schema 版本高于支持版本：照常显示，禁止写回
    Corrupt,  // 不是合法 JSON 或顶层结构不对：不覆盖、不删除，也不产生写入计划
};

struct LoadResult {
    Mode mode = Mode::Normal;
    int version = 0;     // 文件中的 schema 版本；缺少 schemaVersion 为 0；Corrupt 时为 -1
    QJsonObject root;    // 顶层对象（已去掉 schemaVersion）；schema 0 的顶层数组包装为 {arrayKey: [...]}
    QString error;       // Corrupt 时的原因（便于日志）
};

// 解析数据文件字节。
// supported：当前支持的 schema 版本。
// arrayKey：schema 0 顶层数组要包装进的键名（instances.json 为 "instances"）；
//           传空字符串表示该文件不接受顶层数组，遇到时按 Corrupt 处理。
// schemaVersion 存在但不是非负整数时按 Corrupt 处理；顶层数组只可能是 schema 0。
LoadResult load(const QByteArray &bytes, int supported,
                const QString &arrayKey = QStringLiteral("instances"));

// 文件不存在时使用的加载结果：Normal、当前版本、空对象（写回时不需要备份）
LoadResult fresh(int supported);

// 备份文件名：<file>.bak-<oldVer>，file 可以是文件名或完整路径
QString backupName(const QString &file, int oldVer);

// 把 known（当前版本识别并序列化出的字段）合并到 original（读取时的顶层对象）上：
// original 中无法识别的顶层字段原样保留，同名键以 known 为准，最后写入 schemaVersion = supported。
QJsonObject merge(const QJsonObject &original, const QJsonObject &known, int supported);

enum class StepKind {
    Backup, // 把 bytes（原文件原字节）写到 path；path 已存在时不应生成此步骤
    Write,  // 把 bytes 写到 path（应使用 QSaveFile）
};

struct WriteStep {
    StepKind kind = StepKind::Write;
    QString path;
    QByteArray bytes;
};

struct WritePlan {
    bool allowed = false; // ReadOnly / Corrupt 时为 false，steps 为空
    QString reason;       // 不允许写回的原因
    QList<WriteStep> steps; // 按顺序执行；任一步失败即中止（备份失败则放弃写回）
};

// 生成写回计划。
// file：目标文件路径；loaded：读取时的 LoadResult（文件不存在时用 fresh()）；
// originalBytes：读取时的原字节（文件不存在时为空）；known：当前版本序列化出的字段；
// backupExists：backupName(file, loaded.version) 是否已存在（已存在则跳过备份，不覆盖）。
// 低版本且原文件存在时，第一步是 Backup。
WritePlan planWrite(const QString &file, const LoadResult &loaded, const QByteArray &originalBytes,
                    const QJsonObject &known, int supported, bool backupExists);

// 序列化为写盘字节（缩进格式）
QByteArray toBytes(const QJsonObject &obj);

// 类型错误的字段标识："文件名.字段名"（嵌套字段用前缀串联，如 settings.json.autoRestart.baseSec）
QString fieldPath(const QString &prefix, const QString &field);

// 按类型读取字段：缺少字段时静默返回默认值；字段存在但类型不对时返回默认值，
// 并把 fieldPath(prefix, key) 追加到 errors（errors 可为 nullptr）。
class FieldReader {
public:
    FieldReader(const QJsonObject &obj, const QString &prefix, QStringList *errors);

    bool has(const QString &key) const;
    QString string(const QString &key, const QString &def = QString()) const;
    bool boolean(const QString &key, bool def) const;
    int integer(const QString &key, int def) const; // 必须是整数值且在 int 范围内
    double number(const QString &key, double def) const;
    QJsonObject object(const QString &key) const;
    QJsonArray array(const QString &key) const;
    // 子对象读取器；字段存在但不是对象时记录错误并返回空对象的读取器
    FieldReader child(const QString &key) const;

    const QJsonObject &raw() const { return m_obj; }
    const QString &prefix() const { return m_prefix; }

private:
    void report(const QString &key) const;

    QJsonObject m_obj;
    QString m_prefix;
    QStringList *m_errors;
};

} // namespace DataFile
