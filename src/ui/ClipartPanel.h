// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTabBar;

namespace lf {

// Browser for ready-made pictures: the clipart folder shipped next to the
// executable plus the user's own folder in the app-data directory. Each
// subfolder is a category. Items are dragged onto the label (as file URLs)
// or inserted with a double click.
class ClipartPanel : public QWidget {
    Q_OBJECT
public:
    explicit ClipartPanel(QWidget* parent = nullptr);

    static QString builtinDir();  // <exe dir>/clipart
    static QString userDir();     // <app data>/clipart, may not exist yet

    void rescan();
    int visibleCount() const;
    QStringList categories() const { return m_categories; }
    void setFilter(const QString& text);
    void setCategory(int index);  // 0 = all

signals:
    void insertRequested(const QString& path);

private:
    struct Entry {
        QString path;
        QString name;      // file name without suffix, used for search
        QString category;  // empty = the "all" tab only
    };
    void applyFilter();
    void showSources();
    void openUserFolder();

    QList<Entry> m_entries;
    QStringList m_categories;  // sorted, without "all"
    QLineEdit* m_search;
    QTabBar* m_tabs;
    QListWidget* m_list;
};

} // namespace lf
