#pragma once

#include "core/SettingsCodec.h"

#include <QObject>
#include <QString>
#include <QVariantList>

class DataStore;

class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dshExecutable READ dshExecutable WRITE setDshExecutable NOTIFY dshExecutableChanged)
    Q_PROPERTY(QString nodeExecutable READ nodeExecutable WRITE setNodeExecutable NOTIFY nodeExecutableChanged)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY startMinimizedChanged)
    Q_PROPERTY(bool openUiOnStart READ openUiOnStart WRITE setOpenUiOnStart NOTIFY openUiOnStartChanged)
    // 崩溃自动重启：基础间隔 1–60 秒，最大间隔 baseSec–600 秒，上限 1–20 次（setter 内夹紧）
    Q_PROPERTY(int autoRestartBaseSec READ autoRestartBaseSec WRITE setAutoRestartBaseSec NOTIFY autoRestartChanged)
    Q_PROPERTY(int autoRestartMaxSec READ autoRestartMaxSec WRITE setAutoRestartMaxSec NOTIFY autoRestartChanged)
    Q_PROPERTY(int autoRestartMaxAttempts READ autoRestartMaxAttempts WRITE setAutoRestartMaxAttempts NOTIFY autoRestartChanged)
    // 批量启动与错峰自启的间隔：0–60 秒（setter 内夹紧）
    Q_PROPERTY(int staggerSec READ staggerSec WRITE setStaggerSec NOTIFY staggerSecChanged)
    // 更新检查间隔：1–168 小时，0 表示关闭定时检查（setter 内夹紧）
    Q_PROPERTY(int updateCheckHours READ updateCheckHours WRITE setUpdateCheckHours NOTIFY updateCheckHoursChanged)
    // 为运行中泊位显示独立托盘图标（默认关闭）
    Q_PROPERTY(bool trayInstanceIcons READ trayInstanceIcons WRITE setTrayInstanceIcons NOTIFY trayInstanceIconsChanged)
    // 关闭主窗口时只隐藏到托盘（默认开启）；关闭时关窗即退出
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged)
    // 系统通知开关：崩溃 / 就绪 / 更新，默认全部开启
    Q_PROPERTY(bool notifyCrash READ notifyCrash WRITE setNotifyCrash NOTIFY notifyChanged)
    Q_PROPERTY(bool notifyReady READ notifyReady WRITE setNotifyReady NOTIFY notifyChanged)
    Q_PROPERTY(bool notifyUpdate READ notifyUpdate WRITE setNotifyUpdate NOTIFY notifyChanged)
    // settings.json 为只读模式（schema 过高或无法解析）时为 true，界面据此禁用保存
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)
    // 插件目录源地址；只接受 http:// 或 https://，经 setCatalogUrl 修改
    Q_PROPERTY(QString catalogUrl READ catalogUrl NOTIFY catalogUrlChanged)
    // 模型单价（每百万 token）：[{model, input, output, cache}]，按模型名排序；经 setModelPrice / removeModelPrice 修改
    Q_PROPERTY(QVariantList modelPrices READ modelPrices NOTIFY modelPricesChanged)

public:
    // store 由 AppController 持有，生命周期覆盖 Settings
    explicit Settings(DataStore *store, QObject *parent = nullptr);

    QString dshExecutable() const;
    void setDshExecutable(const QString &value);
    // 解析成可直接交给 QProcess 的完整路径（Windows 下按 PATHEXT 找 .cmd/.bat 等）
    QString resolvedDshExecutable() const;
    QString nodeExecutable() const;
    void setNodeExecutable(const QString &value);
    QString dataDir() const;
    bool startMinimized() const;
    void setStartMinimized(bool value);
    bool openUiOnStart() const;
    void setOpenUiOnStart(bool value);
    int autoRestartBaseSec() const;
    // 调大基础间隔时同步抬高最大间隔，保证 maxSec >= baseSec
    void setAutoRestartBaseSec(int value);
    int autoRestartMaxSec() const;
    void setAutoRestartMaxSec(int value);
    int autoRestartMaxAttempts() const;
    void setAutoRestartMaxAttempts(int value);
    int staggerSec() const;
    void setStaggerSec(int value);
    int updateCheckHours() const;
    void setUpdateCheckHours(int value);
    bool trayInstanceIcons() const;
    void setTrayInstanceIcons(bool value);
    bool closeToTray() const;
    void setCloseToTray(bool value);
    bool notifyCrash() const;
    void setNotifyCrash(bool value);
    bool notifyReady() const;
    void setNotifyReady(bool value);
    bool notifyUpdate() const;
    void setNotifyUpdate(bool value);
    bool readOnly() const;
    QString catalogUrl() const;
    // 校验通过（首尾空白去掉后）才写入，返回空串；不合法时保留原值并返回错误描述
    Q_INVOKABLE QString setCatalogUrl(const QString &value);
    // 插件安装渠道记录：key = "<home>|<profile>|<name>"；写入后立即保存 settings.json
    QString pluginChannel(const QString &key) const;
    void setPluginChannel(const QString &key, const QString &channel);
    // 模型单价（需求 22.3、22.9）
    QVariantList modelPrices() const;
    // 三项单价均为文本；任一项不合法（不在 0–10000 或超过 4 位小数）时不保存、保留原单价并返回错误；
    // 合法时写入并立即保存 settings.json，返回空串
    Q_INVOKABLE QString setModelPrice(const QString &model, const QString &input, const QString &output,
                                      const QString &cache);
    Q_INVOKABLE void removeModelPrice(const QString &model);

    // 代理与 npm 镜像（需求 16）：由 ProxyManager 校验后整体写入并立即保存，发 proxyChanged
    void setProxyData(const ProxySettings &proxy, const NpmRegistrySettings &registry,
                      bool mirrorFallback, bool instanceInheritProxy);

    // 完整设置数据（含新增字段与无法识别的字段）
    const SettingsData &data() const { return m_data; }

    void load();
    Q_INVOKABLE void save();

signals:
    void dshExecutableChanged();
    void nodeExecutableChanged();
    void startMinimizedChanged();
    void openUiOnStartChanged();
    void autoRestartChanged();
    void staggerSecChanged();
    void updateCheckHoursChanged();
    void trayInstanceIconsChanged();
    void closeToTrayChanged();
    void notifyChanged();
    void readOnlyChanged();
    void catalogUrlChanged();
    void modelPricesChanged();
    void proxyChanged();

private:
    DataStore *m_store;
    SettingsData m_data;
};
