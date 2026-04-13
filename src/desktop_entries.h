#pragma once

#include "models.h"

#include <QString>
#include <QStringList>

struct LaunchCommandInfo {
    QStringList command;
    QString source;
};

QStringList readCmdline(qint64 pid);
QString readCwd(qint64 pid);
QStringList desktopFileCandidates(const QString &desktopFileName);
QStringList desktopEntryExec(const QString &desktopFileName);
LaunchCommandInfo deriveLaunchCommand(qint64 pid, const QString &desktopFileName);
LaunchCommandInfo resolveSavedLaunchCommand(const QStringList &savedCommand, const QString &desktopFileName);
LaunchResult launchCommand(const QStringList &command, const QString &cwd);
