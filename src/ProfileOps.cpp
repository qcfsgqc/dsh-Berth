#include "ProfileOps.h"

#include "PluginOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {

QString packageJsonPath(const QString &profileDir) {
    return QDir(profileDir).filePath(QStringLiteral("package.json"));
}

// 读取并解析 package.json 根对象。语义与 PluginOps 的 loadPackageJson 一致，但返回值不同：
// 返回 -1=目录不存在；0=可解析（root 已写出）；1=出错（error 已写出）；
// 2=目录存在但 package.json 缺失（不写 error，root 不变）。
enum class LoadResult { DirMissing = -1, Ok = 0, Failed = 1, PackageMissing = 2 };

LoadResult loadPackageJson(const QString &profileDir, QJsonObject *root, QString *error) {
    if (profileDir.isEmpty() || !QFileInfo(profileDir).isDir())
        return LoadResult::DirMissing;
    const QString path = packageJsonPath(profileDir);
    QFile file(path);
    if (!file.exists())
        return LoadResult::PackageMissing;
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取 %1：%2").arg(QDir::toNativeSeparators(path), file.errorString());
        return LoadResult::Failed;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("package.json 解析失败：%1（偏移 %2）")
                     .arg(parseError.errorString())
                     .arg(parseError.offset);
        return LoadResult::Failed;
    }
    if (!doc.isObject()) {
        *error = QStringLiteral("package.json 根节点不是对象");
        return LoadResult::Failed;
    }
    *root = doc.object();
    return LoadResult::Ok;
}

QStringList bundlesOf(const QJsonObject &root) {
    QStringList result;
    const QJsonArray bundles = root.value(QStringLiteral("dsh")).toObject()
                                   .value(QStringLiteral("profile")).toObject()
                                   .value(QStringLiteral("bundles")).toArray();
    for (const QJsonValue &v : bundles) {
        if (v.isString())
            result.append(v.toString());
    }
    return result;
}

// 递归删除一个普通目录（不含 symlink/junction 的分支；后者在调用方处理，只 unlink/rmdir 链接本身）。
// 出错时把第一条错误写进 firstError（若尚未写入）并返回 false。
bool removePlainDir(const QString &path, QString *firstError) {
    auto recordError = [&](const QString &msg) {
        if (firstError && firstError->isEmpty())
            *firstError = msg;
    };

    QDir dir(path);
    // Hidden | System 与 QDir::AllEntries 的默认组合即可覆盖 junction、隐藏文件等特殊条目
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &info : entries) {
        // junction/symlink：只删链接本身，绝不跟随链接去删目标内容
        if (info.isSymLink()) {
            if (!QFile::remove(info.absoluteFilePath()) && !QDir().rmdir(info.absoluteFilePath()))
                recordError(QStringLiteral("无法删除链接 %1（可能被占用）")
                                .arg(QDir::toNativeSeparators(info.absoluteFilePath())));
            continue;
        }
        if (info.isDir()) {
            if (!removePlainDir(info.absoluteFilePath(), firstError))
                return false;
            continue;
        }
        if (!QFile::remove(info.absoluteFilePath()))
            recordError(QStringLiteral("无法删除文件 %1（可能被占用）")
                            .arg(QDir::toNativeSeparators(info.absoluteFilePath())));
    }
    if (!QDir().rmdir(path)) {
        recordError(QStringLiteral("无法删除目录 %1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    return true;
}

} // namespace

QString ProfileOps::validateProfileName(const QString &name) {
    if (name.isEmpty())
        return QStringLiteral("profile 名不能为空");
    if (name.contains(QLatin1Char('/')))
        return QStringLiteral("profile 名不能包含路径分隔符");
    if (name.contains(QLatin1Char('\\')))
        return QStringLiteral("profile 名不能包含路径分隔符");
    const QString lower = name.toLower();
    if (lower == QLatin1String(".") || lower == QLatin1String(".."))
        return QStringLiteral("profile 名不能为 %1").arg(name);
    if (lower == QLatin1String("node_modules"))
        return QStringLiteral("profile 名不能为 %1（保留名）").arg(name);
    // Berth 额外禁保留名：desktop 由 DSH Desktop 独占管理
    if (name.compare(QLatin1String("desktop"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("profile 名不能为 %1（保留名）").arg(name);
    return {};
}

bool ProfileOps::removeProfileTree(const QString &dirPath, QString *error) {
    if (dirPath.isEmpty())
        return true;
    const QFileInfo info(dirPath);
    if (!info.exists() && !info.isSymLink())
        return true;
    // 顶层本身是链接（极少见）：只删链接本身，不进入其指向的位置
    if (info.isSymLink()) {
        const bool ok = QFile::remove(dirPath) || QDir().rmdir(dirPath);
        if (!ok && error)
            *error = QStringLiteral("无法删除链接 %1（可能被占用）").arg(QDir::toNativeSeparators(dirPath));
        return ok;
    }
    if (!info.isDir()) {
        // 普通文件被误传入：直接按文件删
        if (QFile::remove(dirPath))
            return true;
        if (error)
            *error = QStringLiteral("无法删除文件 %1").arg(QDir::toNativeSeparators(dirPath));
        return false;
    }

    QString firstError;
    removePlainDir(dirPath, &firstError);
    if (!firstError.isEmpty()) {
        if (error)
            *error = firstError;
        return false;
    }
    return true;
}

bool ProfileOps::copyProfileFiles(const QString &srcDir, const QString &dstDir, QString *error) {
    const QFileInfo srcInfo(srcDir);
    if (srcDir.isEmpty() || !srcInfo.isDir()) {
        if (error)
            *error = QStringLiteral("源 profile 目录不存在：%1").arg(QDir::toNativeSeparators(srcDir));
        return false;
    }
    if (dstDir.isEmpty()) {
        if (error)
            *error = QStringLiteral("目标 profile 目录为空");
        return false;
    }

    const QDir src(srcDir);
    QDir dst(dstDir);
    if (!dst.exists() && !dst.mkpath(dstDir)) {
        if (error)
            *error = QStringLiteral("无法创建目录 %1").arg(QDir::toNativeSeparators(dstDir));
        return false;
    }

    // 只复制用户配置相关文件；cordis.yml 每次启动都会重写、node_modules 由 pnpm install 重建
    static const QStringList files = {
        QStringLiteral("package.json"),
        QStringLiteral("pnpm-lock.yaml"),
        QStringLiteral("cordis.patch.yml"),
        QStringLiteral("pnpm-workspace.yaml"),
    };

    for (const QString &name : files) {
        const QString from = src.filePath(name);
        const QFileInfo info(from);
        if (!info.exists() || !info.isFile())
            continue;

        const QString to = dst.filePath(name);
        QFile dstFile(to);
        if (dstFile.exists() && !dstFile.remove()) {
            if (error)
                *error = QStringLiteral("无法覆盖 %1：%2")
                             .arg(QDir::toNativeSeparators(to), dstFile.errorString());
            return false;
        }
        // 保留源文件属性（Windows 上主要是只读位）
        if (!QFile::copy(from, to)) {
            if (error)
                *error = QStringLiteral("无法复制 %1 到 %2")
                             .arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to));
            return false;
        }
    }
    return true;
}

QVariantMap ProfileOps::readProfileInfo(const QString &profileDir) {
    QVariantMap result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("error"), QString());
    // 目录名（末段）作为展示名；调用者传入的是 $DSH_HOME/profiles/<name> 形式的完整目录。
    // cleanPath 去掉尾部分隔符，避免 fileName() 返回空串。
    const QString cleanDir = QDir::cleanPath(profileDir);
    result.insert(QStringLiteral("name"), QFileInfo(cleanDir).fileName());
    result.insert(QStringLiteral("dir"), QDir::toNativeSeparators(cleanDir));

    if (profileDir.isEmpty() || !QFileInfo(cleanDir).isDir()) {
        result.insert(QStringLiteral("exists"), false);
        result.insert(QStringLiteral("bundles"), QStringList());
        result.insert(QStringLiteral("pluginCount"), 0);
        return result;
    }

    result.insert(QStringLiteral("exists"), true);

    QJsonObject root;
    QString error;
    const LoadResult load = loadPackageJson(profileDir, &root, &error);
    if (load == LoadResult::Failed) {
        result[QStringLiteral("ok")] = false;
        result[QStringLiteral("error")] = error;
        result.insert(QStringLiteral("bundles"), QStringList());
        result.insert(QStringLiteral("pluginCount"), 0);
        return result;
    }

    // 目录存在但 package.json 缺失：视为无 bundle、不报错
    QStringList bundles;
    if (load == LoadResult::Ok)
        bundles = bundlesOf(root);

    int pluginCount = 0;
    for (const QString &name : bundles) {
        if (!PluginOps::isBuiltinBundle(name))
            ++pluginCount;
    }

    result.insert(QStringLiteral("bundles"), bundles);
    result.insert(QStringLiteral("pluginCount"), pluginCount);
    return result;
}
