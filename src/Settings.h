#pragma once

#include <QObject>
#include <QString>

class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dshExecutable READ dshExecutable WRITE setDshExecutable NOTIFY dshExecutableChanged)
    Q_PROPERTY(QString nodeExecutable READ nodeExecutable WRITE setNodeExecutable NOTIFY nodeExecutableChanged)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY startMinimizedChanged)
    Q_PROPERTY(bool openUiOnStart READ openUiOnStart WRITE setOpenUiOnStart NOTIFY openUiOnStartChanged)

public:
    explicit Settings(QObject *parent = nullptr);

    QString dshExecutable() const;
    void setDshExecutable(const QString &value);
    QString nodeExecutable() const;
    void setNodeExecutable(const QString &value);
    QString dataDir() const;
    bool startMinimized() const;
    void setStartMinimized(bool value);
    bool openUiOnStart() const;
    void setOpenUiOnStart(bool value);

    void load();
    Q_INVOKABLE void save();

signals:
    void dshExecutableChanged();
    void nodeExecutableChanged();
    void startMinimizedChanged();
    void openUiOnStartChanged();

private:
    QString filePath() const;

    QString m_dshExecutable = QStringLiteral("dsh");
    QString m_nodeExecutable;
    bool m_startMinimized = false;
    bool m_openUiOnStart = true;
};
