// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

namespace lf {

class LabelDocument;

struct TemplateInfo {
    QString id;        // builtin id or file path
    QString path;      // empty for built-in templates
    QString title;
    QString category;
    QString description;
    bool builtin = false;
};

// Label templates: built-in examples plus the user's own templates,
// stored as regular .lfl files in the application data folder.
class TemplateLibrary {
public:
    static QString userDir();
    static QVector<TemplateInfo> builtins();
    static QVector<TemplateInfo> userTemplates();
    static QVector<TemplateInfo> all();
    static QStringList categories();

    static bool load(const TemplateInfo& info, LabelDocument& doc, QString* err);
    static bool saveUser(const LabelDocument& doc, const QString& title, const QString& category,
                         const QString& description, bool includeData, QString* outPath, QString* err);
    static bool remove(const TemplateInfo& info);
    static bool importFile(const QString& path, QString* err);

private:
    static void buildBuiltin(const QString& id, LabelDocument& doc);
};

} // namespace lf
