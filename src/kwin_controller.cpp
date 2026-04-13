#include "kwin_controller.h"

#include "constants.h"

#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMutexLocker>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

namespace {
QString jsString(const QString &value)
{
    QJsonArray array;
    array.append(value);
    const QString compact = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return compact.mid(1, compact.size() - 2);
}

QString jsJson(const QJsonValue &value)
{
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}
}

ResultBridge::ResultBridge(QObject *parent)
    : QObject(parent)
    , m_connection(QDBusConnection::sessionBus())
{
    m_serviceName = kResultInterface + QStringLiteral(".Instance") + QString::number(QCoreApplication::applicationPid()) +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_connection.registerService(m_serviceName)) {
        throw std::runtime_error(QStringLiteral("Failed to register D-Bus service: %1").arg(m_connection.lastError().message()).toStdString());
    }
    if (!m_connection.registerObject(kResultObjectPath, this, QDBusConnection::ExportAllSlots)) {
        throw std::runtime_error(QStringLiteral("Failed to register D-Bus object: %1").arg(m_connection.lastError().message()).toStdString());
    }
}

ResultBridge::~ResultBridge()
{
    m_connection.unregisterObject(kResultObjectPath);
    m_connection.unregisterService(m_serviceName);
}

QDBusConnection ResultBridge::connection() const
{
    return m_connection;
}

QString ResultBridge::serviceName() const
{
    return m_serviceName;
}

bool ResultBridge::ReportResult(const QString &requestId, const QString &payloadJson)
{
    QMutexLocker locker(&m_mutex);
    m_results.insert(requestId, QJsonDocument::fromJson(payloadJson.toUtf8()).object());
    m_condition.wakeAll();
    return true;
}

QJsonObject ResultBridge::waitFor(const QString &requestId, int timeoutMs)
{
    QMutexLocker locker(&m_mutex);
    while (!m_results.contains(requestId)) {
        if (!m_condition.wait(&m_mutex, timeoutMs)) {
            throw std::runtime_error(QStringLiteral("Timed out waiting for KWin response for request %1").arg(requestId).toStdString());
        }
    }
    return m_results.take(requestId);
}

QJsonObject RunningScript::wait(int timeoutMs) const
{
    return bridge->waitFor(requestId, timeoutMs);
}

void RunningScript::stop() const
{
    if (scriptId > 0) {
        QDBusInterface script(
            QStringLiteral("org.kde.KWin"),
            KWinController::scriptObjectPath(scriptId),
            QStringLiteral("org.kde.kwin.Script"),
            connection
        );
        script.call(QStringLiteral("stop"));
    }
    if (!sourcePath.isEmpty()) {
        QFile::remove(sourcePath);
    }
}

KWinController::KWinController(const AppPaths &paths, ResultBridge *bridge, QList<qint64> ignoredPids)
    : m_paths(paths)
    , m_bridge(bridge)
    , m_connection(bridge->connection())
    , m_ignoredPids(std::move(ignoredPids))
{
}

QString KWinController::scriptObjectPath(int scriptId)
{
    return QStringLiteral("/Scripting/Script%1").arg(scriptId);
}

QJsonObject KWinController::captureWorkspace() const
{
    const QJsonObject payload = runRequest(captureScript(), 5000);
    if (!payload.value(QStringLiteral("ok")).toBool()) {
        throw std::runtime_error(payload.value(QStringLiteral("error")).toString(QStringLiteral("Failed to capture workspace")).toStdString());
    }
    return payload;
}

QJsonObject KWinController::closeWorkspaceWindows(const QStringList &exceptIds) const
{
    const QJsonObject payload = runRequest(closeScript(exceptIds), 6000);
    if (!payload.value(QStringLiteral("ok")).toBool()) {
        throw std::runtime_error(payload.value(QStringLiteral("error")).toString(QStringLiteral("Failed to close windows")).toStdString());
    }
    return payload;
}

RunningScript KWinController::startRestore(const QJsonArray &targets, int timeoutMs, int settleMs) const
{
    return startRequest(restoreScript(targets, timeoutMs, settleMs));
}

QJsonObject KWinController::placeWindow(qint64 pid, const QJsonObject &geometry, const QString &caption, int timeoutMs) const
{
    const QJsonObject payload = runRequest(placeWindowScript(pid, geometry, caption, timeoutMs), std::max(2000, timeoutMs + 1000));
    if (!payload.value(QStringLiteral("ok")).toBool()) {
        throw std::runtime_error(payload.value(QStringLiteral("error")).toString(QStringLiteral("Failed to place window")).toStdString());
    }
    return payload;
}

QJsonObject KWinController::runRequest(const QString &scriptSource, int timeoutMs) const
{
    RunningScript running = startRequest(scriptSource);
    try {
        const QJsonObject payload = running.wait(timeoutMs);
        running.stop();
        return payload;
    } catch (...) {
        running.stop();
        throw;
    }
}

RunningScript KWinController::startRequest(const QString &scriptSource) const
{
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString source = wrapScript(requestId, scriptSource);
    const QString sourcePath = m_paths.runtimeDir() + QStringLiteral("/kwin-") + requestId + QStringLiteral(".js");
    QFile file(sourcePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw std::runtime_error(QStringLiteral("Unable to write KWin script: %1").arg(sourcePath).toStdString());
    }
    file.write(source.toUtf8());
    file.close();

    QDBusInterface scripting(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"),
        m_connection
    );
    QDBusReply<int> loadReply = scripting.call(QStringLiteral("loadScript"), sourcePath);
    if (!loadReply.isValid()) {
        throw std::runtime_error(QStringLiteral("Unable to load KWin script: %1").arg(loadReply.error().message()).toStdString());
    }

    const int scriptId = loadReply.value();
    QDBusInterface script(
        QStringLiteral("org.kde.KWin"),
        scriptObjectPath(scriptId),
        QStringLiteral("org.kde.kwin.Script"),
        m_connection
    );
    QDBusReply<void> runReply = script.call(QStringLiteral("run"));
    if (!runReply.isValid()) {
        throw std::runtime_error(QStringLiteral("Unable to run KWin script: %1").arg(runReply.error().message()).toStdString());
    }

    return {requestId, scriptId, sourcePath, m_bridge, m_connection};
}

QString KWinController::wrapScript(const QString &requestId, const QString &scriptBody) const
{
    QJsonArray ignored;
    for (qint64 pid : m_ignoredPids) {
        ignored.append(pid);
    }
    return QStringLiteral(
               "\"use strict\";\n"
               "const SERVICE = %1;\n"
               "const PATH = %2;\n"
               "const INTERFACE = %3;\n"
               "const REQUEST_ID = %4;\n"
               "const IGNORED_PIDS = %5;\n"
               "function sendResult(payload) { callDBus(SERVICE, PATH, INTERFACE, \"ReportResult\", REQUEST_ID, JSON.stringify(payload)); }\n"
               "function fail(errorMessage) { sendResult({ ok: false, error: String(errorMessage) }); }\n"
               "try {\n%6\n} catch (error) {\n"
               "  fail(error && error.toString ? error.toString() : error);\n"
               "}\n")
        .arg(jsString(m_bridge->serviceName()),
            jsString(kResultObjectPath),
            jsString(kResultInterface),
            jsString(requestId),
            jsJson(ignored),
            scriptBody);
}

QString KWinController::captureScript() const
{
    return QString::fromUtf8(R"(
function shouldIncludeWindow(window) {
    if (!window) return false;
    if (!window.managed || window.deleted) return false;
    if (window.desktopWindow || window.dock || window.popupWindow || window.outline) return false;
    if (window.specialWindow) return false;
    if (!(window.normalWindow || window.dialog || window.utility)) return false;
    if (IGNORED_PIDS.indexOf(window.pid) !== -1) return false;
    return true;
}

function rectData(rect) {
    if (!rect) return null;
    return {
        x: Math.round(rect.x),
        y: Math.round(rect.y),
        width: Math.round(rect.width),
        height: Math.round(rect.height)
    };
}

function desktopNumber(window) {
    if (window.onAllDesktops || !window.desktops || window.desktops.length === 0) {
        return 0;
    }
    const desktop = window.desktops[0];
    if (desktop.x11DesktopNumber !== undefined) return desktop.x11DesktopNumber;
    if (desktop.id !== undefined) return desktop.id;
    return 0;
}

const outputs = (workspace.screens || []).map(function(output) {
    return {
        name: output.name || "",
        geometry: rectData(output.geometry)
    };
});

const windows = workspace.stackingOrder
    .filter(shouldIncludeWindow)
    .map(function(window) {
        return {
            internal_id: String(window.internalId),
            pid: window.pid,
            desktop_file_name: window.desktopFileName !== undefined ? window.desktopFileName : "",
            resource_name: window.resourceName || "",
            resource_class: window.resourceClass || "",
            window_role: window.windowRole || "",
            caption: window.caption || "",
            geometry: rectData(window.frameGeometry),
            desktop: desktopNumber(window),
            output_name: window.output && window.output.name ? window.output.name : "",
            output_geometry: window.output ? rectData(window.output.geometry) : null,
            full_screen: !!window.fullScreen,
            maximize_mode: Number(window.maximizeMode || 0)
        };
    });

sendResult({
    ok: true,
    windows: windows,
    outputs: outputs
});
)");
}

QString KWinController::closeScript(const QStringList &exceptIds) const
{
    QJsonArray ids;
    for (const QString &id : exceptIds) {
        ids.append(id);
    }
    return QStringLiteral(R"(
const CLOSE = %1;
const closed = [];

function shouldClose(window) {
    if (!window) return false;
    if (!window.managed || window.deleted) return false;
    if (window.desktopWindow || window.dock || window.popupWindow || window.outline) return false;
    if (window.specialWindow) return false;
    if (IGNORED_PIDS.indexOf(window.pid) !== -1) return false;
    if (CLOSE.except_ids.indexOf(String(window.internalId)) !== -1) return false;
    return window.closeable;
}

workspace.stackingOrder.forEach(function(window) {
    if (!shouldClose(window)) return;
    closed.push(String(window.internalId));
    window.closeWindow();
});

sendResult({
    ok: true,
    closed_count: closed.length,
    closed_ids: closed
});
)")
        .arg(jsJson(QJsonObject{{QStringLiteral("except_ids"), ids}}));
}

QString KWinController::restoreScript(const QJsonArray &targets, int timeoutMs, int settleMs) const
{
    const QJsonObject payload{
        {QStringLiteral("targets"), targets},
        {QStringLiteral("timeout_ms"), timeoutMs},
        {QStringLiteral("settle_ms"), settleMs},
    };

    return QStringLiteral(R"(
const RESTORE = %1;

function rectData(rect) {
    if (!rect) return null;
    return {
        x: Math.round(rect.x),
        y: Math.round(rect.y),
        width: Math.round(rect.width),
        height: Math.round(rect.height)
    };
}

function shouldUseWindow(window) {
    if (!window) return false;
    if (!window.managed || window.deleted) return false;
    if (window.desktopWindow || window.dock || window.popupWindow || window.outline) return false;
    if (window.specialWindow) return false;
    if (!(window.normalWindow || window.dialog || window.utility)) return false;
    if (IGNORED_PIDS.indexOf(window.pid) !== -1) return false;
    return true;
}

function findDesktop(desktopNumber) {
    if (!desktopNumber) return null;
    for (const desktop of workspace.desktops || []) {
        if (desktop.x11DesktopNumber === desktopNumber || desktop.id === desktopNumber) {
            return desktop;
        }
    }
    return null;
}

function findOutput(outputName) {
    for (const output of workspace.screens || []) {
        if ((output.name || "") === (outputName || "")) return output;
    }
    return null;
}

function buildRect(x, y, width, height) {
    return { x: x, y: y, width: width, height: height };
}

function clamp(value, minimum, maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

function targetRect(target) {
    if (!target.geometry) return null;
    let x = target.geometry.x;
    let y = target.geometry.y;
    const width = target.geometry.width;
    const height = target.geometry.height;

    if (target.output_name && target.output_geometry) {
        const currentOutput = findOutput(target.output_name);
        if (currentOutput && currentOutput.geometry) {
            const dx = target.geometry.x - target.output_geometry.x;
            const dy = target.geometry.y - target.output_geometry.y;
            const maxX = Math.round(currentOutput.geometry.x + Math.max(0, currentOutput.geometry.width - width));
            const maxY = Math.round(currentOutput.geometry.y + Math.max(0, currentOutput.geometry.height - height));
            x = clamp(Math.round(currentOutput.geometry.x + dx), Math.round(currentOutput.geometry.x), maxX);
            y = clamp(Math.round(currentOutput.geometry.y + dy), Math.round(currentOutput.geometry.y), maxY);
        }
    }
    return buildRect(x, y, width, height);
}

function score(window, target) {
    let value = 0;
    if (target.desktop_file_name && window.desktopFileName === target.desktop_file_name) value += 100;
    if (target.resource_class && window.resourceClass === target.resource_class) value += 35;
    if (target.resource_name && window.resourceName === target.resource_name) value += 20;
    if (target.window_role && window.windowRole === target.window_role) value += 10;
    if (target.caption && window.caption === target.caption) value += 3;
    return value;
}

function geometryDistance(window, target) {
    if (!target.geometry || !window.frameGeometry) return 1000000;
    const dx = Math.abs(window.frameGeometry.x - target.geometry.x);
    const dy = Math.abs(window.frameGeometry.y - target.geometry.y);
    const dw = Math.abs(window.frameGeometry.width - target.geometry.width);
    const dh = Math.abs(window.frameGeometry.height - target.geometry.height);
    return dx + dy + dw + dh;
}

function assignWindow(window, target, force) {
    if (!target || !window) return;
    const windowId = String(window.internalId);
    if (target.applied && (!force || target.matched_window !== windowId)) return;

    if (target.desktop) {
        const desktop = findDesktop(target.desktop);
        if (desktop && !window.onAllDesktops) {
            window.desktops = [desktop];
        }
    }

    const desired = targetRect(target);
    if (desired) {
        if (window.fullScreen) {
            window.fullScreen = false;
        }
        if (window.setMaximize) {
            window.setMaximize(false, false);
        }
        const nextRect = Object.assign({}, window.frameGeometry);
        nextRect.x = desired.x;
        nextRect.y = desired.y;
        nextRect.width = desired.width;
        nextRect.height = desired.height;
        window.frameGeometry = nextRect;
    }

    const wantsFullScreen = !!target.full_screen;
    const maximizeMode = Number(target.maximize_mode || 0);
    const maximizeVertically = (maximizeMode & 1) !== 0;
    const maximizeHorizontally = (maximizeMode & 2) !== 0;

    if (wantsFullScreen) {
        if (window.setMaximize) {
            window.setMaximize(false, false);
        }
        if (!window.fullScreen) {
            window.fullScreen = true;
        }
    } else {
        if (window.fullScreen) {
            window.fullScreen = false;
        }
        if (window.setMaximize) {
            window.setMaximize(maximizeVertically, maximizeHorizontally);
        }
    }

    target.applied = true;
    target.matched_window = windowId;
    target.matched_geometry = rectData(window.frameGeometry);
}

function bestTargetFor(window) {
    let best = null;
    let bestScore = -1;
    let bestDistance = 1000000;
    for (const target of RESTORE.targets) {
        if (target.applied) continue;
        const currentScore = score(window, target);
        if (currentScore <= 0) continue;
        const currentDistance = geometryDistance(window, target);
        if (currentScore > bestScore || (currentScore === bestScore && currentDistance < bestDistance)) {
            best = target;
            bestScore = currentScore;
            bestDistance = currentDistance;
        }
    }
    return best;
}

function sweepExistingWindows() {
    for (const window of workspace.stackingOrder) {
        if (!shouldUseWindow(window)) continue;
        const target = bestTargetFor(window);
        if (!target) continue;
        assignWindow(window, target);
    }
}

let finished = false;
const timers = [];

function finish(reason) {
    if (finished) return;
    finished = true;
    for (const timer of timers) {
        timer.stop();
    }
    sendResult({
        ok: true,
        reason: reason,
        applied: RESTORE.targets.filter(function(target) { return target.applied; }).map(function(target) {
            return {
                target_id: target.id,
                matched_window: target.matched_window || "",
                matched_geometry: target.matched_geometry || null
            };
        }),
        unmatched: RESTORE.targets.filter(function(target) { return !target.applied; }).map(function(target) {
            return target.id;
        })
    });
}

function maybeFinish() {
    const pending = RESTORE.targets.filter(function(target) { return !target.applied; });
    if (pending.length === 0) {
        finish("complete");
    }
}

function singleShot(delayMs, callback) {
    const timer = new QTimer();
    timer.singleShot = true;
    timer.timeout.connect(callback);
    timer.start(delayMs);
    timers.push(timer);
}

workspace.windowAdded.connect(function(window) {
    if (!shouldUseWindow(window)) return;
    singleShot(RESTORE.settle_ms, function() {
        const target = bestTargetFor(window);
        if (!target) return;
        assignWindow(window, target, false);
        singleShot(250, function() {
            assignWindow(window, target, true);
            maybeFinish();
        });
    });
});

sweepExistingWindows();
maybeFinish();
singleShot(RESTORE.timeout_ms, function() { finish("timeout"); });
)")
        .arg(jsJson(payload));
}

QString KWinController::placeWindowScript(qint64 pid, const QJsonObject &geometry, const QString &caption, int timeoutMs) const
{
    const QJsonObject payload{
        {QStringLiteral("pid"), static_cast<qint64>(pid)},
        {QStringLiteral("caption"), caption},
        {QStringLiteral("geometry"), geometry},
        {QStringLiteral("timeout_ms"), timeoutMs},
    };

    return QStringLiteral(R"(
const PLACE = %1;
let finished = false;
const timers = [];

function singleShot(delayMs, callback) {
    const timer = new QTimer();
    timer.singleShot = true;
    timer.timeout.connect(callback);
    timer.start(delayMs);
    timers.push(timer);
}

function finish(ok, extra) {
    if (finished) return;
    finished = true;
    for (const timer of timers) {
        timer.stop();
    }
    sendResult(Object.assign({ ok: ok }, extra || {}));
}

function shouldMatch(window) {
    if (!window || !window.managed || window.deleted) return false;
    if (window.pid !== PLACE.pid) return false;
    if (PLACE.caption && window.caption !== PLACE.caption) return false;
    return true;
}

function apply(window) {
    if (!shouldMatch(window)) return false;
    if (window.fullScreen) {
        window.fullScreen = false;
    }
    if (window.setMaximize) {
        window.setMaximize(false, false);
    }
    const nextRect = Object.assign({}, window.frameGeometry);
    nextRect.x = PLACE.geometry.x;
    nextRect.y = PLACE.geometry.y;
    nextRect.width = PLACE.geometry.width;
    nextRect.height = PLACE.geometry.height;
    window.frameGeometry = nextRect;
    finish(true, {
        matched_window: String(window.internalId),
        geometry: {
            x: nextRect.x,
            y: nextRect.y,
            width: nextRect.width,
            height: nextRect.height
        }
    });
    return true;
}

for (const window of workspace.stackingOrder) {
    if (apply(window)) {
        break;
    }
}

if (!finished) {
    workspace.windowAdded.connect(function(window) {
        apply(window);
    });
    singleShot(PLACE.timeout_ms, function() {
        finish(false, { error: "Timed out waiting for panel window" });
    });
}
)")
        .arg(jsJson(payload));
}
