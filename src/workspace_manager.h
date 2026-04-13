#pragma once

#include "app_paths.h"
#include "kwin_controller.h"
#include "models.h"
#include "template_store.h"

#include <QJsonArray>
#include <QJsonObject>
#include <memory>

class WorkspaceManager {
public:
    WorkspaceManager();

    QVector<WorkspaceTemplate> listTemplates() const;
    void deleteTemplate(const QString &name) const;
    SaveResult saveTemplate(const QString &name) const;
    LoadResult loadTemplate(const QString &name, bool closeExisting) const;

    const AppPaths &paths() const;
    ResultBridge *bridge();

private:
    AppPaths m_paths;
    TemplateStore m_store;
    mutable std::unique_ptr<ResultBridge> m_bridge;
    mutable std::unique_ptr<KWinController> m_kwin;

    static QString appName(const QJsonObject &window);
    static QString desktopLookupName(const QStringList &values);
    static QJsonArray buildTargets(const QVector<WindowTemplate> &windows);
    static QHash<QString, QString> findReusableTargets(const QVector<WindowTemplate> &templateWindows, const QJsonArray &currentWindows);
    static int matchScore(const QJsonObject &current, const WindowTemplate &target);
    static int geometryDistance(const QJsonObject &current, const WindowTemplate &target);
    static bool sameValue(const QString &left, const QString &right);
    ResultBridge &bridgeInstance() const;
    KWinController &kwin() const;
};
