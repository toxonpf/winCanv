#pragma once

#include "app_paths.h"

#include <QDBusConnection>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QWaitCondition>

class ResultBridge : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.toxonpf.WorkspaceTemplates")

public:
    explicit ResultBridge(QObject *parent = nullptr);
    ~ResultBridge() override;

    QDBusConnection connection() const;
    QString serviceName() const;
    QJsonObject waitFor(const QString &requestId, int timeoutMs);

public slots:
    bool ReportResult(const QString &requestId, const QString &payloadJson);

private:
    QDBusConnection m_connection;
    QString m_serviceName;
    mutable QMutex m_mutex;
    QWaitCondition m_condition;
    QHash<QString, QJsonObject> m_results;
};

class RunningScript {
public:
    QString requestId;
    int scriptId = 0;
    QString sourcePath;
    ResultBridge *bridge = nullptr;
    QDBusConnection connection;

    QJsonObject wait(int timeoutMs) const;
    void stop() const;
};

class KWinController {
public:
    KWinController(const AppPaths &paths, ResultBridge *bridge, QList<qint64> ignoredPids = {});

    QJsonObject captureWorkspace() const;
    QJsonObject closeWorkspaceWindows(const QStringList &exceptIds = {}) const;
    RunningScript startRestore(const QJsonArray &targets, int timeoutMs = 20000, int settleMs = 900) const;
    QJsonObject placeWindow(qint64 pid, const QJsonObject &geometry, const QString &caption = {}, int timeoutMs = 1800) const;
    static QString scriptObjectPath(int scriptId);

private:
    AppPaths m_paths;
    ResultBridge *m_bridge = nullptr;
    QDBusConnection m_connection;
    QList<qint64> m_ignoredPids;

    QJsonObject runRequest(const QString &scriptSource, int timeoutMs) const;
    RunningScript startRequest(const QString &scriptSource) const;
    QString wrapScript(const QString &requestId, const QString &scriptBody) const;
    QString captureScript() const;
    QString closeScript(const QStringList &exceptIds) const;
    QString restoreScript(const QJsonArray &targets, int timeoutMs, int settleMs) const;
    QString placeWindowScript(qint64 pid, const QJsonObject &geometry, const QString &caption, int timeoutMs) const;

};
