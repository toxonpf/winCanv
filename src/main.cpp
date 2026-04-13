#include "constants.h"
#include "workspace_manager.h"
#include "workspace_panel.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

namespace {
void configureApplicationMetadata()
{
    QCoreApplication::setApplicationName(QStringLiteral("workspace-templates"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(kAppOrganization);
    QCoreApplication::setOrganizationDomain(kAppDomain);
}

QString firstPositionalArgument(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        const QString value = QString::fromLocal8Bit(argv[i]);
        if (!value.startsWith(QLatin1Char('-'))) {
            return value;
        }
    }
    return {};
}

bool hasArgument(int argc, char **argv, const QString &expected)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == expected) {
            return true;
        }
    }
    return false;
}

int runCli(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    configureApplicationMetadata();

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("standalone"), QStringLiteral("Run as a normal window instead of a tray app")));
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("Command to run"));
    parser.addPositionalArgument(QStringLiteral("name"), QStringLiteral("Template name"), QStringLiteral("[name]"));
    parser.addOption(QCommandLineOption(QStringLiteral("name"), QStringLiteral("Template name for save"), QStringLiteral("template")));
    parser.addOption(QCommandLineOption(QStringLiteral("close-existing"), QStringLiteral("Close existing windows first")));
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    const QString command = positional.isEmpty() ? QString() : positional[0];
    QTextStream out(stdout);
    QTextStream err(stderr);

    WorkspaceManager manager;
    try {
        if (command == QStringLiteral("save")) {
            const QString name = parser.value(QStringLiteral("name"));
            if (name.isEmpty()) {
                err << "--name is required for save\n";
                return 2;
            }
            const SaveResult result = manager.saveTemplate(name);
            out << "saved " << result.name << " (" << result.windowCount << " windows) -> " << result.path << "\n";
            return 0;
        }
        if (command == QStringLiteral("load")) {
            if (positional.size() < 2) {
                err << "template name is required for load\n";
                return 2;
            }
            const LoadResult result = manager.loadTemplate(positional[1], parser.isSet(QStringLiteral("close-existing")));
            int launchedOk = 0;
            for (const LaunchResult &item : result.launched) {
                if (item.ok) {
                    ++launchedOk;
                }
            }
            out << "loaded " << result.templateName << ": applied=" << result.appliedCount << "/" << result.requestedWindows
                << " unmatched=" << result.unmatchedTargetIds.size() << " launched=" << launchedOk << "\n";
            return result.unmatchedTargetIds.isEmpty() ? 0 : 1;
        }
        if (command == QStringLiteral("delete")) {
            if (positional.size() < 2) {
                err << "template name is required for delete\n";
                return 2;
            }
            manager.deleteTemplate(positional[1]);
            out << "deleted " << positional[1] << "\n";
            return 0;
        }
        if (command == QStringLiteral("list")) {
            const QVector<WorkspaceTemplate> templates = manager.listTemplates();
            for (const WorkspaceTemplate &workspace : templates) {
                out << workspace.name << "\t" << workspace.windows.size() << "\t" << workspace.createdAt << "\n";
            }
            return 0;
        }
        err << "unknown command: " << command << "\n";
        return 2;
    } catch (const std::exception &exc) {
        err << exc.what() << "\n";
        return 1;
    }
}
}

int main(int argc, char **argv)
{
    const QString commandHint = firstPositionalArgument(argc, argv);
    const bool standalone = hasArgument(argc, argv, QStringLiteral("--standalone"));
    const bool helpRequested = hasArgument(argc, argv, QStringLiteral("--help")) || hasArgument(argc, argv, QStringLiteral("-h"));
    const bool versionRequested = hasArgument(argc, argv, QStringLiteral("--version")) || hasArgument(argc, argv, QStringLiteral("-v"));

    if (helpRequested || versionRequested || (!standalone && !commandHint.isEmpty() && commandHint != QStringLiteral("toggle-panel"))) {
        return runCli(argc, argv);
    }

    QApplication app(argc, argv);
    configureApplicationMetadata();

    if (standalone) {
        return runStandalone(app);
    }
    if (commandHint == QStringLiteral("toggle-panel")) {
        if (sendPanelCommand(QStringLiteral("toggle"))) {
            return 0;
        }
        return runPanelHost(app, true);
    }
    if (sendPanelCommand(QStringLiteral("show"))) {
        return 0;
    }
    return runPanelHost(app, false);
}
