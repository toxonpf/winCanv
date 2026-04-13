from __future__ import annotations

import os
from typing import Callable

from PyQt6.QtCore import QEasingCurve, QEvent, QObject, QPoint, QPropertyAnimation, QRect, QThread, Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QAction, QColor, QCursor, QGuiApplication, QIcon, QPainter, QPalette
from PyQt6.QtNetwork import QLocalServer, QLocalSocket
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QFrame,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMenu,
    QMessageBox,
    QPushButton,
    QSizePolicy,
    QStyle,
    QSystemTrayIcon,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from .config import APP_DOMAIN, APP_ID, APP_NAME, APP_ORGANIZATION
from .control_service import AppControlService
from .kwin_controller import KWinController
from .manager import LoadResult, SaveResult, WorkspaceTemplateManager


PANEL_SERVER_NAME = f"{APP_ID}.panel"


class OperationThread(QThread):
    succeeded = pyqtSignal(object)
    failed = pyqtSignal(str)

    def __init__(self, fn: Callable[[], object]) -> None:
        super().__init__()
        self._fn = fn

    def run(self) -> None:
        try:
            result = self._fn()
        except Exception as exc:  # noqa: BLE001
            self.failed.emit(str(exc))
            return
        self.succeeded.emit(result)


class WorkspacePanel(QWidget):
    def __init__(self, manager: WorkspaceTemplateManager, tray_icon: QSystemTrayIcon | None, mode: str = "drawer") -> None:
        self.mode = mode
        flags = Qt.WindowType.Window
        if mode == "popup":
            flags = Qt.WindowType.Popup | Qt.WindowType.FramelessWindowHint
        elif mode == "drawer":
            flags = (
                Qt.WindowType.Tool
                | Qt.WindowType.FramelessWindowHint
                | Qt.WindowType.NoDropShadowWindowHint
                | Qt.WindowType.WindowStaysOnTopHint
            )
        super().__init__(flags=flags)
        self.manager = manager
        self.tray_icon = tray_icon
        self.window_positioner = KWinController(self.manager.paths, self.manager.bridge, ignored_pids=[])
        self.thread: OperationThread | None = None
        self.position_thread: OperationThread | None = None
        self._animation = QPropertyAnimation(self, b"geometry", self)
        self._animation.setDuration(220)
        self._animation.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._animation.finished.connect(self._handle_animation_finished)
        self._hide_after_animation = False
        self._quitting = False
        self._drawer_width = 420
        self._last_target_geometry: QRect | None = None

        self.setWindowTitle(APP_NAME)
        self.setWindowIcon(self._theme_icon("preferences-desktop-workspaces", QStyle.StandardPixmap.SP_DesktopIcon))
        self.setMinimumWidth(360)
        self.setMinimumHeight(420)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        if self.mode == "drawer":
            self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)
            outer_layout = QHBoxLayout(self)
            outer_layout.setContentsMargins(0, 0, 0, 0)
            outer_layout.setSpacing(0)
        else:
            outer_layout = QVBoxLayout(self)
            outer_layout.setContentsMargins(0, 0, 0, 0)

        self.content_frame = QFrame()
        self.content_frame.setFrameShape(QFrame.Shape.StyledPanel)
        self.content_frame.setFrameShadow(QFrame.Shadow.Raised)
        if self.mode == "drawer":
            self.content_frame.setFixedWidth(self._drawer_width)
            panel_palette = self.content_frame.palette()
            panel_color = self.palette().color(QPalette.ColorRole.Window)
            panel_color.setAlpha(248)
            panel_palette.setColor(QPalette.ColorRole.Window, panel_color)
            self.content_frame.setPalette(panel_palette)
            self.content_frame.setAutoFillBackground(True)
            outer_layout.addWidget(self.content_frame, 0)
            outer_layout.addStretch(1)
        else:
            outer_layout.addWidget(self.content_frame)

        content_layout = QVBoxLayout(self.content_frame)
        content_layout.setContentsMargins(14, 12, 14, 12)
        content_layout.setSpacing(10)

        title_icon = QLabel()
        title_icon.setPixmap(self.windowIcon().pixmap(22, 22))

        self.title_label = QLabel("Шаблоны рабочих пространств")
        title_font = self.title_label.font()
        title_font.setBold(True)
        title_font.setPointSize(title_font.pointSize() + 1)
        self.title_label.setFont(title_font)

        self.save_tool_button = QToolButton()
        self.save_tool_button.setIcon(self._theme_icon("document-save", QStyle.StandardPixmap.SP_DialogSaveButton))
        self.save_tool_button.setToolTip("Сохранить текущее расположение")
        self.save_tool_button.clicked.connect(self.save_current_layout)
        self.save_tool_button.setAutoRaise(True)

        self.refresh_tool_button = QToolButton()
        self.refresh_tool_button.setIcon(self._theme_icon("view-refresh", QStyle.StandardPixmap.SP_BrowserReload))
        self.refresh_tool_button.setToolTip("Обновить список")
        self.refresh_tool_button.clicked.connect(self.refresh_templates)
        self.refresh_tool_button.setAutoRaise(True)

        self.close_tool_button = QToolButton()
        self.close_tool_button.setIcon(self._theme_icon("window-close", QStyle.StandardPixmap.SP_TitleBarCloseButton))
        self.close_tool_button.setToolTip("Скрыть панель")
        self.close_tool_button.clicked.connect(self.request_hide)
        self.close_tool_button.setAutoRaise(True)

        title_row = QHBoxLayout()
        title_row.setContentsMargins(0, 0, 0, 0)
        title_row.addWidget(title_icon)
        title_row.addWidget(self.title_label)
        title_row.addStretch(1)
        title_row.addWidget(self.save_tool_button)
        title_row.addWidget(self.refresh_tool_button)
        if mode == "drawer":
            title_row.addWidget(self.close_tool_button)

        self.template_list = QListWidget()
        self.template_list.itemDoubleClicked.connect(self.load_selected)
        self.template_list.setAlternatingRowColors(True)
        self.template_list.setSelectionMode(QListWidget.SelectionMode.SingleSelection)
        self.template_list.setUniformItemSizes(True)

        self.close_checkbox = QCheckBox("Закрыть текущие окна перед загрузкой")
        self.close_checkbox.setChecked(True)

        self.status_label = QLabel("Готово")
        self.status_label.setWordWrap(True)
        self.status_label.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Minimum)

        self.load_button = QPushButton("Загрузить шаблон")
        self.load_button.setIcon(self._theme_icon("system-run", QStyle.StandardPixmap.SP_MediaPlay))
        self.delete_button = QPushButton("Удалить")
        self.delete_button.setIcon(self._theme_icon("edit-delete", QStyle.StandardPixmap.SP_TrashIcon))

        self.load_button.clicked.connect(self.load_selected)
        self.delete_button.clicked.connect(self.delete_selected)

        divider = QFrame()
        divider.setFrameShape(QFrame.Shape.HLine)
        divider.setFrameShadow(QFrame.Shadow.Sunken)

        actions_row = QHBoxLayout()
        actions_row.setContentsMargins(0, 0, 0, 0)
        actions_row.addWidget(self.load_button)
        actions_row.addWidget(self.delete_button)

        content_layout.addLayout(title_row)
        content_layout.addWidget(self.template_list, 1)
        content_layout.addWidget(self.close_checkbox)
        content_layout.addWidget(divider)
        content_layout.addLayout(actions_row)
        content_layout.addWidget(self.status_label)

        self.refresh_templates()

    @staticmethod
    def _theme_icon(name: str, fallback: QStyle.StandardPixmap) -> QIcon:
        icon = QIcon.fromTheme(name)
        if icon.isNull():
            app = QApplication.instance()
            if app is not None:
                return app.style().standardIcon(fallback)
        return icon

    def toggle(self) -> None:
        if self.mode == "drawer":
            self.toggle_drawer()
            return
        if self.isVisible():
            self.hide()
        else:
            self.show_panel()

    def show_panel(self) -> None:
        if self.mode == "popup":
            self.show_near_tray()
        elif self.mode == "drawer":
            self.show_drawer()
        else:
            self.refresh_templates()
            self.show()
            self.raise_()
            self.activateWindow()

    def request_hide(self) -> None:
        if self.mode == "drawer":
            self.hide_drawer()
        else:
            self.hide()

    def show_near_tray(self) -> None:
        if self.tray_icon is None:
            self.refresh_templates()
            self.show()
            self.raise_()
            self.activateWindow()
            return
        self.refresh_templates()
        self.adjustSize()
        tray_rect = self.tray_icon.geometry()
        if tray_rect.isValid():
            x = tray_rect.right() - self.width()
            y = tray_rect.top() - self.height() - 8
            if y < 0:
                y = tray_rect.bottom() + 8
            self.move(QPoint(x, y))
        else:
            cursor = QCursor.pos()
            self.move(QPoint(cursor.x() - self.width() + 24, cursor.y() - self.height() - 8))
        self.show()
        self.raise_()
        self.activateWindow()

    def toggle_drawer(self) -> None:
        if self.isVisible():
            self.hide_drawer()
        else:
            self.show_drawer()

    def show_drawer(self) -> None:
        self.refresh_templates()
        self._hide_after_animation = False
        self._animation.stop()
        screen = QGuiApplication.screenAt(QCursor.pos()) or QGuiApplication.primaryScreen()
        available = screen.availableGeometry() if screen is not None else QRect(0, 0, 1920, 1080)
        self._last_target_geometry = available
        self.setGeometry(available)
        self.show()
        self.raise_()
        self.activateWindow()
        self._reposition_with_kwin(available)
        QTimer.singleShot(0, self._begin_show_drawer)

    def _reposition_with_kwin(self, available: QRect) -> None:
        target = {
            "x": available.x(),
            "y": available.y(),
            "width": available.width(),
            "height": available.height(),
        }
        if self.position_thread and self.position_thread.isRunning():
            return
        self.position_thread = OperationThread(
            lambda: self.window_positioner.place_window(
                pid=os.getpid(),
                caption=self.windowTitle(),
                geometry=target,
                timeout_ms=1600,
            )
        )
        self.position_thread.failed.connect(lambda _message: None)
        self.position_thread.start()
        QTimer.singleShot(220, lambda: self._retry_reposition(target))

    def _retry_reposition(self, target: dict[str, int]) -> None:
        self.position_thread = OperationThread(
            lambda: self.window_positioner.place_window(
                pid=os.getpid(),
                caption=self.windowTitle(),
                geometry=target,
                timeout_ms=1000,
            )
        )
        self.position_thread.failed.connect(lambda _message: None)
        self.position_thread.start()

    def _begin_show_drawer(self) -> None:
        hidden_geometry = QRect(-self._drawer_width, 0, self._drawer_width, self.height())
        visible_geometry = QRect(0, 0, self._drawer_width, self.height())
        self.content_frame.setGeometry(hidden_geometry)
        self._animation = QPropertyAnimation(self.content_frame, b"geometry", self)
        self._animation.setDuration(220)
        self._animation.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._animation.finished.connect(self._handle_animation_finished)
        self._animation.stop()
        self._animation.setStartValue(hidden_geometry)
        self._animation.setEndValue(visible_geometry)
        self._animation.start()

    def hide_drawer(self) -> None:
        if not self.isVisible():
            return
        self._hide_after_animation = True
        visible_geometry = QRect(max(0, self.content_frame.x()), 0, self._drawer_width, self.height())
        hidden_geometry = QRect(-self._drawer_width, 0, self._drawer_width, self.height())
        self._animation = QPropertyAnimation(self.content_frame, b"geometry", self)
        self._animation.setDuration(220)
        self._animation.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._animation.finished.connect(self._handle_animation_finished)
        self._animation.stop()
        self._animation.setStartValue(visible_geometry)
        self._animation.setEndValue(hidden_geometry)
        self._animation.start()

    def _handle_animation_finished(self) -> None:
        if self._hide_after_animation:
            self.hide()
            self._hide_after_animation = False

    def refresh_templates(self) -> None:
        current_name = self.selected_name()
        self.template_list.clear()
        for template in self.manager.list_templates():
            item = QListWidgetItem(f"{template.name}\n{len(template.windows)} окон")
            item.setData(Qt.ItemDataRole.UserRole, template.name)
            self.template_list.addItem(item)
            if current_name and current_name == template.name:
                self.template_list.setCurrentItem(item)
        if self.template_list.count() and not self.template_list.currentItem():
            self.template_list.setCurrentRow(0)
        self.status_label.setText("Готово")

    def selected_name(self) -> str | None:
        item = self.template_list.currentItem()
        if item is None:
            return None
        return str(item.data(Qt.ItemDataRole.UserRole))

    def set_busy(self, busy: bool, message: str) -> None:
        self.save_tool_button.setDisabled(busy)
        self.load_button.setDisabled(busy)
        self.refresh_tool_button.setDisabled(busy)
        self.delete_button.setDisabled(busy)
        self.close_checkbox.setDisabled(busy)
        self.status_label.setText(message)

    def start_operation(self, fn: Callable[[], object], on_success: Callable[[object], None]) -> None:
        if self.thread and self.thread.isRunning():
            return
        self.thread = OperationThread(fn)
        self.thread.succeeded.connect(on_success)
        self.thread.failed.connect(self.operation_failed)
        self.thread.finished.connect(lambda: self.set_busy(False, "Готово"))
        self.thread.start()

    def operation_failed(self, message: str) -> None:
        self.set_busy(False, f"Ошибка: {message}")
        if self.tray_icon is not None:
            self.tray_icon.showMessage(APP_NAME, message, QSystemTrayIcon.MessageIcon.Warning)

    def save_current_layout(self) -> None:
        name, accepted = QInputDialog.getText(self, APP_NAME, "Имя шаблона:", text="Рабочее пространство")
        if not accepted or not name.strip():
            return
        clean_name = name.strip()
        self.set_busy(True, f"Сохраняю '{clean_name}'...")
        self.start_operation(lambda: self.manager.save_template(clean_name), self.save_finished)

    def save_finished(self, result: object) -> None:
        save_result = result if isinstance(result, SaveResult) else None
        if save_result is None:
            self.operation_failed("Неожиданный результат сохранения")
            return
        self.refresh_templates()
        self.status_label.setText(f"Сохранено '{save_result.name}', окон: {save_result.window_count}")
        if self.tray_icon is not None:
            self.tray_icon.showMessage(APP_NAME, f"Сохранено '{save_result.name}'", QSystemTrayIcon.MessageIcon.Information)

    def load_selected(self, *_args: object) -> None:
        name = self.selected_name()
        if not name:
            self.status_label.setText("Сначала выбери шаблон")
            return
        close_existing = self.close_checkbox.isChecked()
        self.set_busy(True, f"Загружаю '{name}'...")
        self.start_operation(lambda: self.manager.load_template(name, close_existing=close_existing), self.load_finished)

    def load_finished(self, result: object) -> None:
        load_result = result if isinstance(result, LoadResult) else None
        if load_result is None:
            self.operation_failed("Неожиданный результат загрузки")
            return
        launched_ok = sum(1 for item in load_result.launched if item.ok)
        self.status_label.setText(
            f"Загружено '{load_result.template_name}': {load_result.applied_count}/{load_result.requested_windows}, "
            f"запущено {launched_ok}, не сопоставлено {len(load_result.unmatched_target_ids)}"
        )
        if self.tray_icon is not None:
            self.tray_icon.showMessage(
                APP_NAME,
                f"Загружено '{load_result.template_name}'",
                QSystemTrayIcon.MessageIcon.Information,
            )
        if self.mode == "drawer":
            QTimer.singleShot(180, self.hide_drawer)

    def delete_selected(self) -> None:
        name = self.selected_name()
        if not name:
            self.status_label.setText("Сначала выбери шаблон")
            return
        answer = QMessageBox.question(self, APP_NAME, f"Удалить шаблон '{name}'?")
        if answer != QMessageBox.StandardButton.Yes:
            return
        self.manager.delete_template(name)
        self.refresh_templates()
        self.status_label.setText(f"Удален '{name}'")

    def keyPressEvent(self, event) -> None:  # type: ignore[override]
        if self.mode == "drawer" and event.key() == Qt.Key.Key_Escape:
            self.hide_drawer()
            event.accept()
            return
        super().keyPressEvent(event)

    def closeEvent(self, event) -> None:  # type: ignore[override]
        if self.mode == "drawer" and not self._quitting:
            event.ignore()
            self.hide_drawer()
            return
        super().closeEvent(event)

    def mousePressEvent(self, event) -> None:  # type: ignore[override]
        if self.mode == "drawer" and not self.content_frame.geometry().contains(event.position().toPoint()):
            self.hide_drawer()
            event.accept()
            return
        super().mousePressEvent(event)

    def paintEvent(self, event) -> None:  # type: ignore[override]
        if self.mode == "drawer":
            painter = QPainter(self)
            scrim = QColor(self.palette().color(QPalette.ColorRole.Window))
            scrim.setAlpha(120)
            painter.fillRect(self.rect(), scrim)
            return
        super().paintEvent(event)

    def mark_quitting(self) -> None:
        self._quitting = True


class LocalCommandServer(QObject):
    command_received = pyqtSignal(str)

    def __init__(self, server_name: str) -> None:
        super().__init__()
        self.server_name = server_name
        self.server = QLocalServer(self)
        self.server.newConnection.connect(self._handle_new_connection)

    def listen(self) -> bool:
        if self.server.listen(self.server_name):
            return True
        QLocalServer.removeServer(self.server_name)
        return self.server.listen(self.server_name)

    def _handle_new_connection(self) -> None:
        while self.server.hasPendingConnections():
            socket = self.server.nextPendingConnection()
            socket.readyRead.connect(lambda sock=socket: self._read_socket(sock))
            socket.disconnected.connect(socket.deleteLater)

    def _read_socket(self, socket: QLocalSocket) -> None:
        command = bytes(socket.readAll()).decode("utf-8", errors="replace").strip()
        if command:
            self.command_received.emit(command)
        socket.disconnectFromServer()

    def close_server(self) -> None:
        self.server.close()


class WorkspaceHost:
    def __init__(self, app: QApplication, show_panel_on_start: bool = False) -> None:
        self.app = app
        self.app.setQuitOnLastWindowClosed(False)
        self.app.setApplicationName("workspace-templates")
        self.app.setApplicationDisplayName(APP_NAME)
        self.app.setDesktopFileName(APP_ID)
        self.app.setOrganizationName(APP_ORGANIZATION)
        self.app.setOrganizationDomain(APP_DOMAIN)

        self.manager = WorkspaceTemplateManager()
        self.control_service = AppControlService()
        self.control_service.command_received.connect(self.handle_command)
        self.command_server = LocalCommandServer(PANEL_SERVER_NAME)
        self.command_server.command_received.connect(self.handle_command)
        self.command_server.listen()
        self.panel = WorkspacePanel(self.manager, None, mode="drawer")

        icon = WorkspacePanel._theme_icon("preferences-desktop-workspaces", QStyle.StandardPixmap.SP_DesktopIcon)
        self.tray_icon: QSystemTrayIcon | None = None
        if QSystemTrayIcon.isSystemTrayAvailable():
            self.tray_icon = QSystemTrayIcon(icon, self.app)
            self.panel.tray_icon = self.tray_icon
            self._setup_tray_menu()
            self.tray_icon.activated.connect(self.on_tray_activated)
            self.tray_icon.show()
        self.app.aboutToQuit.connect(self.cleanup)

        if show_panel_on_start:
            QTimer.singleShot(0, self.panel.show_drawer)

    def _setup_tray_menu(self) -> None:
        assert self.tray_icon is not None
        menu = QMenu()
        show_action = QAction("Показать панель", menu)
        refresh_action = QAction("Обновить", menu)
        quit_action = QAction("Выход", menu)
        show_action.triggered.connect(self.panel.show_drawer)
        refresh_action.triggered.connect(self.panel.refresh_templates)
        quit_action.triggered.connect(self.quit)
        menu.addAction(show_action)
        menu.addAction(refresh_action)
        menu.addSeparator()
        menu.addAction(quit_action)
        self.tray_icon.setContextMenu(menu)

    def on_tray_activated(self, reason: QSystemTrayIcon.ActivationReason) -> None:
        if reason in (QSystemTrayIcon.ActivationReason.Trigger, QSystemTrayIcon.ActivationReason.DoubleClick):
            self.panel.toggle_drawer()

    def handle_command(self, command: str) -> None:
        if command == "toggle":
            self.panel.toggle_drawer()
        elif command == "show":
            self.panel.show_drawer()
        elif command == "hide":
            self.panel.hide_drawer()
        elif command == "quit":
            self.quit()

    def quit(self) -> None:
        self.panel.mark_quitting()
        self.panel.hide()
        if self.tray_icon is not None:
            self.tray_icon.hide()
        self.app.quit()

    def cleanup(self) -> None:
        self.panel.mark_quitting()
        self.command_server.close_server()
        self.control_service.stop()
        self.manager.shutdown()


def send_panel_command(command: str, timeout_ms: int = 500) -> bool:
    socket = QLocalSocket()
    socket.connectToServer(PANEL_SERVER_NAME)
    if not socket.waitForConnected(timeout_ms):
        return False
    socket.write(command.encode("utf-8"))
    socket.flush()
    socket.waitForBytesWritten(timeout_ms)
    socket.disconnectFromServer()
    return True


def run_panel_host(show_panel_on_start: bool = False) -> int:
    app = QApplication([])
    host = WorkspaceHost(app, show_panel_on_start=show_panel_on_start)
    return app.exec()


def run_standalone() -> int:
    app = QApplication([])
    app.setApplicationName("workspace-templates")
    app.setApplicationDisplayName(APP_NAME)
    app.setDesktopFileName(APP_ID)
    app.setOrganizationName(APP_ORGANIZATION)
    app.setOrganizationDomain(APP_DOMAIN)
    manager = WorkspaceTemplateManager()
    window = WorkspacePanel(manager, None, mode="window")
    window.resize(460, 560)
    window.show()
    app.aboutToQuit.connect(manager.shutdown)
    return app.exec()
