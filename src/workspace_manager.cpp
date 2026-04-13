#include "workspace_manager.h"

#include "desktop_entries.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

WorkspaceManager::WorkspaceManager()
    : m_paths(defaultPaths())
    , m_store(m_paths.templatesDir())
{
}

QVector<WorkspaceTemplate> WorkspaceManager::listTemplates() const
{
    return m_store.listTemplates();
}

void WorkspaceManager::deleteTemplate(const QString &name) const
{
    m_store.remove(name);
}

QString WorkspaceManager::appName(const QJsonObject &window)
{
    for (const QString &key : {QStringLiteral("desktop_file_name"), QStringLiteral("resource_class"), QStringLiteral("resource_name"), QStringLiteral("caption")}) {
        const QString value = window.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return QStringLiteral("unknown-app");
}

QString WorkspaceManager::desktopLookupName(const QStringList &values)
{
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return {};
}

SaveResult WorkspaceManager::saveTemplate(const QString &name) const
{
    const QJsonObject snapshot = kwin().captureWorkspace();
    QVector<WindowTemplate> windows;
    for (const QJsonValue &value : snapshot.value(QStringLiteral("windows")).toArray()) {
        const QJsonObject item = value.toObject();
        const qint64 pid = item.value(QStringLiteral("pid")).toInteger();
        const QString desktopName = desktopLookupName({
            item.value(QStringLiteral("desktop_file_name")).toString(),
            item.value(QStringLiteral("resource_class")).toString(),
            item.value(QStringLiteral("app_name")).toString(),
            item.value(QStringLiteral("caption")).toString(),
        });
        const LaunchCommandInfo commandInfo = deriveLaunchCommand(pid, desktopName);
        WindowTemplate window;
        window.id = WindowTemplate::newId();
        window.appName = appName(item);
        window.desktopFileName = (commandInfo.source == QStringLiteral("desktop") && item.value(QStringLiteral("desktop_file_name")).toString().isEmpty())
            ? desktopName
            : item.value(QStringLiteral("desktop_file_name")).toString();
        window.resourceClass = item.value(QStringLiteral("resource_class")).toString();
        window.resourceName = item.value(QStringLiteral("resource_name")).toString();
        window.windowRole = item.value(QStringLiteral("window_role")).toString();
        window.caption = item.value(QStringLiteral("caption")).toString();
        window.command = commandInfo.command;
        window.commandSource = commandInfo.source;
        window.cwd = readCwd(pid);
        bool hasGeometry = false;
        window.geometry = Rect::fromJson(item.value(QStringLiteral("geometry")), &hasGeometry);
        window.hasGeometry = hasGeometry;
        window.desktop = item.value(QStringLiteral("desktop")).toInt();
        window.outputName = item.value(QStringLiteral("output_name")).toString();
        bool hasOutputGeometry = false;
        window.outputGeometry = Rect::fromJson(item.value(QStringLiteral("output_geometry")), &hasOutputGeometry);
        window.hasOutputGeometry = hasOutputGeometry;
        window.isFullScreen = item.value(QStringLiteral("full_screen")).toBool(false);
        window.maximizeMode = item.value(QStringLiteral("maximize_mode")).toInt(0);
        windows.append(window);
    }
    const WorkspaceTemplate workspace = WorkspaceTemplate::create(name, windows);
    const QString path = m_store.save(workspace);
    return {workspace.name, path, static_cast<int>(workspace.windows.size())};
}

QJsonArray WorkspaceManager::buildTargets(const QVector<WindowTemplate> &windows)
{
    QJsonArray targets;
    for (const WindowTemplate &window : windows) {
        targets.append(QJsonObject{
            {QStringLiteral("id"), window.id},
            {QStringLiteral("app_name"), window.appName},
            {QStringLiteral("desktop_file_name"), window.desktopFileName},
            {QStringLiteral("resource_class"), window.resourceClass},
            {QStringLiteral("resource_name"), window.resourceName},
            {QStringLiteral("window_role"), window.windowRole},
            {QStringLiteral("caption"), window.caption},
            {QStringLiteral("geometry"), window.hasGeometry ? QJsonValue(window.geometry.toJson()) : QJsonValue()},
            {QStringLiteral("desktop"), window.desktop},
            {QStringLiteral("output_name"), window.outputName},
            {QStringLiteral("output_geometry"), window.hasOutputGeometry ? QJsonValue(window.outputGeometry.toJson()) : QJsonValue()},
            {QStringLiteral("full_screen"), window.isFullScreen},
            {QStringLiteral("maximize_mode"), window.maximizeMode},
        });
    }
    return targets;
}

bool WorkspaceManager::sameValue(const QString &left, const QString &right)
{
    return !left.trimmed().isEmpty() && !right.trimmed().isEmpty() && left.trimmed().compare(right.trimmed(), Qt::CaseInsensitive) == 0;
}

int WorkspaceManager::matchScore(const QJsonObject &current, const WindowTemplate &target)
{
    int score = 0;
    if (sameValue(current.value(QStringLiteral("desktop_file_name")).toString(), target.desktopFileName)) score += 100;
    if (sameValue(current.value(QStringLiteral("resource_class")).toString(), target.resourceClass)) score += 35;
    if (sameValue(current.value(QStringLiteral("resource_name")).toString(), target.resourceName)) score += 20;
    if (sameValue(current.value(QStringLiteral("window_role")).toString(), target.windowRole)) score += 10;
    if (sameValue(current.value(QStringLiteral("caption")).toString(), target.caption)) score += 3;
    return score;
}

int WorkspaceManager::geometryDistance(const QJsonObject &current, const WindowTemplate &target)
{
    bool currentOk = false;
    const Rect currentRect = Rect::fromJson(current.value(QStringLiteral("geometry")), &currentOk);
    if (!currentOk || !target.hasGeometry) {
        return 1000000;
    }
    return std::abs(currentRect.x - target.geometry.x)
        + std::abs(currentRect.y - target.geometry.y)
        + std::abs(currentRect.width - target.geometry.width)
        + std::abs(currentRect.height - target.geometry.height);
}

QHash<QString, QString> WorkspaceManager::findReusableTargets(const QVector<WindowTemplate> &templateWindows, const QJsonArray &currentWindows)
{
    QHash<QString, QString> reused;
    QSet<int> usedIndexes;
    for (const WindowTemplate &target : templateWindows) {
        int bestIndex = -1;
        int bestScore = 0;
        int bestDistance = 1000000;
        for (int i = 0; i < currentWindows.size(); ++i) {
            if (usedIndexes.contains(i)) {
                continue;
            }
            const QJsonObject current = currentWindows[i].toObject();
            const int currentScore = matchScore(current, target);
            if (currentScore <= 0) {
                continue;
            }
            const int distance = geometryDistance(current, target);
            if (currentScore > bestScore || (currentScore == bestScore && distance < bestDistance)) {
                bestIndex = i;
                bestScore = currentScore;
                bestDistance = distance;
            }
        }
        if (bestIndex >= 0) {
            usedIndexes.insert(bestIndex);
            reused.insert(target.id, currentWindows[bestIndex].toObject().value(QStringLiteral("internal_id")).toString());
        }
    }
    return reused;
}

LoadResult WorkspaceManager::loadTemplate(const QString &name, bool closeExisting) const
{
    const WorkspaceTemplate workspace = m_store.get(name);
    const QJsonObject current = kwin().captureWorkspace();
    const QJsonArray currentWindows = current.value(QStringLiteral("windows")).toArray();
    const QHash<QString, QString> reusableMatches = findReusableTargets(workspace.windows, currentWindows);
    QJsonObject closeSummary;
    if (closeExisting) {
        QStringList keepIds = reusableMatches.values();
        std::sort(keepIds.begin(), keepIds.end());
        closeSummary = kwin().closeWorkspaceWindows(keepIds);
        QThread::msleep(1000);
    }

    const QJsonArray targets = buildTargets(workspace.windows);
    QVector<WindowTemplate> launches;
    for (const WindowTemplate &window : workspace.windows) {
        if (!reusableMatches.contains(window.id)) {
            launches.append(window);
        }
    }

    RunningScript session = kwin().startRestore(targets, 20000, 900);
    QVector<LaunchResult> launched;
    try {
        for (const WindowTemplate &window : launches) {
            const QString desktopName = desktopLookupName({window.desktopFileName, window.resourceClass, window.appName, window.caption});
            const LaunchCommandInfo info = resolveSavedLaunchCommand(window.command, desktopName);
            launched.append(launchCommand(info.command, window.cwd));
            QThread::msleep(180);
        }
        const QJsonObject restore = session.wait(22000);
        session.stop();
        LoadResult result;
        result.templateName = workspace.name;
        result.requestedWindows = workspace.windows.size();
        result.launched = launched;
        for (const QJsonValue &value : restore.value(QStringLiteral("unmatched")).toArray()) {
            result.unmatchedTargetIds.append(value.toString());
        }
        result.appliedCount = restore.value(QStringLiteral("applied")).toArray().size();
        result.closeSummary = closeSummary;
        return result;
    } catch (...) {
        session.stop();
        throw;
    }
}

const AppPaths &WorkspaceManager::paths() const
{
    return m_paths;
}

ResultBridge *WorkspaceManager::bridge()
{
    return &bridgeInstance();
}

ResultBridge &WorkspaceManager::bridgeInstance() const
{
    if (!m_bridge) {
        m_bridge = std::make_unique<ResultBridge>();
    }
    return *m_bridge;
}

KWinController &WorkspaceManager::kwin() const
{
    if (!m_kwin) {
        m_kwin = std::make_unique<KWinController>(m_paths, &bridgeInstance(), QList<qint64>{QCoreApplication::applicationPid()});
    }
    return *m_kwin;
}
