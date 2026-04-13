#pragma once

#include <QString>

struct AppPaths {
    QString baseDataDir;
    QString baseStateDir;
    QString baseRuntimeDir;

    QString dataDir() const;
    QString stateDir() const;
    QString runtimeDir() const;
    QString templatesDir() const;
    QString logsDir() const;
    void ensure() const;
};

AppPaths defaultPaths();
