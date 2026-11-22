// SPDX-License-Identifier: GPL-3.0-or-later
// UI smoke test: drives the main window and dialogs offscreen and saves screenshots.
// Usage: QT_QPA_PLATFORM=offscreen labelforge_uitest <output-dir>
#include "core/LabelDocument.h"
#include "core/ShapePaths.h"
#include "data/DataLoader.h"
#include "templates/TemplateLibrary.h"
#include "ui/ClipartPanel.h"
#include "ui/IconDownloadDialog.h"
#include "ui/Icons.h"
#include "TinyHttp.h"
#include "ui/DataSourceDialog.h"
#include "ui/LabelScene.h"
#include "ui/LabelView.h"
#include "ui/MainWindow.h"
#include "ui/NewLabelDialog.h"
#include "ui/ObjectItem.h"
#include "ui/PrintDialog.h"
#include "ui/PropertyPanel.h"
#include <QPlainTextEdit>
#include <QPointer>
#include <QPrinterInfo>
#include "ui/SheetLayoutDialog.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QComboBox>
#include <QDockWidget>
#include <QDropEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QFile>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolButton>
#include <QWindow>
#include <QTest>
#include <QUndoStack>
#include <cstdio>

using namespace lf;

static int failures = 0;
#define CHECK(cond, msg)                                                         \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        } else {                                                                 \
            std::printf("ok: %s\n", msg);                                        \
        }                                                                        \
    } while (0)

static void settle(int ms = 150)
{
    QTest::qWait(ms);
    QApplication::processEvents();
}

static void shot(QWidget* w, const QDir& out, const QString& name)
{
    settle();
    w->grab().save(out.filePath(name + QStringLiteral(".png")));
}

static void mouse(QWidget* w, QEvent::Type type, const QPoint& pos, Qt::MouseButtons buttons,
                  Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent e(type, QPointF(pos), w->mapToGlobal(QPointF(pos)), Qt::LeftButton, buttons, mods);
    QApplication::sendEvent(w, &e);
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LabelForgeUiTest"));
    QApplication::setOrganizationName(QStringLiteral("LabelForgeTest"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QDir out(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("uitest-out"));
    out.mkpath(QStringLiteral("."));

    // a template saved as a normal file
    LabelDocument tdoc;
    QString err;
    TemplateInfo price;
    for (const TemplateInfo& t : TemplateLibrary::builtins())
        if (t.id == QLatin1String("price-58x40"))
            price = t;
    TemplateLibrary::load(price, tdoc, &err);
    const QString file = out.filePath(QStringLiteral("price.lfl"));
    CHECK(tdoc.save(file, &err), "save template file");

    MainWindow w;
    w.resize(1400, 900);
    w.show();
    CHECK(w.openFile(file), "open file");
    settle(400);
    shot(&w, out, QStringLiteral("01_main"));

    auto* view = w.findChild<LabelView*>();
    auto* scene = static_cast<LabelScene*>(view->scene());
    auto* undo = w.findChild<QUndoStack*>();
    CHECK(view && scene && undo, "find widgets");

    // select the barcode
    ObjectItem* bcItem = nullptr;
    for (QGraphicsItem* it : scene->items())
        if (it->type() == ObjectItem::Type && dynamic_cast<BarcodeObject*>(static_cast<ObjectItem*>(it)->object()))
            bcItem = static_cast<ObjectItem*>(it);
    CHECK(bcItem != nullptr, "barcode item present");
    scene->selectObjects({bcItem->object()});
    settle();
    shot(&w, out, QStringLiteral("02_barcode_selected"));

    // resize with the bottom-right handle
    const double w0 = bcItem->object()->w;
    const QPoint start = view->mapFromScene(bcItem->mapToScene(QPointF(bcItem->object()->w, bcItem->object()->h)));
    QWidget* vp = view->viewport();
    // the date text lies over this corner: the selected item's handle must still win
    CHECK(scene->itemAt(view->mapToScene(start), QTransform()) != bcItem, "resize handle is under another object");
    mouse(vp, QEvent::MouseButtonPress, start, Qt::LeftButton);
    mouse(vp, QEvent::MouseMove, start + QPoint(20, 10), Qt::LeftButton);
    mouse(vp, QEvent::MouseMove, start + QPoint(40, 20), Qt::LeftButton);
    mouse(vp, QEvent::MouseButtonRelease, start + QPoint(40, 20), Qt::NoButton);
    settle();
    const double w1 = bcItem->object()->w;
    std::printf("barcode width %.2f -> %.2f\n", w0, w1);
    CHECK(w1 > w0 + 1, "handle resize grows the object");
    CHECK(undo->count() >= 1, "resize recorded for undo");

    // move by dragging the body
    {
        ObjectItem* it = nullptr;
        for (QGraphicsItem* gi : scene->items())
            if (gi->type() == ObjectItem::Type && dynamic_cast<BarcodeObject*>(static_cast<ObjectItem*>(gi)->object()))
                it = static_cast<ObjectItem*>(gi);
        const double x0 = it->object()->x;
        const QPoint c = view->mapFromScene(it->mapToScene(QPointF(it->object()->w / 2, it->object()->h / 3)));
        mouse(vp, QEvent::MouseButtonPress, c, Qt::LeftButton);
        mouse(vp, QEvent::MouseMove, c + QPoint(15, 0), Qt::LeftButton);
        mouse(vp, QEvent::MouseMove, c + QPoint(30, 0), Qt::LeftButton);
        mouse(vp, QEvent::MouseButtonRelease, c + QPoint(30, 0), Qt::NoButton);
        settle();
        std::printf("barcode x %.2f -> %.2f\n", x0, it->object()->x);
        CHECK(it->object()->x > x0 + 1, "drag moves the object");
        CHECK(std::abs(it->object()->x - std::round(it->object()->x)) < 1e-9, "plain drag snaps to the 1 mm grid");
        undo->undo();
        settle();

        // Alt-drag by a fraction of the grid step: no snapping (items are rebuilt by undo, find it again)
        for (QGraphicsItem* gi : scene->items())
            if (gi->type() == ObjectItem::Type && dynamic_cast<BarcodeObject*>(static_cast<ObjectItem*>(gi)->object()))
                it = static_cast<ObjectItem*>(gi);
        scene->selectObjects({it->object()});
        settle();
        const double ppm = view->transform().m11();
        const int dx = std::max(1, int(std::lround(ppm * 0.4)));  // ~0.4 mm
        const double xa = it->object()->x;
        const QPoint c2 = view->mapFromScene(it->mapToScene(QPointF(it->object()->w / 2, it->object()->h / 3)));
        mouse(vp, QEvent::MouseButtonPress, c2, Qt::LeftButton, Qt::AltModifier);
        mouse(vp, QEvent::MouseMove, c2 + QPoint(dx / 2 + 1, 0), Qt::LeftButton, Qt::AltModifier);
        mouse(vp, QEvent::MouseMove, c2 + QPoint(dx, 0), Qt::LeftButton, Qt::AltModifier);
        mouse(vp, QEvent::MouseButtonRelease, c2 + QPoint(dx, 0), Qt::NoButton, Qt::AltModifier);
        settle();
        const double moved = it->object()->x - xa;
        std::printf("alt-drag %d px (%.2f px/mm): x %.3f -> %.3f\n", dx, ppm, xa, it->object()->x);
        CHECK(moved > 0.05 && moved < 0.9, "Alt-drag moves by less than one grid step");
        undo->undo();
        settle();
    }
    undo->undo();  // undo resize
    settle();
    double wUndo = 0;
    for (QGraphicsItem* it : scene->items())
        if (it->type() == ObjectItem::Type && dynamic_cast<BarcodeObject*>(static_cast<ObjectItem*>(it)->object()))
            wUndo = static_cast<ObjectItem*>(it)->object()->w;
    CHECK(std::abs(wUndo - w0) < 1e-6, "undo restores the size");

    // typing into the text property must not rebuild the panel (focus stays)
    {
        ObjectItem* textItem = nullptr;
        for (QGraphicsItem* gi : scene->items())
            if (gi->type() == ObjectItem::Type && dynamic_cast<TextObject*>(static_cast<ObjectItem*>(gi)->object()))
                textItem = static_cast<ObjectItem*>(gi);
        scene->selectObjects({textItem->object()});
        settle();
        auto* panel = w.findChild<PropertyPanel*>();
        QPointer<QPlainTextEdit> edit = panel->findChild<QPlainTextEdit*>();
        CHECK(!edit.isNull(), "text editor in property panel");
        edit->setFocus();
        edit->moveCursor(QTextCursor::End);
        QTest::keyClicks(edit, QStringLiteral(" SALE"));
        settle();
        auto* to = static_cast<TextObject*>(textItem->object());
        CHECK(!edit.isNull() && panel->findChild<QPlainTextEdit*>() == edit.data(), "editor survives typing");
        CHECK(to->text.endsWith(QStringLiteral(" SALE")), "typed text applied to object");
        const int steps = undo->count();
        undo->undo();
        settle();
        CHECK(undo->count() == steps && undo->index() == steps - 1, "typing merged into one undo step");
        undo->redo();
        undo->undo();
        settle();
    }

    // insert a text with the text tool
    const int before = int(scene->document()->objects.size());
    scene->setTool(LabelScene::ToolText);
    const QPoint p = view->mapFromScene(QPointF(5, 30));
    mouse(vp, QEvent::MouseButtonPress, p, Qt::LeftButton);
    mouse(vp, QEvent::MouseButtonRelease, p, Qt::NoButton);
    settle();
    CHECK(int(scene->document()->objects.size()) == before + 1, "text tool inserts an object");
    shot(&w, out, QStringLiteral("03_text_inserted"));
    undo->undo();
    settle();
    CHECK(int(scene->document()->objects.size()) == before, "undo removes inserted object");
    scene->clearSelection();
    settle();
    shot(&w, out, QStringLiteral("04_label_props"));

    // drop an SVG file: becomes an embedded image with its own property section
    {
        const QString svgPath = out.filePath(QStringLiteral("icon.svg"));
        QFile sf(svgPath);
        sf.open(QIODevice::WriteOnly);
        sf.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\">"
                 "<rect width=\"40\" height=\"20\" rx=\"4\" fill=\"#2a7\"/></svg>");
        sf.close();
        const int n = int(scene->document()->objects.size());
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(svgPath)});
        // Qt delivers drops through the window, after an accepted DragEnter; aim at the
        // status bar, which does not take drops itself, so the main window gets them
        const QPoint at = w.statusBar()->mapTo(&w, w.statusBar()->rect().center());
        QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.windowHandle(), &enter);
        QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.windowHandle(), &drop);
        settle();
        CHECK(int(scene->document()->objects.size()) == n + 1, "dropped svg inserts an object");
        auto* im = dynamic_cast<ImageObject*>(scene->document()->objects.back().get());
        CHECK(im && !im->svgData.isEmpty() && std::abs(im->h * 2 - im->w) < 0.01, "svg embedded with its aspect ratio");
        if (im) {
            scene->selectObjects({im});
            settle();
            shot(&w, out, QStringLiteral("05_image_props"));
            auto* panel = w.findChild<PropertyPanel*>();
            panel->verticalScrollBar()->setValue(panel->verticalScrollBar()->maximum());
            shot(panel, out, QStringLiteral("06_image_props_bottom"));
        }
        undo->undo();
        settle();
    }

    // clipart panel: built-in set, search, double-click insert, drop under the cursor
    {
        auto* clip = w.findChild<ClipartPanel*>();
        CHECK(clip != nullptr, "clipart panel present");
        auto* dock = qobject_cast<QDockWidget*>(clip->parentWidget());
        if (dock) {
            dock->show();
            dock->raise();
        }
        settle();
        const QStringList cats = clip->categories();
        std::printf("clipart: %d items, categories: %s\n", clip->visibleCount(), qPrintable(cats.join(QStringLiteral(", "))));
        CHECK(clip->visibleCount() >= 50, "clipart: built-in set listed");
        CHECK(cats.contains(QStringLiteral("Обращение с грузом")) && cats.contains(QStringLiteral("Опасность (GHS)")) &&
                  cats.contains(QStringLiteral("Переработка")),
              "clipart: subfolders become categories");
        shot(&w, out, QStringLiteral("07_clipart"));
        clip->setCategory(int(cats.indexOf(QStringLiteral("Опасность (GHS)"))) + 1);
        settle();
        CHECK(clip->visibleCount() == 9, "clipart: category tab filters");
        shot(clip, out, QStringLiteral("08_clipart_ghs"));
        clip->setCategory(0);
        clip->setFilter(QStringLiteral("хрупк"));
        settle();
        CHECK(clip->visibleCount() == 1, "clipart: search is case-insensitive");

        auto* list = clip->findChild<QListView*>(QStringLiteral("clipartList"));
        const QModelIndex first = list->model()->index(0, 0);
        const int n = int(scene->document()->objects.size());
        const QPoint ip = list->visualRect(first).center();
        // the same event sequence a real double click produces
        QWidget* lvp = list->viewport();
        mouse(lvp, QEvent::MouseButtonPress, ip, Qt::LeftButton);
        mouse(lvp, QEvent::MouseButtonRelease, ip, Qt::NoButton);
        mouse(lvp, QEvent::MouseButtonDblClick, ip, Qt::LeftButton);
        mouse(lvp, QEvent::MouseButtonRelease, ip, Qt::NoButton);
        settle();
        CHECK(int(scene->document()->objects.size()) == n + 1, "clipart: double click inserts");
        auto* im = dynamic_cast<ImageObject*>(scene->document()->objects.back().get());
        CHECK(im && !im->svgData.isEmpty() && im->pathTemplate.isEmpty(), "clipart: inserted picture is embedded");
        undo->undo();
        settle();

        // drag data of an item is a file URL; drop it on the label at (10, 6) mm
        std::unique_ptr<QMimeData> mime(list->model()->mimeData({first}));
        CHECK(mime && mime->hasUrls() && mime->urls().value(0).isLocalFile(), "clipart: drags as a file URL");
        const QPoint at = view->viewport()->mapTo(&w, view->mapFromScene(QPointF(10, 6)));
        QDragEnterEvent enter(at, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.windowHandle(), &enter);
        QDragMoveEvent move(at, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.windowHandle(), &move);
        QDropEvent drop(QPointF(at), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.windowHandle(), &drop);
        settle();
        CHECK(int(scene->document()->objects.size()) == n + 1, "clipart: drop on the label inserts");
        im = dynamic_cast<ImageObject*>(scene->document()->objects.back().get());
        if (im)
            std::printf("clipart drop at %.2f, %.2f mm\n", im->x, im->y);
        CHECK(im && std::abs(im->x - 10) < 0.5 && std::abs(im->y - 6) < 0.5, "clipart: dropped where the mouse is");
        undo->undo();
        clip->setFilter(QString());
        settle();
    }

    // icon downloads from a fake Iconify server: search, add to the gallery, a whole set
    {
        auto* clip = w.findChild<ClipartPanel*>();
        const QString setDir = ClipartPanel::userDir() + QStringLiteral("/Test Icons");
        const QString manyDir = ClipartPanel::userDir() + QStringLiteral("/UiTest Many");
        QDir(setDir).removeRecursively();
        QDir(manyDir).removeRecursively();

        QStringList setNames;
        for (int i = 0; i < 350; ++i)
            setNames << QStringLiteral("%1-%2").arg(QChar(QLatin1Char("abcde"[i % 5]))).arg(i, 3, 10, QLatin1Char('0'));
        TinyHttp http([setNames](const QByteArray& target) {
            const QByteArray json = "application/json";
            const QByteArray testset =
                R"({"name": "Test Icons", "total": 350, "license": {"title": "MIT", "spdx": "MIT"}, "category": "Test"})";
            const QByteArray ncset =
                R"({"name": "NC Icons", "total": 5, "license": {"title": "CC BY-NC 4.0", "spdx": "CC-BY-NC-4.0"}})";
            if (target == "/collections")
                return TinyHttp::response(200, json, "{\"testset\": " + testset + ", \"ncset\": " + ncset + "}");
            if (target.startsWith("/search?"))
                return TinyHttp::response(200, json,
                                          R"({"icons": ["testset:box", "ncset:box", "testset:box-open"], "total": 3,
                                              "collections": {"testset": )" +
                                              testset + ", \"ncset\": " + ncset + "}}");
            if (target == "/collection?prefix=testset") {
                QJsonObject o;
                o[QStringLiteral("prefix")] = QStringLiteral("testset");
                o[QStringLiteral("uncategorized")] = QJsonArray::fromStringList(setNames);
                o[QStringLiteral("hidden")] = QJsonArray({QStringLiteral("a-000")});
                return TinyHttp::response(200, json, QJsonDocument(o).toJson());
            }
            if (target.startsWith("/testset.json?icons=")) {
                QJsonObject icons;
                for (const QByteArray& n : target.mid(target.indexOf('=') + 1).split(','))
                    icons[QString::fromLatin1(n)] = QJsonObject(
                        {{QStringLiteral("body"), QStringLiteral("<path d=\"M4 4h16v16H4z\" fill=\"currentColor\"/>")}});
                QJsonObject o;
                o[QStringLiteral("prefix")] = QStringLiteral("testset");
                o[QStringLiteral("width")] = 24;
                o[QStringLiteral("height")] = 24;
                o[QStringLiteral("icons")] = icons;
                return TinyHttp::response(200, json, QJsonDocument(o).toJson());
            }
            return TinyHttp::response(404, QByteArray(), QByteArray());
        });
        qputenv("LABELFORGE_ICONIFY_URL", http.base().toUtf8());

        IconDownloadDialog* dlg = clip->openDownloads(QStringLiteral("коробка"));
        CHECK(QTest::qWaitFor([dlg] { return !dlg->busy(); }, 5000), "icons: request finished");
        std::printf("icon search: %d results, %s\n", dlg->resultCount(), qPrintable(dlg->status()));
        CHECK(dlg->resultCount() == 2, "icons: russian query searched, non-free set dropped");
        shot(dlg, out, QStringLiteral("13_icon_search"));
        dlg->selectAllResults();
        dlg->addSelected();
        CHECK(QFile::exists(setDir + QStringLiteral("/box.svg")) && QFile::exists(setDir + QStringLiteral("/box-open.svg")) &&
                  QFile::exists(setDir + QStringLiteral("/LICENSE.txt")),
              "icons: search results saved to the set folder");

        dlg->showSets();
        CHECK(QTest::qWaitFor([dlg] { return !dlg->busy(); }, 5000), "icons: request finished");
        CHECK(dlg->setCount() == 1, "icons: set list shows free sets only");
        CHECK(dlg->downloadSet(QStringLiteral("testset")), "icons: set download starts");
        CHECK(QTest::qWaitFor([dlg] { return !dlg->busy(); }, 20000), "icons: set download finished");
        std::printf("icon set: %s\n", qPrintable(dlg->status()));
        int files = 0;
        for (const QString& letter : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D"),
                                      QStringLiteral("E")})
            files += int(QDir(setDir + QLatin1Char('/') + letter).entryList({QStringLiteral("*.svg")}).size());
        CHECK(files == 349, "icons: whole set saved in letter folders, hidden icon skipped");
        CHECK(QFile::exists(setDir + QStringLiteral("/iconify-set.json")), "icons: set marked as complete");
        shot(dlg, out, QStringLiteral("14_icon_sets"));
        dlg->close();
        settle(700);  // the panel rescans by itself after downloads

        const QStringList cats = clip->categories();
        CHECK(cats.contains(QStringLiteral("Test Icons")), "icons: set appears as a gallery tab");
        clip->setFilter(QString());
        const int before = ClipartPanel::thumbnailsRendered();
        clip->setCategory(int(cats.indexOf(QStringLiteral("Test Icons"))) + 1);
        settle(600);
        const int rendered = ClipartPanel::thumbnailsRendered() - before;
        std::printf("icon set tab: %d items, %d thumbnails rendered, sections %s\n", clip->visibleCount(), rendered,
                    qPrintable(clip->sections().join(QLatin1Char(','))));
        CHECK(clip->visibleCount() == 351, "icons: tab lists all icons of the set");
        // the visible ones may already be cached from the "all" tab
        CHECK(rendered < 200, "icons: only visible thumbnails are rendered");
        CHECK(clip->sections() == QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                                               QStringLiteral("D"), QStringLiteral("E")}),
              "icons: letter folders become sections");
        shot(clip, out, QStringLiteral("15_clipart_set"));
        clip->setSection(1);
        settle();
        CHECK(clip->visibleCount() == 69, "icons: section filters");
        clip->setSection(6);
        settle();
        CHECK(clip->visibleCount() == 2, "icons: files outside sections");
        clip->setSection(0);
        clip->setCategory(0);
        clip->setFilter(QStringLiteral("коробка"));
        CHECK(clip->visibleCount() >= 2, "icons: russian search finds english names");
        clip->setFilter(QString());

        // a big folder: scanning is quick and only what is visible gets drawn
        QDir().mkpath(manyDir);
        for (int i = 0; i < 3000; ++i) {
            QFile f(manyDir + QStringLiteral("/many-%1.svg").arg(i, 4, 10, QLatin1Char('0')));
            if (f.open(QIODevice::WriteOnly))
                f.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><circle cx=\"12\" cy=\"12\" "
                        "r=\"8\"/></svg>");
        }
        QElapsedTimer t;
        t.start();
        clip->rescan();
        const int many = int(clip->categories().indexOf(QStringLiteral("UiTest Many"))) + 1;
        const int before2 = ClipartPanel::thumbnailsRendered();
        clip->setCategory(many);
        const qint64 ms = t.elapsed();
        settle(800);
        const int rendered2 = ClipartPanel::thumbnailsRendered() - before2;
        std::printf("3000 pictures: rescan + tab %lld ms, %d thumbnails rendered\n", static_cast<long long>(ms), rendered2);
        CHECK(clip->visibleCount() == 3000 && ms < 3000, "icons: 3000 pictures listed quickly");
        CHECK(rendered2 > 0 && rendered2 < 300, "icons: 3000 pictures, few thumbnails rendered");

        // thumbnails sit on light cards (readable in a dark theme)
        {
            const QPixmap thumb = qvariant_cast<QPixmap>(
                clip->findChild<QListView*>(QStringLiteral("clipartList"))->model()->index(0, 0).data(Qt::DecorationRole));
            const QImage ti = thumb.toImage();
            CHECK(!ti.isNull() && qGray(ti.pixel(ti.width() / 2, 3)) > 230, "theme: thumbnail on a light card");
        }

        // removing: built-in tabs cannot be removed, user folders go to the recycle bin
        const QString trash = QDir::homePath() + QStringLiteral("/.local/share/Trash");
        auto cleanTrash = [&trash](const QString& name) {
            for (const QString& sub : {QStringLiteral("/files"), QStringLiteral("/info")})
                for (const QFileInfo& fi : QDir(trash + sub).entryInfoList({name + QLatin1Char('*')},
                                                                          QDir::AllEntries | QDir::NoDotAndDotDot)) {
                    if (fi.isDir())
                        QDir(fi.absoluteFilePath()).removeRecursively();
                    else
                        QFile::remove(fi.absoluteFilePath());
                }
        };
        CHECK(!ClipartPanel::canRemoveCategory(QStringLiteral("Стрелки")) &&
                  ClipartPanel::canRemoveCategory(QStringLiteral("Test Icons")),
              "remove: only the user's folders can be removed");
        CHECK(clip->removeCategory(QStringLiteral("Test Icons"), false) && !QFileInfo::exists(setDir) &&
                  !clip->categories().contains(QStringLiteral("Test Icons")),
              "remove: tab removed from the panel");
        CHECK(!QDir(trash + QStringLiteral("/files")).entryList({QStringLiteral("Test Icons*")}, QDir::Dirs).isEmpty(),
              "remove: folder is in the recycle bin");
        cleanTrash(QStringLiteral("Test Icons"));
        // the download window removes a set too
        dlg->addSelected();
        CHECK(QFileInfo::exists(setDir), "remove: set added again");
        CHECK(dlg->removeSet(QStringLiteral("testset"), false) && !QFileInfo::exists(setDir), "remove: set removed from the download window");
        cleanTrash(QStringLiteral("Test Icons"));
        CHECK(!ClipartPanel::removeGalleryFolder(nullptr, ClipartPanel::builtinDir() + QStringLiteral("/Стрелки"), false) &&
                  QFileInfo::exists(ClipartPanel::builtinDir() + QStringLiteral("/Стрелки")),
              "remove: nothing outside the user's gallery");

        clip->setCategory(0);
        QDir(setDir).removeRecursively();
        QDir(manyDir).removeRecursively();
        qunsetenv("LABELFORGE_ICONIFY_URL");
        clip->rescan();
        settle();
    }

    // dark theme: interface icons turn light, the label stays white
    {
        const QPalette light = QApplication::palette();
        auto strokeGray = [] { return qGray(icons::get(QStringLiteral("rect")).pixmap(32, 32).toImage().pixel(5, 16)); };
        CHECK(strokeGray() < 110, "theme: dark icon lines in a light theme");
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(32, 33, 36));
        dark.setColor(QPalette::WindowText, QColor(230, 230, 230));
        dark.setColor(QPalette::Base, QColor(24, 25, 27));
        dark.setColor(QPalette::AlternateBase, QColor(40, 41, 45));
        dark.setColor(QPalette::Text, QColor(230, 230, 230));
        dark.setColor(QPalette::Button, QColor(45, 46, 50));
        dark.setColor(QPalette::ButtonText, QColor(230, 230, 230));
        dark.setColor(QPalette::Highlight, QColor(60, 120, 200));
        QApplication::setPalette(dark);
        settle();
        CHECK(strokeGray() > 160, "theme: light icon lines in a dark theme");
        shot(&w, out, QStringLiteral("16_dark_theme"));
        QApplication::setPalette(light);
        settle();
        CHECK(strokeGray() < 110, "theme: icons follow the switch back");
    }

    // whole-layout view: three labels in a row on a roll label, edited as one
    {
        LabelDocument* doc = scene->document();
        const int undoStart = undo->index();
        doc->print.layoutId = QStringLiteral("custom");
        doc->print.layoutSetup.carrier = LayoutSetup::CarrierRollLabel;
        doc->print.layoutSetup.carrierW = doc->label.width * 3 + 4 + 2;
        doc->print.layoutSetup.carrierH = doc->label.height + 2;
        doc->print.layoutSetup.fit = true;
        doc->print.layoutSetup.gapX = 2;
        auto box = std::make_unique<RectObject>();
        box->x = 2;
        box->y = 2;
        box->w = box->h = 8;
        box->fillType = shapes::FillSolid;
        box->fill = QColor(200, 30, 30);
        RectObject* boxp = box.get();
        doc->objects.push_back(std::move(box));
        QAction* lv = w.findChild<QAction*>(QStringLiteral("layoutView"));
        CHECK(lv != nullptr, "layout view action present");
        if (lv && lv->isChecked())
            lv->setChecked(false);
        scene->rebuild();
        if (lv)
            lv->setChecked(true);
        settle();
        std::printf("layout view: %d cells\n", scene->cellCount());
        CHECK(scene->layoutView() && scene->cellCount() == 3, "layout view shows three labels");
        CHECK(scene->itemsFor(boxp).size() == 3, "shared object has a copy in every label");
        CHECK(scene->contentRect().width() > doc->label.width * 3, "view shows the whole carrier");
        CHECK(scene->contextFor(2).recordNumber == scene->contextFor(1).recordNumber % 3 + 1,
              "next label shows the next record");

        // drag the copy in the second label: all copies follow
        ObjectItem* second = scene->itemsFor(boxp).value(1);
        const QPoint at = view->mapFromScene(second->mapToScene(QPointF(4, 4)));
        const QPoint to = view->mapFromScene(second->mapToScene(QPointF(9, 4)));
        mouse(vp, QEvent::MouseButtonPress, at, Qt::LeftButton);
        mouse(vp, QEvent::MouseMove, (at + to) / 2, Qt::LeftButton);
        mouse(vp, QEvent::MouseMove, to, Qt::LeftButton);
        mouse(vp, QEvent::MouseButtonRelease, to, Qt::NoButton);
        settle();
        std::printf("dragged copy: x %.2f\n", boxp->x);
        CHECK(std::abs(boxp->x - 7) < 0.6, "dragging a copy moves the object");
        bool follow = true;
        for (ObjectItem* it : scene->itemsFor(boxp))
            follow = follow && (it->pos() - it->origin() - QPointF(boxp->x, boxp->y)).manhattanLength() < 0.01;
        CHECK(follow, "every copy follows");
        CHECK(scene->selectedObjects() == QList<LabelObject*>({boxp}), "copies count as one selected object");
        shot(&w, out, QStringLiteral("17_layout_view"));

        // only in the second label
        emit scene->cellRequested(boxp, 2);
        settle();
        CHECK(boxp->cell == 2 && scene->itemsFor(boxp).size() == 1 && scene->itemsFor(boxp).value(0)->cell() == 2,
              "object kept only in the second label");
        auto* panel = w.findChild<PropertyPanel*>();
        auto* where = panel ? panel->findChild<QComboBox*>(QStringLiteral("objectCell")) : nullptr;
        CHECK(where && where->currentData().toInt() == 2, "panel shows where the object is shown");
        shot(&w, out, QStringLiteral("18_layout_cell"));
        scene->clearSelection();
        settle();
        auto* summary = panel ? panel->findChild<QLabel*>(QStringLiteral("layoutSummary")) : nullptr;
        std::printf("layout summary: %s\n", summary ? qPrintable(summary->text()) : "-");
        CHECK(summary && summary->text().contains(QStringLiteral("3 в ряд")), "panel sums up the layout");
        shot(panel, out, QStringLiteral("19_layout_props"));

        while (undo->index() > undoStart)
            undo->undo();
        if (lv)
            lv->setChecked(false);
        settle();
        CHECK(scene->cellCount() == 1 && scene->document()->print.layoutId != QLatin1String("custom"),
              "undo and the switch bring back one label");
    }

    // shape tool: pick a kind from the menu, click on the label, edit the effects
    {
        const int n = int(scene->document()->objects.size());
        auto* arrow = w.findChild<QAction*>(QStringLiteral("shape-arrow"));
        CHECK(arrow != nullptr, "shapes menu has the arrow");
        if (arrow)
            arrow->trigger();
        CHECK(scene->tool() == LabelScene::ToolShape, "shape kind selects the shape tool");
        const QPoint at = view->mapFromScene(QPointF(4, 24));
        mouse(vp, QEvent::MouseButtonPress, at, Qt::LeftButton);
        mouse(vp, QEvent::MouseButtonRelease, at, Qt::NoButton);
        settle();
        CHECK(int(scene->document()->objects.size()) == n + 1, "shape tool inserts an object");
        auto* sh = dynamic_cast<PolyShapeObject*>(scene->document()->objects.back().get());
        CHECK(sh && sh->kind == shapes::Arrow && sh->w > sh->h, "inserted shape is a wide arrow");
        if (sh) {
            sh->fillType = shapes::FillLinear;
            sh->fill = QColor(30, 90, 200);
            sh->fill2 = QColor(255, 230, 120);
            sh->frame = shapes::FrameDouble;
            sh->strokeWidth = 0.5;
            sh->shadow = true;
            sh->shadowBlur = 0.8;
            scene->rebuild();
            scene->clearSelection();
            scene->selectObjects({sh});
            settle();
            shot(&w, out, QStringLiteral("09_shape"));
            auto* panel = w.findChild<PropertyPanel*>();
            shot(panel, out, QStringLiteral("10_shape_props"));
            panel->verticalScrollBar()->setValue(panel->verticalScrollBar()->maximum() / 2);
            shot(panel, out, QStringLiteral("11_shape_props_middle"));

            // bind the fill colour to a data field through the "{}" button
            auto* bind = panel->findChild<QToolButton*>(QStringLiteral("colorField-fill"));
            CHECK(bind && bind->menu(), "fill colour has a field binding button");
            if (bind && bind->menu()) {
                emit bind->menu()->aboutToShow();
                QAction* field = nullptr;
                for (QAction* a : bind->menu()->actions())
                    if (a->text().startsWith(QLatin1Char('{')) && !field)
                        field = a;
                CHECK(field != nullptr, "binding menu lists the data fields");
                if (field) {
                    const QString t = field->text();
                    const int idx = undo->index();
                    field->trigger();
                    settle();
                    CHECK(sh->colorFields.value(QStringLiteral("fill")) == t && bind->statusTip() == t,
                          "fill colour bound to a field");
                    CHECK(undo->index() == idx + 1, "binding is one undo step");
                    shot(panel, out, QStringLiteral("12_color_field"));
                    undo->undo();
                    settle();
                    auto* back = scene->document()->objects.back().get();
                    CHECK(back->colorFields.isEmpty(), "undo removes the binding");
                }
            }
        }
        undo->undo();
        settle();
        CHECK(int(scene->document()->objects.size()) == n, "undo removes the shape");
    }

    // dialogs
    DataTable data;
    loadData(tdoc.data, QString(), &data, &err);
    {
        PrintDialog dlg(tdoc, data, out.absolutePath(), &w);
        dlg.show();
        shot(&dlg, out, QStringLiteral("10_print_roll"));

        // the printer list sits in "Вывод" for every output that goes to a printer
        auto* output = dlg.findChild<QComboBox*>(QStringLiteral("output"));
        auto* printer = dlg.findChild<QComboBox*>(QStringLiteral("printer"));
        CHECK(output && printer, "print dialog has an output and a printer list");
        if (output && printer) {
            CHECK(printer->count() == QPrinterInfo::availablePrinterNames().size(), "printer list shows every printer");
            output->setCurrentIndex(output->findData(1));  // PDF
            settle();
            CHECK(!printer->isVisible(), "no printer list for PDF");
            output->setCurrentIndex(output->findData(2));  // thermal, direct
            settle();
            CHECK(printer->isVisible(), "printer list shown for the thermal printer");
            output->setCurrentIndex(output->findData(0));  // driver
            settle();
            CHECK(printer->isVisible(), "printer list shown for the driver output");
            auto* setup = dlg.findChild<QToolButton*>(QStringLiteral("printerSetup"));
            CHECK(setup && setup->isVisible(), "driver settings button shown");
            // every layout remembers its own printer
            if (printer->count() >= 2) {
                auto* layout = dlg.findChild<QComboBox*>(QStringLiteral("layout"));
                const int rollIdx = layout->findData(QStringLiteral("roll"));
                const int a4Idx = layout->findData(QStringLiteral("a4-auto"));
                if (rollIdx >= 0 && a4Idx >= 0) {
                    layout->setCurrentIndex(rollIdx);
                    printer->setCurrentIndex(0);
                    layout->setCurrentIndex(a4Idx);
                    printer->setCurrentIndex(1);
                    layout->setCurrentIndex(rollIdx);
                    CHECK(printer->currentIndex() == 0, "roll layout brings back its printer");
                    layout->setCurrentIndex(a4Idx);
                    CHECK(printer->currentIndex() == 1, "A4 layout brings back its printer");
                }
            }
        }
        CHECK(!dlg.settings().toJson().contains(QStringLiteral("printer")), "printer is not saved in the label file");
    }
    {
        LabelDocument a4;
        TemplateInfo t;
        for (const TemplateInfo& ti : TemplateLibrary::builtins())
            if (ti.id == QLatin1String("price-a4-21"))
                t = ti;
        TemplateLibrary::load(t, a4, &err);
        DataTable d2;
        loadData(a4.data, QString(), &d2, &err);
        a4.print.copies = 5;
        a4.print.startPosition = 4;
        PrintDialog dlg(a4, d2, out.absolutePath(), &w);
        dlg.show();
        shot(&dlg, out, QStringLiteral("11_print_a4"));
    }
    {
        NewLabelDialog dlg(NewLabelDialog::CreateMode, &w);
        dlg.show();
        shot(&dlg, out, QStringLiteral("12_new_label"));
    }
    {
        SheetLayoutDialog dlg(QStringLiteral("avery-l7160"), &w);
        dlg.show();
        shot(&dlg, out, QStringLiteral("13_sheets"));
    }
    {
        DataSourceDialog dlg(tdoc.data, data, out.absolutePath(), &w);
        dlg.show();
        shot(&dlg, out, QStringLiteral("14_datasource"));
    }

    std::printf("%s\n", failures ? "UITEST FAILED" : "UITEST OK");
    return failures ? 1 : 0;
}
