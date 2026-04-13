#include "models.h"

#include <QJsonValue>

Rect Rect::fromJson(const QJsonValue &value, bool *ok)
{
    if (ok) {
        *ok = false;
    }
    if (!value.isObject()) {
        return {};
    }
    const QJsonObject object = value.toObject();
    Rect rect;
    rect.x = object.value(QStringLiteral("x")).toInt();
    rect.y = object.value(QStringLiteral("y")).toInt();
    rect.width = object.value(QStringLiteral("width")).toInt();
    rect.height = object.value(QStringLiteral("height")).toInt();
    if (ok) {
        *ok = true;
    }
    return rect;
}

QJsonObject Rect::toJson() const
{
    return {
        {QStringLiteral("x"), x},
        {QStringLiteral("y"), y},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
    };
}

QString WindowTemplate::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

WindowTemplate WindowTemplate::fromJson(const QJsonObject &object)
{
    WindowTemplate window;
    window.id = object.value(QStringLiteral("id")).toString();
    window.appName = object.value(QStringLiteral("app_name")).toString();
    window.desktopFileName = object.value(QStringLiteral("desktop_file_name")).toString();
    window.resourceClass = object.value(QStringLiteral("resource_class")).toString();
    window.resourceName = object.value(QStringLiteral("resource_name")).toString();
    window.windowRole = object.value(QStringLiteral("window_role")).toString();
    window.caption = object.value(QStringLiteral("caption")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("command")).toArray()) {
        window.command.append(value.toString());
    }
    window.commandSource = object.value(QStringLiteral("command_source")).toString();
    window.cwd = object.value(QStringLiteral("cwd")).toString();
    bool hasGeometry = false;
    window.geometry = Rect::fromJson(object.value(QStringLiteral("geometry")), &hasGeometry);
    window.hasGeometry = hasGeometry;
    window.desktop = object.value(QStringLiteral("desktop")).toInt();
    window.outputName = object.value(QStringLiteral("output_name")).toString();
    bool hasOutputGeometry = false;
    window.outputGeometry = Rect::fromJson(object.value(QStringLiteral("output_geometry")), &hasOutputGeometry);
    window.hasOutputGeometry = hasOutputGeometry;
    window.isFullScreen = object.value(QStringLiteral("is_full_screen")).toBool(false);
    window.maximizeMode = object.value(QStringLiteral("maximize_mode")).toInt(0);
    return window;
}

QJsonObject WindowTemplate::toJson() const
{
    QJsonArray commandArray;
    for (const QString &entry : command) {
        commandArray.append(entry);
    }
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("app_name"), appName},
        {QStringLiteral("desktop_file_name"), desktopFileName.isEmpty() ? QJsonValue() : QJsonValue(desktopFileName)},
        {QStringLiteral("resource_class"), resourceClass.isEmpty() ? QJsonValue() : QJsonValue(resourceClass)},
        {QStringLiteral("resource_name"), resourceName.isEmpty() ? QJsonValue() : QJsonValue(resourceName)},
        {QStringLiteral("window_role"), windowRole.isEmpty() ? QJsonValue() : QJsonValue(windowRole)},
        {QStringLiteral("caption"), caption.isEmpty() ? QJsonValue() : QJsonValue(caption)},
        {QStringLiteral("command"), commandArray},
        {QStringLiteral("command_source"), commandSource.isEmpty() ? QJsonValue() : QJsonValue(commandSource)},
        {QStringLiteral("cwd"), cwd.isEmpty() ? QJsonValue() : QJsonValue(cwd)},
        {QStringLiteral("geometry"), hasGeometry ? QJsonValue(geometry.toJson()) : QJsonValue()},
        {QStringLiteral("desktop"), desktop},
        {QStringLiteral("output_name"), outputName.isEmpty() ? QJsonValue() : QJsonValue(outputName)},
        {QStringLiteral("output_geometry"), hasOutputGeometry ? QJsonValue(outputGeometry.toJson()) : QJsonValue()},
        {QStringLiteral("is_full_screen"), isFullScreen},
        {QStringLiteral("maximize_mode"), maximizeMode},
    };
}

QStringList WindowTemplate::matchSignature() const
{
    return {desktopFileName.trimmed().toLower(), resourceClass.trimmed().toLower(), resourceName.trimmed().toLower()};
}

WorkspaceTemplate WorkspaceTemplate::create(const QString &name, const QVector<WindowTemplate> &items)
{
    WorkspaceTemplate workspace;
    workspace.name = name;
    workspace.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    workspace.version = 2;
    workspace.windows = items;
    return workspace;
}

WorkspaceTemplate WorkspaceTemplate::fromJson(const QJsonObject &object)
{
    WorkspaceTemplate workspace;
    workspace.name = object.value(QStringLiteral("name")).toString();
    workspace.createdAt = object.value(QStringLiteral("created_at")).toString();
    workspace.version = object.value(QStringLiteral("version")).toInt(1);
    for (const QJsonValue &value : object.value(QStringLiteral("windows")).toArray()) {
        workspace.windows.append(WindowTemplate::fromJson(value.toObject()));
    }
    return workspace;
}

QJsonObject WorkspaceTemplate::toJson() const
{
    QJsonArray windowsArray;
    for (const WindowTemplate &window : windows) {
        windowsArray.append(window.toJson());
    }
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("created_at"), createdAt},
        {QStringLiteral("version"), version},
        {QStringLiteral("windows"), windowsArray},
    };
}
