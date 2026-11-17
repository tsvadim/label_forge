// SPDX-License-Identifier: GPL-3.0-or-later
// Self-test: exercises variables, data loaders, barcodes, templates and exports.
// Usage: labelforge_selftest <output-dir> [test.xlsx]
#include "barcode/Barcode.h"
#include "core/ImageEffects.h"
#include "core/LabelDocument.h"
#include "core/Renderer.h"
#include "data/DataLoader.h"
#include "print/PrintJob.h"
#include "print/ThermalExport.h"
#include "templates/TemplateLibrary.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSvgRenderer>
#include <QTextStream>
#include <cstdio>

using namespace lf;

static int failures = 0;
#define CHECK(cond, msg)                                                         \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        }                                                                        \
    } while (0)

static void testVariables()
{
    const QStringList cols{QStringLiteral("Name"), QStringLiteral("Price")};
    const QStringList vals{QStringLiteral("Молоко"), QStringLiteral("42,5")};
    RenderContext ctx;
    ctx.columns = &cols;
    ctx.values = &vals;
    ctx.sequence = 7;
    ctx.now = QDateTime(QDate(2026, 10, 22), QTime(12, 30));
    CHECK(expandTemplate(QStringLiteral("{name}: {Price:0.00}"), ctx) == QStringLiteral("Молоко: 42.50"), "field+format");
    CHECK(expandTemplate(QStringLiteral("{seq:0001}"), ctx) == QStringLiteral("0007"), "seq padded");
    CHECK(expandTemplate(QStringLiteral("{seq:100:10}"), ctx) == QStringLiteral("160"), "seq step");
    CHECK(expandTemplate(QStringLiteral("{date+10:dd.MM.yyyy}"), ctx) == QStringLiteral("01.11.2026"), "date offset");
    CHECK(expandTemplate(QStringLiteral("{{x}} {Unknown}"), ctx) == QStringLiteral("{x} {Unknown}"), "escapes/unknown");
    CHECK(expandTemplate(QStringLiteral("{Name:upper}"), ctx) == QStringLiteral("МОЛОКО"), "upper");
}

static void testCsv()
{
    // "Имя;Цена\r\nМолоко;12\r\n" in Windows-1251
    const QByteArray cp = QByteArray::fromHex("c8ecff3bd6e5ede00d0acceeebeeeaee3b31320d0a");
    DataTable t;
    QString err;
    CHECK(parseCsv(decodeText(cp, QStringLiteral("auto")), QStringLiteral("auto"), true, &t, &err), "csv parse");
    CHECK(t.columns == QStringList({QStringLiteral("Имя"), QStringLiteral("Цена")}), "csv cp1251 header");
    CHECK(t.rowCount() == 1 && t.rows[0][0] == QStringLiteral("Молоко"), "csv cp1251 row");
    const QString q = QStringLiteral("a,b\n\"x, \"\"y\"\"\",2\n\n3,4\n");
    CHECK(parseCsv(q, QStringLiteral("auto"), true, &t, &err), "csv quoted");
    CHECK(t.rowCount() == 2 && t.rows[0][0] == QStringLiteral("x, \"y\""), "csv quotes");
    CHECK(PrintJob::parseRange(QStringLiteral("1-3, 5, 9-"), 10) == QVector<int>({0, 1, 2, 4, 8, 9}), "range");
}

static void testImageEffects()
{
    QImage grey(64, 64, QImage::Format_ARGB32);
    grey.fill(qRgb(128, 128, 128));
    const QImage d = ditherToMono(grey);
    int black = 0, other = 0;
    for (int y = 0; y < d.height(); ++y)
        for (int x = 0; x < d.width(); ++x) {
            const QRgb px = d.pixel(x, y);
            if (px == 0xFF000000u)
                ++black;
            else if (px != 0xFFFFFFFFu)
                ++other;
        }
    CHECK(other == 0, "dither: only black and white");
    CHECK(black > 64 * 64 * 2 / 5 && black < 64 * 64 * 3 / 5, "dither: 50% grey ~ half black");
    QImage clear(8, 8, QImage::Format_ARGB32);
    clear.fill(Qt::transparent);
    CHECK(ditherToMono(clear).pixel(3, 3) == 0u, "dither: transparent stays transparent");

    QImage bw(2, 1, QImage::Format_ARGB32);
    bw.setPixel(0, 0, qRgb(0, 0, 0));
    bw.setPixel(1, 0, qRgb(255, 255, 255));
    ImageAdjust tint;
    tint.tint = QColor(200, 0, 0);
    const QImage t = applyAdjust(bw, tint);
    CHECK(t.pixel(0, 0) == qRgba(200, 0, 0, 255) && qAlpha(t.pixel(1, 0)) == 0, "tint: dark -> colour, light -> transparent");
    ImageAdjust bright;
    bright.brightness = 100;
    CHECK(applyAdjust(grey, bright).pixel(0, 0) == qRgb(255, 255, 255), "brightness +100");

    const QByteArray svg =
        "<?xml version=\"1.0\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewBox=\"0 0 10 10\">"
        "<style>.a{fill:#00ff00;stroke:none}</style>"
        "<rect class=\"a\" width=\"5\" height=\"5\"/>"
        "<rect x=\"5\" width=\"5\" height=\"5\" fill=\"#123456\" style=\"stroke:#654321;stroke-width:1\"/>"
        "<circle cx=\"5\" cy=\"8\" r=\"1\" fill=\"#ffffff\"/><path d=\"M0 0L1 1\" fill=\"none\"/></svg>";
    const QByteArray rc = recolorSvg(svg, QColor(0xcc, 0x00, 0x11));
    CHECK(!rc.contains("#00ff00") && !rc.contains("#123456") && !rc.contains("#654321"), "svg recolor: colours replaced");
    CHECK(rc.contains("#cc0011") && rc.contains("#ffffff") && rc.contains("fill=\"none\""), "svg recolor: none/white kept");
    CHECK(rc.contains("xmlns:xlink"), "svg recolor: namespaces kept");
    CHECK(QSvgRenderer(rc).isValid(), "svg recolor: still valid");
    const QByteArray gz = QByteArray::fromHex("1f8b0800");
    CHECK(recolorSvg(gz, Qt::red) == gz && isSvgData(gz) && isSvgData(svg) && !isSvgData("\x89PNG"), "svg detection");
}

static double blackShare(const QImage& mono)
{
    int black = 0;
    for (int y = 0; y < mono.height(); ++y)
        for (int x = 0; x < mono.width(); ++x)
            black += mono.pixelIndex(x, y) == 1;
    return double(black) / (mono.width() * mono.height());
}

static void testImageObject(const QDir& out)
{
    // JSON: all new fields survive a round trip, old files keep their meaning
    ImageObject im;
    CHECK(im.dither, "image: new objects dither by default");
    im.setEmbeddedSvg("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"/>");
    im.fit = ImageObject::FitNone;
    im.hAlign = 2;
    im.vAlign = 0;
    im.cropLeft = 10;
    im.cropBottom = 5.5;
    im.flipH = true;
    im.opacity = 0.5;
    im.brightness = -20;
    im.contrast = 30;
    im.recolor = true;
    im.tintColor = QColor(10, 20, 30);
    auto copy = LabelObject::fromJsonAny(im.toJson());
    auto* ic = dynamic_cast<ImageObject*>(copy.get());
    CHECK(ic && ic->svgData == im.svgData && ic->fit == ImageObject::FitNone && ic->hAlign == 2 && ic->vAlign == 0 &&
              ic->cropLeft == 10 && ic->cropBottom == 5.5 && ic->flipH && !ic->flipV && ic->opacity == 0.5 &&
              ic->brightness == -20 && ic->contrast == 30 && ic->recolor && ic->tintColor == QColor(10, 20, 30) &&
              ic->dither,
          "image: json round trip");
    QJsonObject old;
    old[QStringLiteral("type")] = QStringLiteral("image");
    old[QStringLiteral("keepAspect")] = false;
    auto oldObj = LabelObject::fromJsonAny(old);
    auto* io = dynamic_cast<ImageObject*>(oldObj.get());
    CHECK(io && io->fit == ImageObject::FitStretch && !io->dither && io->hAlign == 1, "image: old file compatibility");

    // SVG grey square: threshold prints solid black, dithering prints a halftone
    const QByteArray greySvg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\">"
                               "<rect width=\"20\" height=\"20\" fill=\"#777777\"/></svg>";
    LabelDocument doc;
    doc.label.width = 20;
    doc.label.height = 20;
    auto img = std::make_unique<ImageObject>();
    img->setEmbeddedSvg(greySvg);
    img->w = 20;
    img->h = 20;
    img->fit = ImageObject::FitStretch;
    ImageObject* ip = img.get();
    doc.objects.push_back(std::move(img));
    RenderContext ctx;
    const double dithered = blackShare(renderLabelImage(doc, ctx, 203, true));
    ip->dither = false;
    const double threshold = blackShare(renderLabelImage(doc, ctx, 203, true));
    CHECK(dithered > 0.3 && dithered < 0.7, "image: dithered svg is a halftone");
    CHECK(threshold > 0.95, "image: threshold svg is solid");
    QString err;
    const DataTable noData;
    PrintJob job(doc, noData, doc.print);
    CHECK(job.exportPdf(out.filePath(QStringLiteral("image_svg.pdf")), &err), "image: svg to pdf");

    // recolour + flip + crop on screen rendering
    ip->recolor = true;
    ip->tintColor = QColor(255, 0, 0);
    QImage c = renderLabelImage(doc, ctx, 100, false);
    CHECK(c.pixelColor(c.width() / 2, c.height() / 2) == QColor(255, 0, 0), "image: svg recoloured");
    QImage halves(20, 2, QImage::Format_ARGB32);  // left half black, right half white
    halves.fill(Qt::white);
    for (int x = 0; x < 10; ++x)
        for (int y = 0; y < 2; ++y)
            halves.setPixel(x, y, qRgb(0, 0, 0));
    ip->setEmbedded(halves);
    ip->recolor = false;
    ip->flipH = true;
    c = renderLabelImage(doc, ctx, 100, false);
    CHECK(qGray(c.pixel(5, 40)) > 200 && qGray(c.pixel(c.width() - 5, 40)) < 50, "image: flipped");
    ip->flipH = false;
    ip->cropRight = 50;
    c = renderLabelImage(doc, ctx, 100, false);
    CHECK(qGray(c.pixel(c.width() - 5, 40)) < 50, "image: cropped to the black half");
    c.save(out.filePath(QStringLiteral("image_crop.png")));
}

static void testXlsx(const QString& path, QTextStream& log)
{
    QString err;
    const QStringList sheets = xlsxSheetNames(path, &err);
    log << "xlsx sheets: " << sheets.join(QLatin1Char('|')) << " " << err << "\n";
    CHECK(sheets.size() == 2, "xlsx sheet count");
    DataTable t;
    CHECK(readXlsx(path, sheets.value(1), true, &t, &err), "xlsx read");
    log << "xlsx columns: " << t.columns.join(QLatin1Char('|')) << "\n";
    for (const QStringList& r : t.rows)
        log << "xlsx row: " << r.join(QLatin1Char('|')) << "\n";
    CHECK(t.columns.value(0) == QStringLiteral("Товар"), "xlsx header");
    CHECK(t.rowCount() == 3, "xlsx rows");
    CHECK(t.rows.value(0).value(1) == QStringLiteral("42.5"), "xlsx number");
    CHECK(t.rows.value(0).value(2) == QStringLiteral("31.12.2026"), "xlsx date");
    CHECK(t.rows.value(2).value(0) == QStringLiteral("Ряд 3 без цены"), "xlsx string");
}

static void renderBarcodes(const QDir& out, QTextStream& manifest)
{
    struct Case {
        const char* sym;
        QString data;
        QString expect;
    };
    const QVector<Case> cases = {
        {"code128", QStringLiteral("Hello-128 abc"), QStringLiteral("Hello-128 abc")},
        {"code128", QStringLiteral("1234567890123456"), QStringLiteral("1234567890123456")},
        {"code128", QStringLiteral("AB12345678cd9"), QStringLiteral("AB12345678cd9")},
        {"gs1-128", QStringLiteral("(01)04601234567893(10)AB12(17)271231"),
         QStringLiteral("(01)04601234567893(10)AB12(17)271231")},
        {"ean13", QStringLiteral("482012345678"), QStringLiteral("4820123456784")},
        {"ean8", QStringLiteral("9638507"), QStringLiteral("96385074")},
        {"upca", QStringLiteral("03600029145"), QStringLiteral("0036000291452")},
        {"code39", QStringLiteral("CODE-39 TEST"), QStringLiteral("CODE-39 TEST")},
        {"itf", QStringLiteral("12345678"), QStringLiteral("12345678")},
        {"itf14", QStringLiteral("1540014128876"), QStringLiteral("15400141288763")},
        {"qr", QStringLiteral("Привет, мир! https://example.org"), QStringLiteral("Привет, мир! https://example.org")},
        {"datamatrix", QStringLiteral("DM-0001"), QStringLiteral("DM-0001")},
        {"datamatrix", QStringLiteral("Data Matrix: тест 1234567890"), QStringLiteral("Data Matrix: тест 1234567890")},
        {"datamatrix", QString(300, QLatin1Char('x')), QString(300, QLatin1Char('x'))},
        {"gs1-datamatrix", QStringLiteral("(01)04601234567893(21)abc123(91)XYZ"),
         QStringLiteral("(01)04601234567893(21)abc123(91)XYZ")},
    };
    int n = 0;
    for (const Case& c : cases) {
        LabelDocument doc;
        doc.label.width = 80;
        doc.label.height = barcode::is2D(QLatin1String(c.sym)) ? 80 : 40;
        auto b = std::make_unique<BarcodeObject>();
        b->symbology = QLatin1String(c.sym);
        b->data = c.data;
        b->x = 4;
        b->y = 4;
        b->w = 72;
        b->h = doc.label.height - 8;
        doc.objects.push_back(std::move(b));
        RenderContext ctx;
        const QImage img = renderLabelImage(doc, ctx, 300, false);
        const QString file = QStringLiteral("bc_%1_%2.png").arg(n++, 2, 10, QLatin1Char('0')).arg(QLatin1String(c.sym));
        img.save(out.filePath(file));
        const barcode::Result r = barcode::encode(QLatin1String(c.sym), c.data);
        CHECK(r.ok, c.sym);
        manifest << file << "\t" << QString::fromLatin1(c.expect.toUtf8().toHex()) << "\n";
    }
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LabelForgeSelfTest"));
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("selftest-out");
    QDir().mkpath(outDir);
    QDir out(outDir);
    QFile logFile(out.filePath(QStringLiteral("log.txt")));
    logFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QTextStream log(&logFile);

    testVariables();
    testCsv();
    testImageEffects();
    if (argc > 2)
        testXlsx(QString::fromLocal8Bit(argv[2]), log);
    testImageObject(out);

    QFile mf(out.filePath(QStringLiteral("barcodes.tsv")));
    mf.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QTextStream manifest(&mf);
    renderBarcodes(out, manifest);
    manifest.flush();

    // built-in templates: preview PNG, PDF, ZPL, TSPL
    for (const TemplateInfo& ti : TemplateLibrary::builtins()) {
        LabelDocument doc;
        QString err;
        CHECK(TemplateLibrary::load(ti, doc, &err), "template load");
        // round-trip through JSON
        LabelDocument copy;
        CHECK(copy.fromBytes(doc.toBytes(), &err), "json roundtrip");
        CHECK(copy.toBytes() == doc.toBytes(), "json stable");
        doc.save(out.filePath(ti.id + QStringLiteral(".lfl")));

        DataTable data;
        CHECK(loadData(doc.data, QString(), &data, &err), "inline data");
        PrintSettings ps = doc.print;
        ps.copies = 2;
        PrintJob job(doc, data, ps);
        RenderContext ctx = job.contextFor(job.instances().value(0));
        renderLabelImage(doc, ctx, 300, false).save(out.filePath(ti.id + QStringLiteral(".png")));
        CHECK(job.exportPdf(out.filePath(ti.id + QStringLiteral(".pdf")), &err), "pdf export");
        log << ti.id << ": labels=" << job.labelCount() << " pages=" << job.pageCount() << " layout="
            << job.layout().id << "\n";

        ps.thermalLang = QStringLiteral("zpl");
        QByteArray zpl = buildThermalJob(job, doc, ps);
        QFile zf(out.filePath(ti.id + QStringLiteral(".zpl")));
        zf.open(QIODevice::WriteOnly);
        zf.write(zpl);
        ps.thermalLang = QStringLiteral("tspl");
        QFile tf(out.filePath(ti.id + QStringLiteral(".tspl")));
        tf.open(QIODevice::WriteOnly);
        tf.write(buildThermalJob(job, doc, ps));
        CHECK(zpl.startsWith("^XA"), "zpl header");
    }

    // several small labels on one 100x70 roll label, printed directly (ZPL)
    {
        LabelDocument doc;
        doc.label.width = 30;
        doc.label.height = 20;
        auto t = std::make_unique<TextObject>();
        t->x = 1;
        t->y = 1;
        t->w = 28;
        t->h = 6;
        t->text = QStringLiteral("{Имя}");
        doc.objects.push_back(std::move(t));
        auto b = std::make_unique<BarcodeObject>();
        b->x = 2;
        b->y = 7;
        b->w = 26;
        b->h = 12;
        b->data = QStringLiteral("{Код}");
        doc.objects.push_back(std::move(b));
        doc.data.kind = DataKind::Inline;
        doc.data.table.columns = {QStringLiteral("Имя"), QStringLiteral("Код")};
        for (int i = 1; i <= 12; ++i)
            doc.data.table.rows << QStringList{QStringLiteral("Деталь %1").arg(i), QStringLiteral("P-%1").arg(i, 4, 10, QLatin1Char('0'))};
        doc.print.layoutId = QStringLiteral("roll-100x70-auto");
        DataTable data;
        QString err;
        loadData(doc.data, QString(), &data, &err);
        PrintJob job(doc, data, doc.print);
        log << "multi-up 100x70: per page " << job.layout().perPage() << " (" << job.layout().cols << "x"
            << job.layout().rows << "), pages " << job.pageCount() << "\n";
        CHECK(!job.layout().roll && job.layout().perPage() == 9, "30x20 fits 9 times on 100x70");
        CHECK(job.pageCount() == 2, "12 labels -> 2 pages");
        PrintSettings ps = doc.print;
        const QByteArray zpl = buildThermalJob(job, doc, ps);
        CHECK(zpl.count("^XA") == 2 && zpl.contains("^PW799"), "ZPL page per 100x70 label");
        QFile f(out.filePath(QStringLiteral("multiup.zpl")));
        f.open(QIODevice::WriteOnly);
        f.write(zpl);
        CHECK(job.exportPdf(out.filePath(QStringLiteral("multiup.pdf")), &err), "multi-up pdf");
    }

    log.flush();
    std::printf("%s\n", failures ? "SELFTEST FAILED" : "SELFTEST OK");
    return failures ? 1 : 0;
}
