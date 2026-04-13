#pragma once

#include "models.h"

#include <QString>
#include <QVector>

class TemplateStore {
public:
    explicit TemplateStore(QString templatesDir);

    QVector<WorkspaceTemplate> listTemplates() const;
    WorkspaceTemplate get(const QString &name) const;
    QString save(const WorkspaceTemplate &workspace) const;
    void remove(const QString &name) const;
    QString pathForName(const QString &name) const;

private:
    QString m_templatesDir;

    static QString slugify(const QString &value);
    WorkspaceTemplate readPath(const QString &path) const;
};
