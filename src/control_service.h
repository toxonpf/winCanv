#pragma once

#include <QDBusConnection>
#include <QObject>

class ControlService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.toxonpf.workspacetemplates")

public:
    explicit ControlService(QObject *parent = nullptr);
    ~ControlService() override;

signals:
    void commandReceived(const QString &command);

public slots:
    bool TogglePanel();
    bool ShowPanel();
    bool HidePanel();
    bool Quit();

private:
    QDBusConnection m_connection;
};
