// SPDX-License-Identifier: GPL-3.0-or-later
// UI smoke test: drives the main window and dialogs offscreen and saves screenshots.
// Usage: QT_QPA_PLATFORM=offscreen labelforge_uitest <output-dir>
#include "core/LabelDocument.h"
#include "data/DataLoader.h"
#include "templates/TemplateLibrary.h"
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
#include "ui/SheetLayoutDialog.h"

#include <QApplication>
#include <QDir>
#include <QMouseEvent>
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

    // dialogs
    DataTable data;
    loadData(tdoc.data, QString(), &data, &err);
    {
        PrintDialog dlg(tdoc, data, out.absolutePath(), &w);
        dlg.show();
        shot(&dlg, out, QStringLiteral("10_print_roll"));
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
