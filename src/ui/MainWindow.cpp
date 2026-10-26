// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "Commands.h"
#include "DataPanel.h"
#include "DataSourceDialog.h"
#include "Icons.h"
#include "LabelScene.h"
#include "LabelView.h"
#include "NewLabelDialog.h"
#include "ObjectItem.h"
#include "PrintDialog.h"
#include "PropertyPanel.h"
#include "SaveTemplateDialog.h"
#include "SheetLayoutDialog.h"
#include "barcode/Barcode.h"
#include "data/DataLoader.h"
#include "print/PrintJob.h"
#include "templates/TemplateLibrary.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUndoStack>
#include <QVBoxLayout>

namespace lf {

namespace {
const char* const MIME_OBJECTS = "application/x-labelforge-objects";
const int MAX_RECENT = 10;
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowIcon(icons::appIcon());
    setAcceptDrops(true);
    m_undo = new QUndoStack(this);
    connect(m_undo, &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);

    m_scene = new LabelScene(this);
    m_view = new LabelView;
    m_view->setScene(m_scene);
    setCentralWidget(m_view);

    createDocks();
    createActions();
    createMenus();
    createToolbars();

    m_statusInfo = new QLabel;
    m_statusPos = new QLabel;
    m_statusZoom = new QLabel;
    statusBar()->addWidget(m_statusInfo, 1);
    statusBar()->addPermanentWidget(m_statusPos);
    statusBar()->addPermanentWidget(m_statusZoom);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_scene, &LabelScene::geometryEdited, this, [this] {
        commit(tr("Перемещение"));
        m_props->refreshGeometry();
    });
    connect(m_scene, &LabelScene::insertRequested, this, &MainWindow::insertObject);
    connect(m_scene, &LabelScene::cursorMoved, this, [this](const QPointF& p) {
        m_statusPos->setText(tr("X: %1  Y: %2 мм").arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1));
        m_view->setCursorPos(p);
    });
    connect(m_view, &LabelView::zoomChanged, this,
            [this](double z) { m_statusZoom->setText(QStringLiteral("%1%").arg(std::lround(z * 100))); });
    connect(m_view, &LabelView::nudge, this, &MainWindow::nudge);
    connect(m_props, &PropertyPanel::objectEdited, this, &MainWindow::onObjectEdited);
    connect(m_props, &PropertyPanel::labelEdited, this, &MainWindow::onLabelEdited);
    connect(m_props, &PropertyPanel::layoutApplied, this,
            [this] { QTimer::singleShot(0, this, [this] { m_view->fitToLabel(QRectF(0, 0, m_doc.label.width, m_doc.label.height)); }); });

    QSettings st;
    m_scene->setGridStep(st.value(QStringLiteral("view/grid"), 1.0).toDouble());
    m_scene->setGridVisible(st.value(QStringLiteral("view/showGrid"), true).toBool());
    m_scene->setSnapEnabled(st.value(QStringLiteral("view/snap"), true).toBool());
    m_act.value(QStringLiteral("grid"))->setChecked(m_scene->gridVisible());
    m_act.value(QStringLiteral("snap"))->setChecked(m_scene->snapEnabled());
    resize(1280, 820);
    restoreGeometry(st.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(st.value(QStringLiteral("window/state")).toByteArray());

    newBlank();
}

// ======================================================================= setup

QAction* MainWindow::addAction(QMenu* m, const QString& text, const QString& icon, const QKeySequence& ks,
                               std::function<void()> fn)
{
    auto* a = new QAction(text, this);
    if (!icon.isEmpty())
        a->setIcon(icons::get(icon));
    if (!ks.isEmpty())
        a->setShortcut(ks);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    if (m)
        m->addAction(a);
    return a;
}

void MainWindow::createActions()
{
    auto mk = [this](const QString& id, const QString& text, const QString& icon, const QKeySequence& ks,
                     std::function<void()> fn) {
        QAction* a = addAction(nullptr, text, icon, ks, std::move(fn));
        m_act.insert(id, a);
        return a;
    };
    mk(QStringLiteral("new"), tr("&Новая этикетка…"), QStringLiteral("new"), QKeySequence::New, [this] { newFromDialog(); });
    mk(QStringLiteral("open"), tr("&Открыть…"), QStringLiteral("open"), QKeySequence::Open, [this] { open(); });
    mk(QStringLiteral("save"), tr("&Сохранить"), QStringLiteral("save"), QKeySequence::Save, [this] { save(); });
    mk(QStringLiteral("saveAs"), tr("Сохранить &как…"), QString(), QKeySequence::SaveAs, [this] { saveAs(); });
    mk(QStringLiteral("saveTemplate"), tr("Сохранить как &шаблон…"), QStringLiteral("template"), QKeySequence(),
       [this] { saveAsTemplate(); });
    mk(QStringLiteral("templates"), tr("Шаблоны &этикеток…"), QString(), QKeySequence(), [this] { manageTemplates(); });
    mk(QStringLiteral("sheets"), tr("Шаблоны &листов и рулонов…"), QString(), QKeySequence(), [this] { manageSheets(); });
    mk(QStringLiteral("print"), tr("&Печать…"), QStringLiteral("print"), QKeySequence::Print, [this] { print(); });
    mk(QStringLiteral("pdf"), tr("Экспорт в &PDF…"), QStringLiteral("pdf"), QKeySequence(tr("Ctrl+E")), [this] { quickPdf(); });
    mk(QStringLiteral("quit"), tr("В&ыход"), QString(), QKeySequence::Quit, [this] { close(); });

    QAction* undo = m_undo->createUndoAction(this, tr("&Отменить"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setIcon(icons::get(QStringLiteral("undo")));
    m_act.insert(QStringLiteral("undo"), undo);
    QAction* redo = m_undo->createRedoAction(this, tr("&Повторить"));
    redo->setShortcuts({QKeySequence::Redo, QKeySequence(tr("Ctrl+Y"))});
    redo->setIcon(icons::get(QStringLiteral("redo")));
    m_act.insert(QStringLiteral("redo"), redo);
    mk(QStringLiteral("cut"), tr("Вы&резать"), QString(), QKeySequence::Cut, [this] { cutSelection(); });
    mk(QStringLiteral("copy"), tr("&Копировать"), QString(), QKeySequence::Copy, [this] { copySelection(); });
    mk(QStringLiteral("paste"), tr("Вст&авить"), QString(), QKeySequence::Paste, [this] { paste(); });
    mk(QStringLiteral("duplicate"), tr("&Дублировать"), QString(), QKeySequence(tr("Ctrl+D")), [this] { duplicate(); });
    mk(QStringLiteral("delete"), tr("&Удалить"), QStringLiteral("delete"), QKeySequence::Delete, [this] { deleteSelection(); });
    mk(QStringLiteral("selectAll"), tr("Выделить &всё"), QString(), QKeySequence::SelectAll, [this] { selectAll(); });

    m_toolGroup = new QActionGroup(this);
    struct ToolDef {
        int tool;
        QString text, icon, key;
    };
    const QVector<ToolDef> tools = {
        {LabelScene::ToolSelect, tr("Выбор"), QStringLiteral("select"), QStringLiteral("V")},
        {LabelScene::ToolText, tr("Текст"), QStringLiteral("text"), QStringLiteral("T")},
        {LabelScene::ToolBarcode, tr("Штрихкод"), QStringLiteral("barcode"), QStringLiteral("B")},
        {LabelScene::ToolQr, tr("QR / Data Matrix"), QStringLiteral("qr"), QStringLiteral("Q")},
        {LabelScene::ToolImage, tr("Изображение"), QStringLiteral("image"), QStringLiteral("I")},
        {LabelScene::ToolRect, tr("Прямоугольник"), QStringLiteral("rect"), QStringLiteral("R")},
        {LabelScene::ToolEllipse, tr("Эллипс"), QStringLiteral("ellipse"), QStringLiteral("E")},
        {LabelScene::ToolLine, tr("Линия"), QStringLiteral("line"), QStringLiteral("L")},
    };
    for (const ToolDef& t : tools) {
        auto* a = new QAction(icons::get(t.icon), t.text, this);
        a->setCheckable(true);
        a->setShortcut(QKeySequence(t.key));
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        a->setToolTip(QStringLiteral("%1 (%2)").arg(t.text, t.key));
        a->setData(t.tool);
        m_toolGroup->addAction(a);
        m_toolActions.insert(t.tool, a);
        m_view->addAction(a);  // single-letter shortcuts only while the canvas has focus
        connect(a, &QAction::triggered, this, [this, t] { m_scene->setTool(LabelScene::Tool(t.tool)); });
    }
    m_toolActions.value(LabelScene::ToolSelect)->setChecked(true);

    auto alignAct = [&](const QString& id, const QString& text, int mode) {
        mk(QStringLiteral("align-") + id, text, QStringLiteral("align-") + id, QKeySequence(), [this, mode] { align(mode); });
    };
    alignAct(QStringLiteral("left"), tr("По левому краю"), 0);
    alignAct(QStringLiteral("hcenter"), tr("По центру (горизонтально)"), 1);
    alignAct(QStringLiteral("right"), tr("По правому краю"), 2);
    alignAct(QStringLiteral("top"), tr("По верхнему краю"), 3);
    alignAct(QStringLiteral("vcenter"), tr("По середине (вертикально)"), 4);
    alignAct(QStringLiteral("bottom"), tr("По нижнему краю"), 5);
    mk(QStringLiteral("distH"), tr("Распределить по горизонтали"), QString(), QKeySequence(), [this] { distribute(true); });
    mk(QStringLiteral("distV"), tr("Распределить по вертикали"), QString(), QKeySequence(), [this] { distribute(false); });
    mk(QStringLiteral("front"), tr("На передний план"), QStringLiteral("front"), QKeySequence(tr("Ctrl+Shift+]")),
       [this] { zOrder(0); });
    mk(QStringLiteral("forward"), tr("Переместить вперёд"), QString(), QKeySequence(tr("Ctrl+]")), [this] { zOrder(1); });
    mk(QStringLiteral("backward"), tr("Переместить назад"), QString(), QKeySequence(tr("Ctrl+[")), [this] { zOrder(2); });
    mk(QStringLiteral("back"), tr("На задний план"), QStringLiteral("back"), QKeySequence(tr("Ctrl+Shift+[")),
       [this] { zOrder(3); });

    mk(QStringLiteral("zoomIn"), tr("Увеличить"), QStringLiteral("zoom-in"), QKeySequence::ZoomIn, [this] { m_view->zoomIn(); });
    mk(QStringLiteral("zoomOut"), tr("Уменьшить"), QStringLiteral("zoom-out"), QKeySequence::ZoomOut, [this] { m_view->zoomOut(); });
    mk(QStringLiteral("zoomFit"), tr("По размеру окна"), QStringLiteral("zoom-fit"), QKeySequence(tr("Ctrl+0")),
       [this] { m_view->fitToLabel(QRectF(0, 0, m_doc.label.width, m_doc.label.height)); });
    mk(QStringLiteral("zoom100"), tr("Реальный размер (100%)"), QString(), QKeySequence(tr("Ctrl+1")), [this] { m_view->setZoom(1.0); });
    QAction* grid = mk(QStringLiteral("grid"), tr("Показывать сетку"), QStringLiteral("grid"), QKeySequence(tr("Ctrl+'")), [] {});
    grid->setCheckable(true);
    connect(grid, &QAction::toggled, this, [this](bool on) {
        m_scene->setGridVisible(on);
        QSettings().setValue(QStringLiteral("view/showGrid"), on);
    });
    QAction* snap = mk(QStringLiteral("snap"), tr("Привязка к сетке"), QStringLiteral("snap"), QKeySequence(tr("Ctrl+Shift+'")), [] {});
    snap->setToolTip(tr("Привязка к сетке (удерживайте Alt, чтобы двигать без привязки)"));
    snap->setCheckable(true);
    connect(snap, &QAction::toggled, this, [this](bool on) {
        m_scene->setSnapEnabled(on);
        QSettings().setValue(QStringLiteral("view/snap"), on);
    });
    QAction* frames = mk(QStringLiteral("frames"), tr("Рамки объектов"), QString(), QKeySequence(), [] {});
    frames->setCheckable(true);
    frames->setChecked(true);
    connect(frames, &QAction::toggled, m_scene, &LabelScene::setShowObjectFrames);

    mk(QStringLiteral("dataSource"), tr("&Источник данных…"), QStringLiteral("data"), QKeySequence(tr("Ctrl+Shift+D")),
       [this] { editDataSource(); });
    mk(QStringLiteral("reloadData"), tr("&Обновить данные"), QString(), QKeySequence(tr("F5")), [this] { loadDataFromConfig(true); });
    mk(QStringLiteral("varsHelp"), tr("Переменные и поля…"), QString(), QKeySequence(tr("F1")), [this] { showVariablesHelp(); });
    mk(QStringLiteral("about"), tr("О программе…"), QString(), QKeySequence(), [this] { showAbout(); });
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&Файл"));
    file->addAction(m_act.value(QStringLiteral("new")));
    file->addAction(m_act.value(QStringLiteral("open")));
    m_recentMenu = file->addMenu(tr("Недавние файлы"));
    file->addAction(m_act.value(QStringLiteral("save")));
    file->addAction(m_act.value(QStringLiteral("saveAs")));
    file->addSeparator();
    file->addAction(m_act.value(QStringLiteral("saveTemplate")));
    file->addAction(m_act.value(QStringLiteral("templates")));
    file->addAction(m_act.value(QStringLiteral("sheets")));
    file->addSeparator();
    file->addAction(m_act.value(QStringLiteral("print")));
    file->addAction(m_act.value(QStringLiteral("pdf")));
    file->addSeparator();
    file->addAction(m_act.value(QStringLiteral("quit")));
    updateRecentMenu();

    QMenu* edit = menuBar()->addMenu(tr("&Правка"));
    for (const char* id : {"undo", "redo", "", "cut", "copy", "paste", "duplicate", "delete", "", "selectAll"}) {
        if (!*id)
            edit->addSeparator();
        else
            edit->addAction(m_act.value(QLatin1String(id)));
    }

    QMenu* ins = menuBar()->addMenu(tr("&Вставка"));
    for (QAction* a : m_toolGroup->actions())
        ins->addAction(a);

    QMenu* arr = menuBar()->addMenu(tr("&Упорядочить"));
    QMenu* al = arr->addMenu(tr("Выровнять"));
    for (const char* id : {"align-left", "align-hcenter", "align-right", "align-top", "align-vcenter", "align-bottom"})
        al->addAction(m_act.value(QLatin1String(id)));
    arr->addAction(m_act.value(QStringLiteral("distH")));
    arr->addAction(m_act.value(QStringLiteral("distV")));
    arr->addSeparator();
    for (const char* id : {"front", "forward", "backward", "back"})
        arr->addAction(m_act.value(QLatin1String(id)));

    m_viewMenu = menuBar()->addMenu(tr("&Вид"));
    for (const char* id : {"zoomIn", "zoomOut", "zoomFit", "zoom100", "", "grid", "snap", "frames"}) {
        if (!*id)
            m_viewMenu->addSeparator();
        else
            m_viewMenu->addAction(m_act.value(QLatin1String(id)));
    }
    m_viewMenu->addSeparator();

    QMenu* data = menuBar()->addMenu(tr("&Данные"));
    data->addAction(m_act.value(QStringLiteral("dataSource")));
    data->addAction(m_act.value(QStringLiteral("reloadData")));

    QMenu* help = menuBar()->addMenu(tr("&Справка"));
    help->addAction(m_act.value(QStringLiteral("varsHelp")));
    help->addAction(m_act.value(QStringLiteral("about")));
}

void MainWindow::createToolbars()
{
    QToolBar* main = addToolBar(tr("Основная"));
    main->setObjectName(QStringLiteral("tbMain"));
    main->setIconSize(QSize(22, 22));
    for (const char* id : {"new", "open", "save", "", "undo", "redo", "", "print", "pdf", "", "dataSource", "", "zoomIn",
                           "zoomOut", "zoomFit", "", "grid", "snap"}) {
        if (!*id)
            main->addSeparator();
        else
            main->addAction(m_act.value(QLatin1String(id)));
    }
    // grid step: presets or any value typed in, e.g. 0.3
    auto* stepLabel = new QLabel(tr(" Шаг: "));
    auto* step = new QComboBox;
    step->setEditable(true);
    step->setInsertPolicy(QComboBox::NoInsert);
    step->setToolTip(tr("Шаг сетки и привязки, мм. Можно ввести своё значение.\n"
                        "Alt при перетаскивании — без привязки; стрелки — 0,1 мм, Shift+стрелки — 1 мм."));
    for (double s : {0.1, 0.25, 0.5, 1.0, 2.0, 2.5, 5.0})
        step->addItem(QLocale().toString(s), s);
    step->setCurrentText(QLocale().toString(m_scene->gridStep()));
    step->setMinimumContentsLength(4);
    auto applyStep = [this, step](const QString& text) {
        QString t = text.trimmed();
        t.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        const double v = t.toDouble(&ok);
        if (!ok || v < 0.01 || v > 100) {
            step->setCurrentText(QLocale().toString(m_scene->gridStep()));
            return;
        }
        m_scene->setGridStep(v);
        QSettings().setValue(QStringLiteral("view/grid"), v);
        statusBar()->showMessage(tr("Шаг сетки: %1 мм").arg(QLocale().toString(v)), 3000);
    };
    connect(step, &QComboBox::textActivated, this, applyStep);
    connect(step->lineEdit(), &QLineEdit::editingFinished, this, [step, applyStep] { applyStep(step->currentText()); });
    main->addWidget(stepLabel);
    main->addWidget(step);
    main->addWidget(new QLabel(tr(" мм")));
    QToolBar* arrange = addToolBar(tr("Упорядочить"));
    arrange->setObjectName(QStringLiteral("tbArrange"));
    arrange->setIconSize(QSize(20, 20));
    for (const char* id : {"align-left", "align-hcenter", "align-right", "align-top", "align-vcenter", "align-bottom", "",
                           "front", "back", "delete"}) {
        if (!*id)
            arrange->addSeparator();
        else
            arrange->addAction(m_act.value(QLatin1String(id)));
    }
    QToolBar* tools = new QToolBar(tr("Инструменты"), this);
    tools->setObjectName(QStringLiteral("tbTools"));
    tools->setIconSize(QSize(24, 24));
    addToolBar(Qt::LeftToolBarArea, tools);
    for (QAction* a : m_toolGroup->actions())
        tools->addAction(a);
    m_viewMenu->addAction(main->toggleViewAction());
    m_viewMenu->addAction(arrange->toggleViewAction());
    m_viewMenu->addAction(tools->toggleViewAction());
}

void MainWindow::createDocks()
{
    m_props = new PropertyPanel;
    auto* dProps = new QDockWidget(tr("Свойства"), this);
    dProps->setObjectName(QStringLiteral("dockProps"));
    dProps->setWidget(m_props);
    addDockWidget(Qt::RightDockWidgetArea, dProps);

    m_objects = new QListWidget;
    m_objects->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto* dObj = new QDockWidget(tr("Объекты"), this);
    dObj->setObjectName(QStringLiteral("dockObjects"));
    dObj->setWidget(m_objects);
    addDockWidget(Qt::RightDockWidgetArea, dObj);
    connect(m_objects, &QListWidget::itemSelectionChanged, this, [this] {
        if (m_syncingSelection)
            return;
        QList<LabelObject*> sel;
        for (QListWidgetItem* it : m_objects->selectedItems()) {
            const int idx = it->data(Qt::UserRole).toInt();
            if (idx >= 0 && idx < int(m_doc.objects.size()))
                sel << m_doc.objects[size_t(idx)].get();
        }
        m_syncingSelection = true;
        m_scene->selectObjects(sel);
        m_syncingSelection = false;
        onSelectionChanged();
    });

    m_dataPanel = new DataPanel;
    auto* dData = new QDockWidget(tr("Данные"), this);
    dData->setObjectName(QStringLiteral("dockData"));
    dData->setWidget(m_dataPanel);
    addDockWidget(Qt::BottomDockWidgetArea, dData);
    connect(m_dataPanel, &DataPanel::currentRecordChanged, this, [this](int r) {
        m_currentRecord = r;
        updatePreviewContext();
    });
    connect(m_dataPanel, &DataPanel::showDataToggled, this, [this] { updatePreviewContext(); });
    connect(m_dataPanel, &DataPanel::editSourceRequested, this, &MainWindow::editDataSource);
    connect(m_dataPanel, &DataPanel::reloadRequested, this, [this] { loadDataFromConfig(true); });
    connect(m_dataPanel, &DataPanel::inlineTableEdited, this, &MainWindow::onInlineTableEdited);
    resizeDocks({dData}, {170}, Qt::Vertical);
    resizeDocks({dProps}, {300}, Qt::Horizontal);
}

// ======================================================================= file

QString MainWindow::baseDir() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).absolutePath();
}

void MainWindow::resetDocument()
{
    m_undo->clear();
    m_snapshot = m_doc.toBytes();
    m_currentRecord = 0;
    m_pasteCount = 0;
    m_scene->setDocument(&m_doc);
    loadDataFromConfig(false);
    onSelectionChanged();
    updateObjectList();
    updateTitle();
    QTimer::singleShot(0, this, [this] { m_view->fitToLabel(QRectF(0, 0, m_doc.label.width, m_doc.label.height)); });
}

void MainWindow::newBlank()
{
    m_doc.clear();
    m_path.clear();
    auto t = std::make_unique<TextObject>();
    t->x = 4;
    t->y = 4;
    t->w = 50;
    t->h = 10;
    t->text = tr("Новая этикетка");
    t->fontSize = 14;
    t->bold = true;
    m_doc.objects.push_back(std::move(t));
    resetDocument();
}

void MainWindow::newFromDialog()
{
    if (!maybeSave())
        return;
    NewLabelDialog dlg(NewLabelDialog::CreateMode, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString err;
    LabelDocument tmp;
    if (!dlg.createDocument(tmp, &err)) {
        QMessageBox::warning(this, tr("Новая этикетка"), err);
        return;
    }
    m_doc.copyFrom(tmp);
    m_path.clear();
    resetDocument();
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    QSettings st;
    const QString path = QFileDialog::getOpenFileName(this, tr("Открыть этикетку"),
                                                      st.value(QStringLiteral("file/lastDir")).toString(),
                                                      tr("Этикетки LabelForge (*.lfl);;Все файлы (*)"));
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    LabelDocument tmp;
    QString err;
    if (!tmp.load(path, &err)) {
        QMessageBox::warning(this, tr("Открыть"), tr("Не удалось открыть %1:\n%2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    m_doc.copyFrom(tmp);
    m_path = QFileInfo(path).absoluteFilePath();
    QSettings().setValue(QStringLiteral("file/lastDir"), QFileInfo(path).absolutePath());
    addRecent(m_path);
    resetDocument();
    return true;
}

bool MainWindow::save()
{
    if (m_path.isEmpty())
        return saveAs();
    QString err;
    if (!m_doc.save(m_path, &err)) {
        QMessageBox::warning(this, tr("Сохранить"), err);
        return false;
    }
    m_undo->setClean();
    addRecent(m_path);
    statusBar()->showMessage(tr("Сохранено: %1").arg(QDir::toNativeSeparators(m_path)), 4000);
    updateTitle();
    return true;
}

bool MainWindow::saveAs()
{
    QSettings st;
    QString suggested = m_path;
    if (suggested.isEmpty()) {
        const QString base = m_doc.title.isEmpty() ? tr("этикетка") : m_doc.title;
        suggested = QDir(st.value(QStringLiteral("file/lastDir")).toString()).filePath(base + QStringLiteral(".lfl"));
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Сохранить этикетку"), suggested, tr("Этикетки LabelForge (*.lfl)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".lfl"), Qt::CaseInsensitive))
        path += QStringLiteral(".lfl");
    m_path = path;
    st.setValue(QStringLiteral("file/lastDir"), QFileInfo(path).absolutePath());
    return save();
}

bool MainWindow::maybeSave()
{
    if (m_undo->isClean())
        return true;
    const auto r = QMessageBox::question(this, tr("Несохранённые изменения"),
                                         tr("Сохранить изменения в этикетке?"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save)
        return save();
    return r == QMessageBox::Discard;
}

void MainWindow::saveAsTemplate()
{
    SaveTemplateDialog dlg(m_doc, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString path, err;
    if (QFileInfo::exists(QDir(TemplateLibrary::userDir()).filePath(dlg.title() + QStringLiteral(".lfl"))) &&
        QMessageBox::question(this, tr("Шаблон"), tr("Шаблон «%1» уже есть. Заменить?").arg(dlg.title())) != QMessageBox::Yes)
        return;
    if (!TemplateLibrary::saveUser(m_doc, dlg.title(), dlg.category(), dlg.description(), dlg.includeData(), &path, &err)) {
        QMessageBox::warning(this, tr("Шаблон"), err);
        return;
    }
    statusBar()->showMessage(tr("Шаблон «%1» сохранён").arg(dlg.title()), 4000);
}

void MainWindow::manageTemplates()
{
    NewLabelDialog dlg(NewLabelDialog::ManageMode, this);
    dlg.exec();
}

void MainWindow::manageSheets()
{
    SheetLayoutDialog dlg(m_doc.print.layoutId, this);
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedId().isEmpty() && dlg.selectedId() != m_doc.print.layoutId) {
        const SheetLayout l = SheetLayoutLibrary::find(dlg.selectedId(), m_doc.label.width, m_doc.label.height);
        if (QMessageBox::question(this, tr("Шаблон листа"),
                                  tr("Применить «%1» к этикетке? Размер станет %2×%3 мм.")
                                      .arg(l.name)
                                      .arg(l.labelW)
                                      .arg(l.labelH)) == QMessageBox::Yes) {
            m_doc.print.layoutId = l.id;
            m_doc.label.width = l.labelW;
            m_doc.label.height = l.labelH;
            m_doc.label.radius = l.radius;
            m_scene->refreshAll();
            commit(tr("Шаблон листа"));
            onSelectionChanged();
            m_view->fitToLabel(QRectF(0, 0, m_doc.label.width, m_doc.label.height));
        }
    }
}

void MainWindow::print()
{
    PrintDialog dlg(m_doc, m_data, baseDir(), this);
    dlg.exec();
    const PrintSettings s = dlg.settings();
    if (s.toJson() != m_doc.print.toJson()) {
        m_doc.print = s;
        commit(tr("Параметры печати"));
    }
}

void MainWindow::quickPdf()
{
    QSettings st;
    const QString dir = st.value(QStringLiteral("print/pdfDir"), baseDir()).toString();
    const QString base = m_path.isEmpty() ? (m_doc.title.isEmpty() ? tr("этикетки") : m_doc.title)
                                          : QFileInfo(m_path).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(this, tr("Экспорт в PDF"), QDir(dir).filePath(base + QStringLiteral(".pdf")),
                                                      tr("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    st.setValue(QStringLiteral("print/pdfDir"), QFileInfo(path).absolutePath());
    PrintJob job(m_doc, m_data, m_doc.print, baseDir());
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = job.exportPdf(path, &err);
    QApplication::restoreOverrideCursor();
    if (!ok)
        QMessageBox::warning(this, tr("Экспорт в PDF"), err);
    else
        statusBar()->showMessage(tr("PDF сохранён: %1 (этикеток: %2, страниц: %3)")
                                     .arg(QDir::toNativeSeparators(path))
                                     .arg(job.labelCount())
                                     .arg(job.pageCount()),
                                 6000);
}

void MainWindow::addRecent(const QString& path)
{
    QSettings st;
    QStringList list = st.value(QStringLiteral("file/recent")).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > MAX_RECENT)
        list.removeLast();
    st.setValue(QStringLiteral("file/recent"), list);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    const QStringList list = QSettings().value(QStringLiteral("file/recent")).toStringList();
    for (const QString& p : list) {
        QAction* a = m_recentMenu->addAction(QDir::toNativeSeparators(p));
        connect(a, &QAction::triggered, this, [this, p] {
            if (maybeSave())
                openFile(p);
        });
    }
    m_recentMenu->setEnabled(!list.isEmpty());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings st;
    st.setValue(QStringLiteral("window/geometry"), saveGeometry());
    st.setValue(QStringLiteral("window/state"), saveState());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        if (path.endsWith(QStringLiteral(".lfl"), Qt::CaseInsensitive)) {
            if (maybeSave())
                openFile(path);
            return;
        }
        if (!QImageReader::imageFormat(path).isEmpty()) {
            insertImageFile(path, QPointF(2, 2));
            return;
        }
    }
}

// ======================================================================= data

void MainWindow::loadDataFromConfig(bool showErrors)
{
    DataTable t;
    QString err;
    if (!loadData(m_doc.data, baseDir(), &t, &err)) {
        t = DataTable();
        m_statusInfo->setText(tr("Ошибка данных: %1").arg(err));
        if (showErrors)
            QMessageBox::warning(this, tr("Данные"), err);
    } else {
        m_statusInfo->setText(t.columns.isEmpty() ? QString()
                                                  : tr("Данные: %1, записей: %2").arg(m_doc.data.describe()).arg(t.rowCount()));
    }
    m_data = t;
    if (m_currentRecord >= m_data.rowCount())
        m_currentRecord = 0;
    m_dataPanel->setData(m_doc.data, m_data);
    updatePreviewContext();
    onSelectionChanged();
}

void MainWindow::editDataSource()
{
    DataSourceDialog dlg(m_doc.data, m_data, baseDir(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_doc.data = dlg.config();
    commit(tr("Источник данных"));
    loadDataFromConfig(true);
}

void MainWindow::onInlineTableEdited(const DataTable& t)
{
    m_doc.data.table = t;
    m_data = t;
    commit(tr("Изменение данных"), QStringLiteral("inline-data"));
    updatePreviewContext();
    m_statusInfo->setText(tr("Данные: %1, записей: %2").arg(m_doc.data.describe()).arg(t.rowCount()));
    m_props->setTarget(&m_doc, m_scene->selectedObjects(), m_data.columns);
}

void MainWindow::updatePreviewContext()
{
    RenderContext ctx;
    ctx.baseDir = baseDir();
    ctx.rawTemplates = !m_dataPanel->showData();
    if (!m_data.isEmpty()) {
        const int r = std::clamp(m_currentRecord, 0, m_data.rowCount() - 1);
        ctx.columns = &m_data.columns;
        ctx.values = &m_data.rows[r];
        ctx.recordNumber = r + 1;
        ctx.sequence = r + 1;
    }
    m_scene->setContext(ctx);
}

// ======================================================================= editing

void MainWindow::addObject(std::unique_ptr<LabelObject> obj, const QString& undoText)
{
    LabelObject* raw = obj.get();
    m_doc.objects.push_back(std::move(obj));
    m_scene->rebuild();
    m_scene->selectObjects({raw});
    commit(undoText);
    updateObjectList();
}

void MainWindow::insertObject(int tool, const QPointF& posIn)
{
    QPointF pos = posIn;
    pos.setX(std::clamp(pos.x(), 0.0, std::max(0.0, m_doc.label.width - 5)));
    pos.setY(std::clamp(pos.y(), 0.0, std::max(0.0, m_doc.label.height - 3)));
    const double maxW = std::max(5.0, m_doc.label.width - pos.x());
    std::unique_ptr<LabelObject> obj;
    switch (tool) {
    case LabelScene::ToolText: {
        auto t = std::make_unique<TextObject>();
        t->w = std::min(40.0, maxW);
        t->h = 7;
        obj = std::move(t);
        break;
    }
    case LabelScene::ToolRect: {
        auto r = std::make_unique<RectObject>();
        r->w = std::min(20.0, maxW);
        r->h = 12;
        obj = std::move(r);
        break;
    }
    case LabelScene::ToolEllipse: {
        auto e = std::make_unique<EllipseObject>();
        e->w = e->h = std::min(14.0, maxW);
        obj = std::move(e);
        break;
    }
    case LabelScene::ToolLine: {
        auto l = std::make_unique<LineObject>();
        l->w = std::min(30.0, maxW);
        pos.ry() -= 1;
        obj = std::move(l);
        break;
    }
    case LabelScene::ToolBarcode: {
        auto b = std::make_unique<BarcodeObject>();
        b->w = std::min(40.0, maxW);
        b->h = 15;
        if (!m_data.columns.isEmpty())
            b->data = QStringLiteral("{%1}").arg(m_data.columns.first());
        obj = std::move(b);
        break;
    }
    case LabelScene::ToolQr: {
        auto b = std::make_unique<BarcodeObject>();
        b->symbology = QStringLiteral("qr");
        b->data = QStringLiteral("https://example.org");
        b->showText = false;
        b->w = b->h = std::min(18.0, std::min(maxW, m_doc.label.height));
        obj = std::move(b);
        break;
    }
    case LabelScene::ToolImage: {
        QStringList filters;
        for (const QByteArray& fmt : QImageReader::supportedImageFormats())
            filters << QStringLiteral("*.") + QString::fromLatin1(fmt);
        const QString path = QFileDialog::getOpenFileName(this, tr("Изображение"), baseDir(),
                                                          tr("Изображения (%1)").arg(filters.join(QLatin1Char(' '))));
        m_toolActions.value(LabelScene::ToolSelect)->trigger();
        if (!path.isEmpty())
            insertImageFile(path, pos);
        return;
    }
    default: return;
    }
    obj->x = pos.x();
    obj->y = pos.y();
    m_toolActions.value(LabelScene::ToolSelect)->trigger();
    addObject(std::move(obj), tr("Добавление объекта"));
}

void MainWindow::insertImageFile(const QString& path, const QPointF& pos)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Изображение"), reader.errorString());
        return;
    }
    if (img.width() > 2000 || img.height() > 2000)
        img = img.scaled(2000, 2000, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    auto im = std::make_unique<ImageObject>();
    im->setEmbedded(img);
    im->x = pos.x();
    im->y = pos.y();
    im->w = std::min(25.0, std::max(5.0, m_doc.label.width - pos.x()));
    im->h = im->w * img.height() / std::max(1, img.width());
    addObject(std::move(im), tr("Добавление изображения"));
}

QList<int> MainWindow::selectedIndices() const
{
    QList<int> idx;
    for (LabelObject* o : m_scene->selectedObjects())
        idx << m_doc.indexOf(o);
    return idx;
}

void MainWindow::selectIndices(const QList<int>& idx)
{
    QList<LabelObject*> sel;
    for (int i : idx)
        if (i >= 0 && i < int(m_doc.objects.size()))
            sel << m_doc.objects[size_t(i)].get();
    m_scene->selectObjects(sel);
}

void MainWindow::deleteSelection()
{
    const QList<LabelObject*> sel = m_scene->selectedObjects();
    if (sel.isEmpty())
        return;
    auto& v = m_doc.objects;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const std::unique_ptr<LabelObject>& o) { return sel.contains(o.get()); }),
            v.end());
    m_scene->rebuild();
    commit(tr("Удаление"));
    onSelectionChanged();
    updateObjectList();
}

void MainWindow::copySelection()
{
    const QList<LabelObject*> sel = m_scene->selectedObjects();
    if (sel.isEmpty())
        return;
    QJsonArray arr;
    for (LabelObject* o : sel)
        arr.append(o->toJson());
    auto* mime = new QMimeData;
    const QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    mime->setData(QLatin1String(MIME_OBJECTS), json);
    QStringList texts;
    for (LabelObject* o : sel)
        if (auto* t = dynamic_cast<TextObject*>(o))
            texts << t->text;
    if (!texts.isEmpty())
        mime->setText(texts.join(QLatin1Char('\n')));
    QApplication::clipboard()->setMimeData(mime);
    m_pasteCount = 0;
}

void MainWindow::cutSelection()
{
    copySelection();
    deleteSelection();
}

QList<LabelObject*> MainWindow::pasteObjects(const QByteArray& json, double offset)
{
    QList<LabelObject*> added;
    for (const QJsonValue& v : QJsonDocument::fromJson(json).array()) {
        auto o = LabelObject::fromJsonAny(v.toObject());
        if (!o)
            continue;
        o->x += offset;
        o->y += offset;
        added << o.get();
        m_doc.objects.push_back(std::move(o));
    }
    return added;
}

void MainWindow::paste()
{
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime)
        return;
    QList<LabelObject*> added;
    if (mime->hasFormat(QLatin1String(MIME_OBJECTS))) {
        ++m_pasteCount;
        added = pasteObjects(mime->data(QLatin1String(MIME_OBJECTS)), 2.0 * m_pasteCount);
    } else if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            auto im = std::make_unique<ImageObject>();
            im->setEmbedded(img);
            im->x = im->y = 2;
            im->w = std::min(30.0, m_doc.label.width - 4);
            im->h = im->w * img.height() / std::max(1, img.width());
            added << im.get();
            m_doc.objects.push_back(std::move(im));
        }
    } else if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        auto t = std::make_unique<TextObject>();
        t->text = mime->text();
        t->x = t->y = 2;
        t->w = std::min(40.0, m_doc.label.width - 4);
        t->h = 8;
        added << t.get();
        m_doc.objects.push_back(std::move(t));
    }
    if (added.isEmpty())
        return;
    m_scene->rebuild();
    m_scene->selectObjects(added);
    commit(tr("Вставка"));
    updateObjectList();
}

void MainWindow::duplicate()
{
    const QList<LabelObject*> sel = m_scene->selectedObjects();
    if (sel.isEmpty())
        return;
    QJsonArray arr;
    for (LabelObject* o : sel)
        arr.append(o->toJson());
    const QList<LabelObject*> added = pasteObjects(QJsonDocument(arr).toJson(), 2.0);
    m_scene->rebuild();
    m_scene->selectObjects(added);
    commit(tr("Дублирование"));
    updateObjectList();
}

void MainWindow::selectAll()
{
    QList<LabelObject*> all;
    for (const auto& o : m_doc.objects)
        all << o.get();
    m_scene->selectObjects(all);
}

void MainWindow::align(int mode)
{
    const QList<LabelObject*> sel = m_scene->selectedObjects();
    if (sel.isEmpty())
        return;
    QRectF ref;
    if (sel.size() == 1) {
        ref = QRectF(0, 0, m_doc.label.width, m_doc.label.height);
    } else {
        for (LabelObject* o : sel)
            ref = ref.isNull() ? o->rect() : ref.united(o->rect());
    }
    for (LabelObject* o : sel) {
        if (o->locked)
            continue;
        switch (mode) {
        case 0: o->x = ref.left(); break;
        case 1: o->x = ref.center().x() - o->w / 2; break;
        case 2: o->x = ref.right() - o->w; break;
        case 3: o->y = ref.top(); break;
        case 4: o->y = ref.center().y() - o->h / 2; break;
        case 5: o->y = ref.bottom() - o->h; break;
        }
        m_scene->refreshObject(o);
    }
    commit(tr("Выравнивание"));
    m_props->refreshGeometry();
}

void MainWindow::distribute(bool horizontal)
{
    QList<LabelObject*> sel = m_scene->selectedObjects();
    if (sel.size() < 3) {
        statusBar()->showMessage(tr("Для распределения выделите не меньше трёх объектов"), 4000);
        return;
    }
    std::sort(sel.begin(), sel.end(), [horizontal](LabelObject* a, LabelObject* b) {
        return horizontal ? a->x < b->x : a->y < b->y;
    });
    double start = horizontal ? sel.first()->x : sel.first()->y;
    double end = horizontal ? sel.last()->x + sel.last()->w : sel.last()->y + sel.last()->h;
    double total = 0;
    for (LabelObject* o : sel)
        total += horizontal ? o->w : o->h;
    const double gap = (end - start - total) / (sel.size() - 1);
    double pos = start;
    for (LabelObject* o : sel) {
        if (horizontal)
            o->x = pos;
        else
            o->y = pos;
        pos += (horizontal ? o->w : o->h) + gap;
        m_scene->refreshObject(o);
    }
    commit(tr("Распределение"));
    m_props->refreshGeometry();
}

void MainWindow::zOrder(int mode)
{
    const QList<LabelObject*> sel = m_scene->selectedObjects();
    if (sel.isEmpty())
        return;
    auto& v = m_doc.objects;
    auto isSel = [&](const std::unique_ptr<LabelObject>& o) { return sel.contains(o.get()); };
    if (mode == 0) {  // front
        std::stable_partition(v.begin(), v.end(), [&](const auto& o) { return !isSel(o); });
    } else if (mode == 3) {  // back
        std::stable_partition(v.begin(), v.end(), isSel);
    } else if (mode == 1) {  // forward one step
        for (int i = int(v.size()) - 2; i >= 0; --i)
            if (isSel(v[size_t(i)]) && !isSel(v[size_t(i + 1)]))
                std::swap(v[size_t(i)], v[size_t(i + 1)]);
    } else {  // backward one step
        for (size_t i = 1; i < v.size(); ++i)
            if (isSel(v[i]) && !isSel(v[i - 1]))
                std::swap(v[i], v[i - 1]);
    }
    m_scene->rebuild();
    m_scene->selectObjects(sel);
    commit(tr("Порядок"));
    updateObjectList();
}

void MainWindow::nudge(double dx, double dy)
{
    const QList<LabelObject*> sel = m_scene->selectedObjects();
    if (sel.isEmpty())
        return;
    for (LabelObject* o : sel) {
        if (o->locked)
            continue;
        o->x += dx;
        o->y += dy;
        m_scene->refreshObject(o);
    }
    commit(tr("Перемещение"), QStringLiteral("nudge"));
    m_props->refreshGeometry();
}

// ======================================================================= sync

void MainWindow::commit(const QString& text, const QString& mergeKey)
{
    const QByteArray now = m_doc.toBytes();
    if (now == m_snapshot)
        return;
    m_undo->push(new SnapshotCommand(m_snapshot, now, text, mergeKey, [this](const QByteArray& b) { restoreSnapshot(b); }));
    m_snapshot = now;
    updateTitle();
}

void MainWindow::restoreSnapshot(const QByteArray& bytes)
{
    const QList<int> sel = selectedIndices();
    const QJsonObject oldData = m_doc.data.toJson();
    m_doc.fromBytes(bytes);
    m_snapshot = bytes;
    m_scene->rebuild();
    if (m_doc.data.toJson() != oldData) {
        if (m_doc.data.kind == DataKind::Inline) {
            m_data = m_doc.data.table;
            m_dataPanel->setData(m_doc.data, m_data);
            updatePreviewContext();
        } else {
            loadDataFromConfig(false);
        }
    } else {
        updatePreviewContext();
    }
    selectIndices(sel);
    onSelectionChanged();
    updateObjectList();
    updateTitle();
}

void MainWindow::onSelectionChanged()
{
    if (!m_props)
        return;
    m_props->setTarget(&m_doc, m_scene->selectedObjects(), m_data.columns);
    syncListSelection();
    updateActions();
}

void MainWindow::syncListSelection()
{
    if (m_syncingSelection)
        return;
    const QList<LabelObject*> sel = m_scene->selectedObjects();
    m_syncingSelection = true;
    for (int i = 0; i < m_objects->count(); ++i) {
        QListWidgetItem* it = m_objects->item(i);
        const int idx = it->data(Qt::UserRole).toInt();
        it->setSelected(idx >= 0 && idx < int(m_doc.objects.size()) && sel.contains(m_doc.objects[size_t(idx)].get()));
    }
    m_syncingSelection = false;
}

void MainWindow::onObjectEdited(LabelObject* obj, const QString& key)
{
    m_scene->refreshObject(obj);
    m_scene->update();
    commit(tr("Изменение свойства"), QStringLiteral("%1:%2").arg(m_doc.indexOf(obj)).arg(key));
    if (key == QLatin1String("name") || key == QLatin1String("text") || key == QLatin1String("data") ||
        key == QLatin1String("symbology"))
        updateObjectList();
}

void MainWindow::onLabelEdited(const QString& key)
{
    m_scene->refreshAll();
    commit(tr("Параметры этикетки"), QStringLiteral("label:") + key);
}

void MainWindow::updateObjectList()
{
    m_syncingSelection = true;
    m_objects->clear();
    for (int i = int(m_doc.objects.size()) - 1; i >= 0; --i) {
        LabelObject* o = m_doc.objects[size_t(i)].get();
        QString detail = o->name;
        if (detail.isEmpty()) {
            if (auto* t = dynamic_cast<TextObject*>(o))
                detail = t->text.section(QLatin1Char('\n'), 0, 0).left(40);
            else if (auto* b = dynamic_cast<BarcodeObject*>(o))
                detail = QStringLiteral("%1: %2").arg(b->symbology, b->data.left(30));
        }
        QString icon = o->typeName();
        if (auto* b = dynamic_cast<BarcodeObject*>(o))
            icon = barcode::is2D(b->symbology) ? QStringLiteral("qr") : QStringLiteral("barcode");
        auto* it = new QListWidgetItem(icons::get(icon), detail.isEmpty() ? o->displayType()
                                                                          : QStringLiteral("%1 — %2").arg(o->displayType(), detail));
        it->setData(Qt::UserRole, i);
        if (!o->printable)
            it->setForeground(QColor(170, 60, 60));
        m_objects->addItem(it);
    }
    m_syncingSelection = false;
    syncListSelection();
}

void MainWindow::updateTitle()
{
    const QString name = m_path.isEmpty() ? tr("Без имени") : QFileInfo(m_path).fileName();
    setWindowTitle(QStringLiteral("%1[*] — LabelForge").arg(name));
    setWindowModified(!m_undo->isClean());
}

void MainWindow::updateActions()
{
    const int n = int(m_scene->selectedObjects().size());
    for (const char* id : {"cut", "copy", "duplicate", "delete", "front", "back", "forward", "backward"})
        m_act.value(QLatin1String(id))->setEnabled(n > 0);
    for (const char* id : {"align-left", "align-hcenter", "align-right", "align-top", "align-vcenter", "align-bottom"})
        m_act.value(QLatin1String(id))->setEnabled(n > 0);
    m_act.value(QStringLiteral("distH"))->setEnabled(n >= 3);
    m_act.value(QStringLiteral("distV"))->setEnabled(n >= 3);
}

void MainWindow::showVariablesHelp()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Переменные и поля"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>Поля данных</h3>"
        "<p><code>{Наименование}</code> — значение поля текущей записи (регистр не важен).<br>"
        "<code>{Цена:0.00}</code>, <code>{Цена:0,00}</code> — число с двумя знаками; "
        "<code>{Код:000000}</code> — с ведущими нулями;<br>"
        "<code>{Имя:upper}</code>, <code>{Имя:lower}</code>, <code>{Имя:trim}</code>.</p>"
        "<h3>Счётчики и дата</h3>"
        "<p><code>{seq}</code> — номер этикетки в задании (1, 2, 3…)<br>"
        "<code>{seq:0001}</code> — с ведущими нулями; <code>{seq:1000}</code> — начать с 1000;<br>"
        "<code>{seq:100:10}</code> — начать со 100 с шагом 10<br>"
        "<code>{rec}</code> — номер записи данных<br>"
        "<code>{date}</code>, <code>{date:dd.MM.yyyy}</code>, <code>{date+30}</code> — дата (+30 дней — срок годности)<br>"
        "<code>{time}</code>, <code>{time:HH:mm:ss}</code> — время</p>"
        "<h3>Штрихкоды</h3>"
        "<p>В данных штрихкода: <code>\\F</code> — FNC1, <code>\\G</code> — разделитель GS (ASCII 29), "
        "<code>\\n</code> — перевод строки.<br>"
        "GS1-128 и GS1 DataMatrix понимают запись <code>(01)04601234567893(21)ABC</code>.<br>"
        "EAN-13/EAN-8/UPC-A: контрольная цифра вычисляется автоматически.</p>"
        "<p>Чтобы напечатать фигурную скобку, удвойте её: <code>{{</code>, <code>}}</code>.</p>"));
    box.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("О программе LabelForge"),
                       tr("<h3>LabelForge %1</h3>"
                          "<p>Свободная программа для дизайна и печати этикеток, ценников и штрихкодов.</p>"
                          "<p>Лицензия: GNU GPL v3 или новее.<br>"
                          "QR-коды: библиотека Project Nayuki (MIT).<br>"
                          "Интерфейс: Qt %2 (LGPL v3).</p>")
                           .arg(QStringLiteral(LABELFORGE_VERSION), QString::fromLatin1(qVersion())));
}

} // namespace lf
