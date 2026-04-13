#include "desktop_entries.h"

#include "models.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {
const QRegularExpression kFieldCode(QStringLiteral("%[fFuUdDnNickvm]"));

QStringList normalizeExecCommand(QStringList command)
{
    if (command.size() >= 2 && command[0] == command[1]) {
        command.removeAt(1);
    }
    return command;
}

bool commandExists(const QStringList &command)
{
    if (command.isEmpty()) {
        return false;
    }
    const QString executable = command[0];
    if (executable.contains(QLatin1Char('/'))) {
        return QFileInfo::exists(executable);
    }
    return !QStandardPaths::findExecutable(executable).isEmpty();
}
}

QStringList readCmdline(qint64 pid)
{
    if (pid <= 0) {
        return {};
    }
    QFile file(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray raw = file.readAll();
    if (raw.endsWith('\0')) {
        raw.chop(1);
    }
    const QList<QByteArray> segments = raw.split('\0');
    QStringList result;
    for (const QByteArray &segment : segments) {
        if (!segment.isEmpty()) {
            result.append(QString::fromUtf8(segment));
        }
    }
    return result;
}

QString readCwd(qint64 pid)
{
    if (pid <= 0) {
        return {};
    }
    const QString path = QStringLiteral("/proc/%1/cwd").arg(pid);
    return QFileInfo(path).symLinkTarget();
}

QStringList desktopFileCandidates(const QString &desktopFileName)
{
    if (desktopFileName.trimmed().isEmpty()) {
        return {};
    }
    const QString name = desktopFileName.endsWith(QStringLiteral(".desktop"))
        ? desktopFileName
        : desktopFileName + QStringLiteral(".desktop");
    return {
        QDir::homePath() + QStringLiteral("/.local/share/applications/") + name,
        QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications/") + name,
        QStringLiteral("/var/lib/flatpak/exports/share/applications/") + name,
        QStringLiteral("/var/lib/snapd/desktop/applications/") + name,
        QStringLiteral("/usr/local/share/applications/") + name,
        QStringLiteral("/usr/share/applications/") + name,
    };
}

QStringList desktopEntryExec(const QString &desktopFileName)
{
    for (const QString &candidate : desktopFileCandidates(desktopFileName)) {
        if (!QFileInfo::exists(candidate)) {
            continue;
        }
        QSettings settings(candidate, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Desktop Entry"));
        const QString execLine = settings.value(QStringLiteral("Exec")).toString();
        settings.endGroup();
        const QString cleaned = execLine.trimmed().remove(kFieldCode).trimmed();
        if (cleaned.isEmpty()) {
            continue;
        }
        QStringList command = QProcess::splitCommand(cleaned);
        command = normalizeExecCommand(command);
        if (commandExists(command)) {
            return command;
        }
    }
    return {};
}

LaunchCommandInfo deriveLaunchCommand(qint64 pid, const QString &desktopFileName)
{
    const QStringList desktopExec = desktopEntryExec(desktopFileName);
    if (!desktopExec.isEmpty()) {
        return {desktopExec, QStringLiteral("desktop")};
    }
    const QStringList cmdline = readCmdline(pid);
    if (!cmdline.isEmpty()) {
        return {cmdline, QStringLiteral("procfs")};
    }
    return {};
}

LaunchCommandInfo resolveSavedLaunchCommand(const QStringList &savedCommand, const QString &desktopFileName)
{
    const QStringList desktopExec = desktopEntryExec(desktopFileName);
    if (!desktopExec.isEmpty()) {
        return {desktopExec, QStringLiteral("desktop")};
    }
    return {savedCommand, QStringLiteral("saved")};
}

LaunchResult launchCommand(const QStringList &command, const QString &cwd)
{
    LaunchResult result;
    result.command = command;
    if (command.isEmpty()) {
        result.message = QStringLiteral("No launch command available");
        return result;
    }
    auto *process = new QProcess(QCoreApplication::instance());
    process->setProgram(command[0]);
    process->setArguments(command.mid(1));
    if (!cwd.isEmpty()) {
        process->setWorkingDirectory(cwd);
    }
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    process->startDetached(&result.pid);
    delete process;
    result.ok = result.pid > 0;
    result.message = result.ok ? QStringLiteral("launched") : QStringLiteral("Failed to launch process");
    return result;
}
