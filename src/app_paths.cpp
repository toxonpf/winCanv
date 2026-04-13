#include "app_paths.h"

#include <QDir>

namespace {
QString xdgPath(const char *envName, const QString &fallback)
{
    const QByteArray raw = qgetenv(envName);
    if (!raw.isEmpty()) {
        return QDir::cleanPath(QString::fromUtf8(raw));
    }
    return QDir::cleanPath(fallback);
}
}

QString AppPaths::dataDir() const
{
    return baseDataDir + QStringLiteral("/workspace-templates");
}

QString AppPaths::stateDir() const
{
    return baseStateDir + QStringLiteral("/workspace-templates");
}

QString AppPaths::runtimeDir() const
{
    return baseRuntimeDir + QStringLiteral("/workspace-templates");
}

QString AppPaths::templatesDir() const
{
    return dataDir() + QStringLiteral("/templates");
}

QString AppPaths::logsDir() const
{
    return stateDir() + QStringLiteral("/logs");
}

void AppPaths::ensure() const
{
    QDir().mkpath(dataDir());
    QDir().mkpath(stateDir());
    QDir().mkpath(runtimeDir());
    QDir().mkpath(templatesDir());
    QDir().mkpath(logsDir());
}

AppPaths defaultPaths()
{
    AppPaths paths{
        xdgPath("XDG_DATA_HOME", QDir::homePath() + QStringLiteral("/.local/share")),
        xdgPath("XDG_STATE_HOME", QDir::homePath() + QStringLiteral("/.local/state")),
        qEnvironmentVariableIsSet("XDG_RUNTIME_DIR")
            ? QDir::cleanPath(QString::fromUtf8(qgetenv("XDG_RUNTIME_DIR")))
            : QStringLiteral("/tmp"),
    };
    paths.ensure();
    return paths;
}
