// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QListView;
class QTabBar;

namespace lf {

class ClipartModel;
class IconDownloadDialog;

// Browser for ready-made pictures: the clipart folder shipped next to the
// executable plus the user's own folder in the app-data directory. Each
// top-level folder is a tab; its subfolders (downloaded icon sets are split
// into them) are sections. Only file names are read up front: thumbnails
// are rendered in the background for the items that scroll into view.
// Items are dragged onto the label (as file URLs) or inserted with a double click.
class ClipartPanel : public QWidget {
    Q_OBJECT
public:
    explicit ClipartPanel(QWidget* parent = nullptr);
    ~ClipartPanel() override;

    static QString builtinDir();  // <exe dir>/clipart
    static QString userDir();     // <app data>/clipart, may not exist yet

    void rescan();
    int visibleCount() const;
    QStringList categories() const { return m_categories; }  // top-level folders, sorted
    QStringList sections() const;                            // sections of the current tab
    void setFilter(const QString& text);
    void setCategory(int index);  // 0 = all
    void setSection(int index);   // 0 = all sections
    // Opens the icon download window; query goes to its search tab.
    IconDownloadDialog* openDownloads(const QString& query = QString());

    // Removes a folder of the user's gallery: to the recycle bin, or for good
    // after a second question when there is none. confirm = ask first.
    static bool removeGalleryFolder(QWidget* parent, const QString& path, bool confirm);
    // The user's folder behind a tab; built-in tabs have none and cannot be removed.
    static bool canRemoveCategory(const QString& name);
    bool removeCategory(const QString& name, bool confirm = true);
    static int thumbnailsRendered();  // for tests: how many thumbnails were drawn so far

signals:
    void insertRequested(const QString& path);

private:
    struct Entry {
        QString path;
        QString name;      // file name without suffix, used for search
        QString category;  // top-level folder
        QString section;   // subfolder of the category, "" = its own files
        QString stamp;     // modification time + size, thumbnail cache key
    };
    void applyFilter();
    void updateSections();
    void openUserFolder();

    QList<Entry> m_entries;
    QStringList m_categories;
    QLineEdit* m_search;
    QTabBar* m_tabs;
    QComboBox* m_section;
    QListView* m_list;
    ClipartModel* m_model;
    QPointer<IconDownloadDialog> m_downloads;
};

} // namespace lf
