#include "workspace_panel.h"

#include "constants.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScreen>
#include <QSizePolicy>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>

OperationThread::OperationThread(std::function<QVariant()> fn, QObject *parent)
    : QThread(parent)
    , m_fn(std::move(fn))
{
}

void OperationThread::run()
{
    try {
        emit succeeded(m_fn());
    } catch (const std::exception &exc) {
        emit failed(QString::fromUtf8(exc.what()));
    } catch (...) {
        emit failed(QStringLiteral("Unknown error"));
    }
}

static QVariant toVariant(const SaveResult &result)
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), result.name);
    map.insert(QStringLiteral("path"), result.path);
    map.insert(QStringLiteral("window_count"), result.windowCount);
    return map;
}

static QVariant toVariant(const LoadResult &result)
{
    QVariantMap map;
    map.insert(QStringLiteral("template_name"), result.templateName);
    map.insert(QStringLiteral("requested_windows"), result.requestedWindows);
    map.insert(QStringLiteral("applied_count"), result.appliedCount);
    map.insert(QStringLiteral("unmatched_count"), result.unmatchedTargetIds.size());
    int launchedOk = 0;
    for (const LaunchResult &item : result.launched) {
        if (item.ok) {
            ++launchedOk;
        }
    }
    map.insert(QStringLiteral("launched_ok"), launchedOk);
    return map;
}

WorkspacePanel::WorkspacePanel(WorkspaceManager *manager, QSystemTrayIcon *trayIcon, const QString &mode)
    : QWidget(nullptr)
    , m_manager(manager)
    , m_trayIcon(trayIcon)
    , m_mode(mode)
    , m_windowPositioner(manager->paths(), manager->bridge(), {})
{
    Qt::WindowFlags flags = Qt::Window;
    if (m_mode == QStringLiteral("popup")) {
        flags = Qt::Popup | Qt::FramelessWindowHint;
    } else if (m_mode == QStringLiteral("drawer")) {
        flags = Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    setWindowTitle(kAppName);
    setWindowIcon(themeIcon(QStringLiteral("preferences-desktop-workspaces"), QStyle::SP_DesktopIcon));
    setMinimumWidth(360);
    setMinimumHeight(420);
    setFocusPolicy(Qt::StrongFocus);

    m_animation = new QPropertyAnimation(this, QByteArrayLiteral("geometry"), this);
    m_animation->setDuration(220);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this, &WorkspacePanel::handleAnimationFinished);

    QBoxLayout *outer = (m_mode == QStringLiteral("drawer")) ? static_cast<QBoxLayout *>(new QHBoxLayout(this))
                                                             : static_cast<QBoxLayout *>(new QVBoxLayout(this));
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_contentFrame = new QFrame(this);
    m_contentFrame->setFrameShape(QFrame::StyledPanel);
    m_contentFrame->setFrameShadow(QFrame::Raised);
    if (m_mode == QStringLiteral("drawer")) {
        m_contentFrame->setFixedWidth(m_drawerWidth);
        QPalette palette = m_contentFrame->palette();
        QColor panelColor = this->palette().color(QPalette::Window);
        panelColor.setAlpha(248);
        palette.setColor(QPalette::Window, panelColor);
        m_contentFrame->setPalette(palette);
        m_contentFrame->setAutoFillBackground(true);
        outer->addWidget(m_contentFrame, 0);
        outer->addStretch(1);
    } else {
        outer->addWidget(m_contentFrame);
    }

    auto *content = new QVBoxLayout(m_contentFrame);
    content->setContentsMargins(14, 12, 14, 12);
    content->setSpacing(10);

    auto *titleIcon = new QLabel(m_contentFrame);
    titleIcon->setPixmap(windowIcon().pixmap(22, 22));

    auto *titleLabel = new QLabel(QStringLiteral("Шаблоны рабочих пространств"), m_contentFrame);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    m_saveToolButton = new QToolButton(m_contentFrame);
    m_saveToolButton->setIcon(themeIcon(QStringLiteral("document-save"), QStyle::SP_DialogSaveButton));
    m_saveToolButton->setAutoRaise(true);
    connect(m_saveToolButton, &QToolButton::clicked, this, &WorkspacePanel::saveCurrentLayout);

    m_refreshToolButton = new QToolButton(m_contentFrame);
    m_refreshToolButton->setIcon(themeIcon(QStringLiteral("view-refresh"), QStyle::SP_BrowserReload));
    m_refreshToolButton->setAutoRaise(true);
    connect(m_refreshToolButton, &QToolButton::clicked, this, &WorkspacePanel::refreshTemplates);

    m_closeToolButton = new QToolButton(m_contentFrame);
    m_closeToolButton->setIcon(themeIcon(QStringLiteral("window-close"), QStyle::SP_TitleBarCloseButton));
    m_closeToolButton->setAutoRaise(true);
    connect(m_closeToolButton, &QToolButton::clicked, this, &WorkspacePanel::requestHide);

    auto *titleRow = new QHBoxLayout();
    titleRow->addWidget(titleIcon);
    titleRow->addWidget(titleLabel);
    titleRow->addStretch(1);
    titleRow->addWidget(m_saveToolButton);
    titleRow->addWidget(m_refreshToolButton);
    if (m_mode == QStringLiteral("drawer")) {
        titleRow->addWidget(m_closeToolButton);
    }

    m_templateList = new QListWidget(m_contentFrame);
    m_templateList->setAlternatingRowColors(true);
    m_templateList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_templateList, &QListWidget::itemDoubleClicked, this, [this]() { loadSelected(); });

    m_closeCheckbox = new QCheckBox(QStringLiteral("Закрыть текущие окна перед загрузкой"), m_contentFrame);
    m_closeCheckbox->setChecked(true);

    m_statusLabel = new QLabel(QStringLiteral("Готово"), m_contentFrame);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    m_loadButton = new QPushButton(QStringLiteral("Загрузить шаблон"), m_contentFrame);
    m_loadButton->setIcon(themeIcon(QStringLiteral("system-run"), QStyle::SP_MediaPlay));
    connect(m_loadButton, &QPushButton::clicked, this, &WorkspacePanel::loadSelected);

    m_deleteButton = new QPushButton(QStringLiteral("Удалить"), m_contentFrame);
    m_deleteButton->setIcon(themeIcon(QStringLiteral("edit-delete"), QStyle::SP_TrashIcon));
    connect(m_deleteButton, &QPushButton::clicked, this, &WorkspacePanel::deleteSelected);

    auto *divider = new QFrame(m_contentFrame);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);

    auto *actions = new QHBoxLayout();
    actions->addWidget(m_loadButton);
    actions->addWidget(m_deleteButton);

    content->addLayout(titleRow);
    content->addWidget(m_templateList, 1);
    content->addWidget(m_closeCheckbox);
    content->addWidget(divider);
    content->addLayout(actions);
    content->addWidget(m_statusLabel);

    refreshTemplates();
}

QIcon WorkspacePanel::themeIcon(const QString &name, QStyle::StandardPixmap fallback)
{
    const QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull()) {
        return icon;
    }
    return QApplication::style()->standardIcon(fallback);
}

void WorkspacePanel::toggle()
{
    if (m_mode == QStringLiteral("drawer")) {
        toggleDrawer();
    } else if (isVisible()) {
        hide();
    } else {
        showPanel();
    }
}

void WorkspacePanel::showPanel()
{
    if (m_mode == QStringLiteral("popup")) {
        showNearTray();
    } else if (m_mode == QStringLiteral("drawer")) {
        showDrawer();
    } else {
        refreshTemplates();
        show();
        raise();
        activateWindow();
    }
}

void WorkspacePanel::requestHide()
{
    if (m_mode == QStringLiteral("drawer")) {
        hideDrawer();
    } else {
        hide();
    }
}

void WorkspacePanel::showNearTray()
{
    refreshTemplates();
    adjustSize();
    if (m_trayIcon && m_trayIcon->geometry().isValid()) {
        const QRect trayRect = m_trayIcon->geometry();
        int x = trayRect.right() - width();
        int y = trayRect.top() - height() - 8;
        if (y < 0) {
            y = trayRect.bottom() + 8;
        }
        move(x, y);
    } else {
        const QPoint cursor = QCursor::pos();
        move(cursor.x() - width() + 24, cursor.y() - height() - 8);
    }
    show();
    raise();
    activateWindow();
}

void WorkspacePanel::showDrawer()
{
    refreshTemplates();
    m_hideAfterAnimation = false;
    m_animation->stop();
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    setGeometry(available);
    show();
    raise();
    activateWindow();
    repositionWithKWin(available);
    QTimer::singleShot(0, this, &WorkspacePanel::beginShowDrawer);
}

void WorkspacePanel::repositionWithKWin(const QRect &available)
{
    if (m_positionThread && m_positionThread->isRunning()) {
        return;
    }
    const QJsonObject target{
        {QStringLiteral("x"), available.x()},
        {QStringLiteral("y"), available.y()},
        {QStringLiteral("width"), available.width()},
        {QStringLiteral("height"), available.height()},
    };
    m_positionThread = new OperationThread([this, target]() -> QVariant {
        m_windowPositioner.placeWindow(QCoreApplication::applicationPid(), target, windowTitle(), 1600);
        return {};
    }, this);
    connect(m_positionThread, &OperationThread::failed, this, [](const QString &) {});
    connect(m_positionThread, &OperationThread::finished, this, [this]() { m_positionThread = nullptr; });
    connect(m_positionThread, &OperationThread::finished, m_positionThread, &QObject::deleteLater);
    m_positionThread->start();
    QTimer::singleShot(220, this, [this, target]() { retryReposition(target); });
}

void WorkspacePanel::retryReposition(const QJsonObject &target)
{
    auto *thread = new OperationThread([this, target]() -> QVariant {
        m_windowPositioner.placeWindow(QCoreApplication::applicationPid(), target, windowTitle(), 1000);
        return {};
    }, this);
    connect(thread, &OperationThread::failed, this, [](const QString &) {});
    connect(thread, &OperationThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void WorkspacePanel::beginShowDrawer()
{
    const QRect hidden(-m_drawerWidth, 0, m_drawerWidth, height());
    const QRect visible(0, 0, m_drawerWidth, height());
    m_contentFrame->setGeometry(hidden);
    delete m_animation;
    m_animation = new QPropertyAnimation(m_contentFrame, QByteArrayLiteral("geometry"), this);
    m_animation->setDuration(220);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this, &WorkspacePanel::handleAnimationFinished);
    m_animation->setStartValue(hidden);
    m_animation->setEndValue(visible);
    m_animation->start();
}

void WorkspacePanel::hideDrawer()
{
    if (!isVisible()) {
        return;
    }
    m_hideAfterAnimation = true;
    const QRect visible(std::max(0, m_contentFrame->x()), 0, m_drawerWidth, height());
    const QRect hidden(-m_drawerWidth, 0, m_drawerWidth, height());
    delete m_animation;
    m_animation = new QPropertyAnimation(m_contentFrame, QByteArrayLiteral("geometry"), this);
    m_animation->setDuration(220);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this, &WorkspacePanel::handleAnimationFinished);
    m_animation->setStartValue(visible);
    m_animation->setEndValue(hidden);
    m_animation->start();
}

void WorkspacePanel::toggleDrawer()
{
    if (isVisible()) {
        hideDrawer();
    } else {
        showDrawer();
    }
}

void WorkspacePanel::handleAnimationFinished()
{
    if (m_hideAfterAnimation) {
        hide();
        m_hideAfterAnimation = false;
    }
}

void WorkspacePanel::refreshTemplates()
{
    const QString current = selectedName();
    m_templateList->clear();
    const QVector<WorkspaceTemplate> templates = m_manager->listTemplates();
    for (const WorkspaceTemplate &workspace : templates) {
        auto *item = new QListWidgetItem(QStringLiteral("%1\n%2 окон").arg(workspace.name).arg(workspace.windows.size()));
        item->setData(Qt::UserRole, workspace.name);
        m_templateList->addItem(item);
        if (!current.isEmpty() && current == workspace.name) {
            m_templateList->setCurrentItem(item);
        }
    }
    if (m_templateList->count() > 0 && !m_templateList->currentItem()) {
        m_templateList->setCurrentRow(0);
    }
    m_statusLabel->setText(QStringLiteral("Готово"));
}

QString WorkspacePanel::selectedName() const
{
    auto *item = m_templateList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void WorkspacePanel::setBusy(bool busy, const QString &message)
{
    m_saveToolButton->setDisabled(busy);
    m_loadButton->setDisabled(busy);
    m_refreshToolButton->setDisabled(busy);
    m_deleteButton->setDisabled(busy);
    m_closeCheckbox->setDisabled(busy);
    m_statusLabel->setText(message);
}

void WorkspacePanel::startOperation(std::function<QVariant()> fn, std::function<void(const QVariant &)> onSuccess)
{
    if (m_thread && m_thread->isRunning()) {
        return;
    }
    m_thread = new OperationThread(std::move(fn), this);
    connect(m_thread, &OperationThread::succeeded, this, [this, onSuccess](const QVariant &value) {
        setBusy(false, QStringLiteral("Готово"));
        onSuccess(value);
    });
    connect(m_thread, &OperationThread::failed, this, &WorkspacePanel::operationFailed);
    connect(m_thread, &OperationThread::finished, this, [this]() { m_thread = nullptr; });
    connect(m_thread, &OperationThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}

void WorkspacePanel::operationFailed(const QString &message)
{
    setBusy(false, QStringLiteral("Ошибка: %1").arg(message));
    if (m_trayIcon) {
        m_trayIcon->showMessage(kAppName, message, QSystemTrayIcon::Warning);
    }
}

void WorkspacePanel::saveCurrentLayout()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, kAppName, QStringLiteral("Имя шаблона:"), QLineEdit::Normal,
        QStringLiteral("Рабочее пространство"), &accepted);
    if (!accepted || name.trimmed().isEmpty()) {
        return;
    }
    const QString cleanName = name.trimmed();
    setBusy(true, QStringLiteral("Сохраняю '%1'...").arg(cleanName));
    startOperation([this, cleanName]() { return toVariant(m_manager->saveTemplate(cleanName)); },
        [this](const QVariant &value) { saveFinished(value); });
}

void WorkspacePanel::saveFinished(const QVariant &value)
{
    const QVariantMap map = value.toMap();
    refreshTemplates();
    m_statusLabel->setText(QStringLiteral("Сохранено '%1', окон: %2").arg(map.value(QStringLiteral("name")).toString()).arg(map.value(QStringLiteral("window_count")).toInt()));
    if (m_trayIcon) {
        m_trayIcon->showMessage(kAppName, QStringLiteral("Сохранено '%1'").arg(map.value(QStringLiteral("name")).toString()), QSystemTrayIcon::Information);
    }
}

void WorkspacePanel::loadSelected()
{
    const QString name = selectedName();
    if (name.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Сначала выбери шаблон"));
        return;
    }
    setBusy(true, QStringLiteral("Загружаю '%1'...").arg(name));
    const bool closeExisting = m_closeCheckbox->isChecked();
    startOperation([this, name, closeExisting]() { return toVariant(m_manager->loadTemplate(name, closeExisting)); },
        [this](const QVariant &value) { loadFinished(value); });
}

void WorkspacePanel::loadFinished(const QVariant &value)
{
    const QVariantMap map = value.toMap();
    m_statusLabel->setText(QStringLiteral("Загружено '%1': %2/%3, запущено %4, не сопоставлено %5")
        .arg(map.value(QStringLiteral("template_name")).toString())
        .arg(map.value(QStringLiteral("applied_count")).toInt())
        .arg(map.value(QStringLiteral("requested_windows")).toInt())
        .arg(map.value(QStringLiteral("launched_ok")).toInt())
        .arg(map.value(QStringLiteral("unmatched_count")).toInt()));
    if (m_trayIcon) {
        m_trayIcon->showMessage(kAppName, QStringLiteral("Загружено '%1'").arg(map.value(QStringLiteral("template_name")).toString()), QSystemTrayIcon::Information);
    }
    if (m_mode == QStringLiteral("drawer")) {
        QTimer::singleShot(180, this, &WorkspacePanel::hideDrawer);
    }
}

void WorkspacePanel::deleteSelected()
{
    const QString name = selectedName();
    if (name.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Сначала выбери шаблон"));
        return;
    }
    if (QMessageBox::question(this, kAppName, QStringLiteral("Удалить шаблон '%1'?").arg(name)) != QMessageBox::Yes) {
        return;
    }
    m_manager->deleteTemplate(name);
    refreshTemplates();
    m_statusLabel->setText(QStringLiteral("Удален '%1'").arg(name));
}

void WorkspacePanel::markQuitting()
{
    m_quitting = true;
}

void WorkspacePanel::keyPressEvent(QKeyEvent *event)
{
    if (m_mode == QStringLiteral("drawer") && event->key() == Qt::Key_Escape) {
        hideDrawer();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void WorkspacePanel::closeEvent(QCloseEvent *event)
{
    if (m_mode == QStringLiteral("drawer") && !m_quitting) {
        event->ignore();
        hideDrawer();
        return;
    }
    QWidget::closeEvent(event);
}

void WorkspacePanel::mousePressEvent(QMouseEvent *event)
{
    if (m_mode == QStringLiteral("drawer") && !m_contentFrame->geometry().contains(event->position().toPoint())) {
        hideDrawer();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WorkspacePanel::paintEvent(QPaintEvent *event)
{
    if (m_mode == QStringLiteral("drawer")) {
        QPainter painter(this);
        QColor scrim = palette().color(QPalette::Window);
        scrim.setAlpha(120);
        painter.fillRect(rect(), scrim);
        return;
    }
    QWidget::paintEvent(event);
}

LocalCommandServer::LocalCommandServer(QObject *parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    connect(m_server, &QLocalServer::newConnection, this, &LocalCommandServer::handleNewConnection);
}

bool LocalCommandServer::listen()
{
    if (m_server->listen(kPanelServerName)) {
        return true;
    }
    QLocalServer::removeServer(kPanelServerName);
    return m_server->listen(kPanelServerName);
}

void LocalCommandServer::closeServer()
{
    m_server->close();
}

void LocalCommandServer::handleNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { readSocket(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void LocalCommandServer::readSocket(QLocalSocket *socket)
{
    const QString command = QString::fromUtf8(socket->readAll()).trimmed();
    if (!command.isEmpty()) {
        emit commandReceived(command);
    }
    socket->disconnectFromServer();
}

WorkspaceHost::WorkspaceHost(QApplication *app, bool showPanelOnStart)
    : QObject(app)
    , m_app(app)
{
    m_app->setQuitOnLastWindowClosed(false);
    m_app->setApplicationName(QStringLiteral("workspace-templates"));
    m_app->setApplicationDisplayName(kAppName);
    m_app->setDesktopFileName(kAppId);
    m_app->setOrganizationName(kAppOrganization);
    m_app->setOrganizationDomain(kAppDomain);

    connect(&m_controlService, &ControlService::commandReceived, this, &WorkspaceHost::handleCommand);
    connect(&m_commandServer, &LocalCommandServer::commandReceived, this, &WorkspaceHost::handleCommand);
    m_commandServer.listen();

    m_panel = new WorkspacePanel(&m_manager, nullptr, QStringLiteral("drawer"));
    QIcon icon = QIcon::fromTheme(QStringLiteral("preferences-desktop-workspaces"));
    if (icon.isNull()) {
        icon = m_app->style()->standardIcon(QStyle::SP_DesktopIcon);
    }
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(icon, this);
        delete m_panel;
        m_panel = new WorkspacePanel(&m_manager, m_trayIcon, QStringLiteral("drawer"));
        setupTrayMenu();
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, &WorkspaceHost::onTrayActivated);
        m_trayIcon->show();
    }
    connect(m_app, &QCoreApplication::aboutToQuit, this, &WorkspaceHost::cleanup);

    if (showPanelOnStart) {
        QTimer::singleShot(0, m_panel, &WorkspacePanel::showDrawer);
    }
}

void WorkspaceHost::setupTrayMenu()
{
    if (!m_trayIcon) {
        return;
    }
    auto *menu = new QMenu();
    QAction *showAction = menu->addAction(QStringLiteral("Показать панель"));
    QAction *refreshAction = menu->addAction(QStringLiteral("Обновить"));
    menu->addSeparator();
    QAction *quitAction = menu->addAction(QStringLiteral("Выход"));
    connect(showAction, &QAction::triggered, m_panel, &WorkspacePanel::showDrawer);
    connect(refreshAction, &QAction::triggered, m_panel, &WorkspacePanel::refreshTemplates);
    connect(quitAction, &QAction::triggered, this, &WorkspaceHost::quit);
    m_trayIcon->setContextMenu(menu);
}

void WorkspaceHost::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        m_panel->toggleDrawer();
    }
}

void WorkspaceHost::handleCommand(const QString &command)
{
    if (command == QStringLiteral("toggle")) {
        m_panel->toggleDrawer();
    } else if (command == QStringLiteral("show")) {
        m_panel->showDrawer();
    } else if (command == QStringLiteral("hide")) {
        m_panel->hideDrawer();
    } else if (command == QStringLiteral("quit")) {
        quit();
    }
}

void WorkspaceHost::quit()
{
    m_panel->markQuitting();
    m_panel->hide();
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    m_app->quit();
}

void WorkspaceHost::cleanup()
{
    m_panel->markQuitting();
    m_commandServer.closeServer();
}

bool sendPanelCommand(const QString &command, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(kPanelServerName);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }
    socket.write(command.toUtf8());
    socket.flush();
    socket.waitForBytesWritten(timeoutMs);
    socket.disconnectFromServer();
    return true;
}

int runPanelHost(QApplication &app, bool showPanelOnStart)
{
    WorkspaceHost host(&app, showPanelOnStart);
    return app.exec();
}

int runStandalone(QApplication &app)
{
    app.setApplicationName(QStringLiteral("workspace-templates"));
    app.setApplicationDisplayName(kAppName);
    app.setDesktopFileName(kAppId);
    app.setOrganizationName(kAppOrganization);
    app.setOrganizationDomain(kAppDomain);
    WorkspaceManager manager;
    WorkspacePanel panel(&manager, nullptr, QStringLiteral("window"));
    panel.resize(460, 560);
    panel.show();
    return app.exec();
}
