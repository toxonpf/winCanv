#pragma once

#include "control_service.h"
#include "workspace_manager.h"

#include <QIcon>
#include <QPropertyAnimation>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QThread>
#include <QWidget>

class QApplication;
class QListWidget;
class QLabel;
class QPushButton;
class QToolButton;
class QCheckBox;
class QFrame;
class QLocalServer;
class QLocalSocket;

class OperationThread : public QThread {
    Q_OBJECT

public:
    explicit OperationThread(std::function<QVariant()> fn, QObject *parent = nullptr);

signals:
    void succeeded(const QVariant &value);
    void failed(const QString &message);

protected:
    void run() override;

private:
    std::function<QVariant()> m_fn;
};

class WorkspacePanel : public QWidget {
    Q_OBJECT

public:
    WorkspacePanel(WorkspaceManager *manager, QSystemTrayIcon *trayIcon, const QString &mode);

    void toggle();
    void showPanel();
    void requestHide();
    void showDrawer();
    void hideDrawer();
    void toggleDrawer();
    void refreshTemplates();
    QString selectedName() const;
    void markQuitting();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    WorkspaceManager *m_manager = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QString m_mode;
    KWinController m_windowPositioner;
    OperationThread *m_thread = nullptr;
    OperationThread *m_positionThread = nullptr;
    QPropertyAnimation *m_animation = nullptr;
    bool m_hideAfterAnimation = false;
    bool m_quitting = false;
    int m_drawerWidth = 420;
    QFrame *m_contentFrame = nullptr;
    QListWidget *m_templateList = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_loadButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QToolButton *m_saveToolButton = nullptr;
    QToolButton *m_refreshToolButton = nullptr;
    QToolButton *m_closeToolButton = nullptr;
    QCheckBox *m_closeCheckbox = nullptr;

    static QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback);
    void setBusy(bool busy, const QString &message);
    void startOperation(std::function<QVariant()> fn, std::function<void(const QVariant &)> onSuccess);
    void operationFailed(const QString &message);
    void saveCurrentLayout();
    void saveFinished(const QVariant &value);
    void loadSelected();
    void loadFinished(const QVariant &value);
    void deleteSelected();
    void showNearTray();
    void repositionWithKWin(const QRect &available);
    void retryReposition(const QJsonObject &target);
    void beginShowDrawer();
    void handleAnimationFinished();
};

class LocalCommandServer : public QObject {
    Q_OBJECT

public:
    explicit LocalCommandServer(QObject *parent = nullptr);
    bool listen();
    void closeServer();

signals:
    void commandReceived(const QString &command);

private slots:
    void handleNewConnection();
    void readSocket(QLocalSocket *socket);

private:
    QLocalServer *m_server = nullptr;
};

class WorkspaceHost : public QObject {
    Q_OBJECT

public:
    WorkspaceHost(QApplication *app, bool showPanelOnStart);
    void quit();

private slots:
    void handleCommand(const QString &command);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void cleanup();

private:
    QApplication *m_app = nullptr;
    WorkspaceManager m_manager;
    ControlService m_controlService;
    LocalCommandServer m_commandServer;
    WorkspacePanel *m_panel = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;

    void setupTrayMenu();
};

bool sendPanelCommand(const QString &command, int timeoutMs = 500);
int runPanelHost(QApplication &app, bool showPanelOnStart);
int runStandalone(QApplication &app);
