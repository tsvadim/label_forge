// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Iconify.h"

#include <QDialog>
#include <QPointer>
#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace lf {

// Downloads icons from the free sets of the Iconify catalogue into the
// user's clipart folder: single icons found by a search, or whole sets.
// Only sets whose license allows commercial printing without credits are
// offered. Each set gets its own folder (split into sections) with a
// LICENSE.txt. The server is https://api.iconify.design unless the
// LABELFORGE_ICONIFY_URL environment variable points elsewhere (tests).
class IconDownloadDialog : public QDialog {
    Q_OBJECT
public:
    explicit IconDownloadDialog(const QString& galleryDir, QWidget* parent = nullptr);

    static QString apiBase();
    // Folder of a set inside the gallery.
    static QString setFolder(const QString& galleryDir, const iconify::Collection& c);

    void search(const QString& query);  // fills the field, shows the tab, searches
    void showSets();                    // shows the sets tab, loads the list once

    // for tests
    bool busy() const { return !m_reply.isNull() || !m_listReply.isNull() || m_jobActive; }
    int resultCount() const;
    void selectAllResults();
    void addSelected();
    bool downloadSet(const QString& prefix);  // false if the set is not in the list
    int setCount() const;
    QString status() const;

signals:
    void galleryChanged();  // files were added to the gallery

private:
    using Handler = std::function<void(const QByteArray& body, const QString& error)>;
    QNetworkReply* get(const QString& pathAndQuery, Handler done);
    void runSearch();
    void loadNextPreview();  // one request per set of the search results
    void loadSets();
    void fillSets();
    void startSetDownload(const iconify::Collection& c);
    void nextBatch();
    void finishSet(bool cancelled);
    void updateAddButton();
    bool isSetComplete(const iconify::Collection& c) const;

    QString m_dir;
    QNetworkAccessManager* m_net;
    QPointer<QNetworkReply> m_reply;     // search and its previews
    QPointer<QNetworkReply> m_listReply;  // list of sets
    QPointer<QNetworkReply> m_jobReply;  // whole-set download
    QTabWidget* m_tabs;

    // search tab
    QLineEdit* m_query;
    QLabel* m_searchStatus;
    QListWidget* m_results;
    QPushButton* m_add;
    QHash<QString, iconify::Collection> m_searchCollections;
    QStringList m_previewQueue;  // prefixes still to fetch for the current search
    QHash<QString, QStringList> m_previewNames;

    // sets tab
    QLineEdit* m_setFilter;
    QTableWidget* m_sets;
    QPushButton* m_download;
    QPushButton* m_cancel;
    QProgressBar* m_progress;
    QLabel* m_setStatus;
    QList<iconify::Collection> m_collections;
    bool m_setsLoaded = false;

    // whole-set download in progress
    bool m_jobActive = false;
    bool m_cancelRequested = false;
    iconify::Collection m_job;
    QList<QPair<QString, QStringList>> m_batches;  // folder, names
    int m_jobTotal = 0, m_jobDone = 0, m_jobAdded = 0;
};

} // namespace lf
