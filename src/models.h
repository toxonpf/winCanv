#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QUuid>

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    static Rect fromJson(const QJsonValue &value, bool *ok = nullptr);
    QJsonObject toJson() const;
};

struct WindowTemplate {
    QString id;
    QString appName;
    QString desktopFileName;
    QString resourceClass;
    QString resourceName;
    QString windowRole;
    QString caption;
    QStringList command;
    QString commandSource;
    QString cwd;
    bool hasGeometry = false;
    Rect geometry;
    int desktop = 0;
    QString outputName;
    bool hasOutputGeometry = false;
    Rect outputGeometry;
    bool isFullScreen = false;
    int maximizeMode = 0;

    static QString newId();
    static WindowTemplate fromJson(const QJsonObject &object);
    QJsonObject toJson() const;
    QStringList matchSignature() const;
};

struct WorkspaceTemplate {
    QString name;
    QString createdAt;
    int version = 2;
    QVector<WindowTemplate> windows;

    static WorkspaceTemplate create(const QString &name, const QVector<WindowTemplate> &windows);
    static WorkspaceTemplate fromJson(const QJsonObject &object);
    QJsonObject toJson() const;
};

struct LaunchResult {
    QStringList command;
    bool ok = false;
    QString message;
    qint64 pid = 0;
};

struct SaveResult {
    QString name;
    QString path;
    int windowCount = 0;
};

struct LoadResult {
    QString templateName;
    int requestedWindows = 0;
    QVector<LaunchResult> launched;
    QStringList unmatchedTargetIds;
    int appliedCount = 0;
    QJsonObject closeSummary;
};
