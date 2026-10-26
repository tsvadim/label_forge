// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/DataSource.h"
#include "core/LabelDocument.h"

#include <QHash>
#include <functional>
#include <QMainWindow>

class QAction;
class QActionGroup;
class QLabel;
class QListWidget;
class QMenu;
class QUndoStack;

namespace lf {

class LabelScene;
class LabelView;
class PropertyPanel;
class DataPanel;
class LabelObject;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool openFile(const QString& path);
    void newBlank();

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    // setup
    void createActions();
    void createMenus();
    void createToolbars();
    void createDocks();
    QAction* addAction(QMenu* m, const QString& text, const QString& icon, const QKeySequence& ks,
                       std::function<void()> fn);

    // file
    void newFromDialog();
    void open();
    bool save();
    bool saveAs();
    bool maybeSave();
    void saveAsTemplate();
    void manageTemplates();
    void manageSheets();
    void print();
    void quickPdf();
    void addRecent(const QString& path);
    void updateRecentMenu();
    void resetDocument();

    // data
    void editDataSource();
    void loadDataFromConfig(bool showErrors);
    void onInlineTableEdited(const DataTable& t);
    void updatePreviewContext();

    // editing
    void insertObject(int tool, const QPointF& pos);
    void insertImageFile(const QString& path, const QPointF& pos);
    void addObject(std::unique_ptr<LabelObject> obj, const QString& undoText);
    void deleteSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void duplicate();
    void selectAll();
    void align(int mode);
    void distribute(bool horizontal);
    void zOrder(int mode);
    void nudge(double dx, double dy);
    QList<LabelObject*> pasteObjects(const QByteArray& json, double offset);

    // sync
    void commit(const QString& text, const QString& mergeKey = QString());
    void restoreSnapshot(const QByteArray& bytes);
    void onSelectionChanged();
    void syncListSelection();
    void onObjectEdited(LabelObject* obj, const QString& key);
    void onLabelEdited(const QString& key);
    void updateObjectList();
    void updateTitle();
    void updateActions();
    QString baseDir() const;
    QList<int> selectedIndices() const;
    void selectIndices(const QList<int>& idx);
    void showVariablesHelp();
    void showAbout();

    LabelDocument m_doc;
    QString m_path;
    DataTable m_data;
    int m_currentRecord = 0;
    QByteArray m_snapshot;

    LabelScene* m_scene = nullptr;
    LabelView* m_view = nullptr;
    PropertyPanel* m_props = nullptr;
    DataPanel* m_dataPanel = nullptr;
    QListWidget* m_objects = nullptr;
    QUndoStack* m_undo = nullptr;
    QActionGroup* m_toolGroup = nullptr;
    QHash<int, QAction*> m_toolActions;
    QHash<QString, QAction*> m_act;
    QMenu* m_recentMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QLabel* m_statusPos = nullptr;
    QLabel* m_statusZoom = nullptr;
    QLabel* m_statusInfo = nullptr;
    bool m_syncingSelection = false;
    int m_pasteCount = 0;
};

} // namespace lf
