#include "template_store.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>

TemplateStore::TemplateStore(QString templatesDir)
    : m_templatesDir(std::move(templatesDir))
{
    QDir().mkpath(m_templatesDir);
}

QString TemplateStore::slugify(const QString &value)
{
    QString slug = value.trimmed().toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
    slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return slug.isEmpty() ? QStringLiteral("template") : slug;
}

QString TemplateStore::pathForName(const QString &name) const
{
    return m_templatesDir + QStringLiteral("/") + slugify(name) + QStringLiteral(".json");
}

WorkspaceTemplate TemplateStore::readPath(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QStringLiteral("Unable to open template: %1").arg(path).toStdString());
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return WorkspaceTemplate::fromJson(document.object());
}

QVector<WorkspaceTemplate> TemplateStore::listTemplates() const
{
    QVector<WorkspaceTemplate> items;
    const QDir dir(m_templatesDir);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &info : files) {
        items.append(readPath(info.absoluteFilePath()));
    }
    std::sort(items.begin(), items.end(), [](const WorkspaceTemplate &left, const WorkspaceTemplate &right) {
        return left.createdAt > right.createdAt;
    });
    return items;
}

WorkspaceTemplate TemplateStore::get(const QString &name) const
{
    const QString path = pathForName(name);
    if (!QFileInfo::exists(path)) {
        throw std::runtime_error(QStringLiteral("Template '%1' does not exist").arg(name).toStdString());
    }
    return readPath(path);
}

QString TemplateStore::save(const WorkspaceTemplate &workspace) const
{
    const QString path = pathForName(workspace.name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw std::runtime_error(QStringLiteral("Unable to save template: %1").arg(path).toStdString());
    }
    file.write(QJsonDocument(workspace.toJson()).toJson(QJsonDocument::Indented));
    file.write("\n");
    return path;
}

void TemplateStore::remove(const QString &name) const
{
    QFile::remove(pathForName(name));
}
