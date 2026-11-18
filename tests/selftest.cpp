// SPDX-License-Identifier: GPL-3.0-or-later
// Self-test: exercises variables, data loaders, barcodes, templates and exports.
// Usage: labelforge_selftest <output-dir> [test.xlsx]
#include "barcode/Barcode.h"
#include "core/IconWords.h"
#include "core/Iconify.h"
#include "core/ImageEffects.h"
#include "core/LabelDocument.h"
#include "core/RemoteImage.h"
#include "core/Renderer.h"
#include "core/ShapePaths.h"
#include "data/DataLoader.h"
#include "print/PrintJob.h"
#include "print/ThermalExport.h"
#include "templates/TemplateLibrary.h"
#include "TinyHttp.h"

#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
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

static bool dark(const QImage& img, double xmm, double ymm, double pxPerMm = 10)
{
    return qGray(img.pixel(int(xmm * pxPerMm), int(ymm * pxPerMm))) < 100;
}

static bool light(const QImage& img, double xmm, double ymm, double pxPerMm = 10)
{
    return qGray(img.pixel(int(xmm * pxPerMm), int(ymm * pxPerMm))) > 200;
}

// Colours from data fields, data: URI and URL images, BLOB helpers.
static void testDataBinding(const QDir& out)
{
    // colour parsing
    CHECK(LabelObject::parseColor(QStringLiteral("#ff0000"), Qt::black) == QColor(255, 0, 0), "color: #RRGGBB");
    CHECK(LabelObject::parseColor(QStringLiteral(" 0f0 "), Qt::black) == QColor(0, 255, 0), "color: bare #RGB");
    CHECK(LabelObject::parseColor(QStringLiteral("#800000ff"), Qt::black) == QColor(0, 0, 255, 128), "color: #AARRGGBB");
    CHECK(LabelObject::parseColor(QStringLiteral("navy"), Qt::black) == QColor(0, 0, 128), "color: svg name");
    CHECK(LabelObject::parseColor(QStringLiteral("красный"), Qt::red) == QColor(Qt::red), "color: invalid -> own");
    CHECK(LabelObject::parseColor(QString(), Qt::blue) == QColor(Qt::blue), "color: empty -> own");

    // JSON: bindings survive, unbound objects stay as before
    {
        RectObject r;
        r.colorFields.insert(QStringLiteral("fill"), QStringLiteral("{Цвет}"));
        auto c = LabelObject::fromJsonAny(r.toJson());
        CHECK(c && c->colorFields.value(QStringLiteral("fill")) == QStringLiteral("{Цвет}") && c->toJson() == r.toJson(),
              "color: binding json round trip");
        CHECK(!RectObject().toJson().contains(QStringLiteral("colorFields")), "color: no key without bindings");
    }

    // rendering with a record: rect fill and line colour (254 dpi = 10 px/mm)
    const QStringList cols = {QStringLiteral("Цвет"), QStringLiteral("Фото")};
    QStringList vals = {QStringLiteral("#00c000"), QString()};
    RenderContext ctx;
    ctx.columns = &cols;
    ctx.values = &vals;
    LabelDocument doc;
    doc.label.width = 30;
    doc.label.height = 20;
    {
        auto r = std::make_unique<RectObject>();
        r->x = 0, r->y = 0, r->w = 10, r->h = 10;
        r->strokeWidth = 0;
        r->fillType = shapes::FillSolid;
        r->fill = Qt::black;
        r->colorFields.insert(QStringLiteral("fill"), QStringLiteral("{Цвет}"));
        doc.objects.push_back(std::move(r));
        auto l = std::make_unique<LineObject>();
        l->x = 0, l->y = 12, l->w = 10, l->h = 4;
        l->width = 2;
        l->colorFields.insert(QStringLiteral("color"), QStringLiteral("{Цвет}"));
        doc.objects.push_back(std::move(l));
    }
    auto pix = [](const QImage& img, double xmm, double ymm) {
        return QColor(img.pixelColor(int(xmm * 10), int(ymm * 10)).rgb());
    };
    QImage img = renderLabelImage(doc, ctx, 254, false);
    CHECK(pix(img, 5, 5) == QColor(0, 192, 0), "color: rect fill from field");
    CHECK(pix(img, 5, 14) == QColor(0, 192, 0), "color: line colour from field");
    vals[0] = QStringLiteral("не цвет");
    img = renderLabelImage(doc, ctx, 254, false);
    CHECK(pix(img, 5, 5) == QColor(Qt::black) && pix(img, 5, 14) == QColor(Qt::black),
          "color: invalid value -> own colour");
    vals[0] = QStringLiteral("#00c000");
    RenderContext design = ctx;
    design.rawTemplates = true;
    CHECK(pix(renderLabelImage(doc, design, 254, false), 5, 5) == QColor(Qt::black),
          "color: design view shows own colour");

    // data: URI images (BLOB fields)
    QImage red(20, 20, QImage::Format_RGB32);
    red.fill(QColor(255, 0, 0));
    QByteArray png;
    {
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        red.save(&b, "PNG");
    }
    const QString uri = remote::makeDataUri(png);
    CHECK(uri.startsWith(QStringLiteral("data:image/png;base64,")), "blob: png data uri");
    CHECK(remote::decodeDataUri(uri) == png && remote::dataUriMime(uri) == QStringLiteral("image/png"), "blob: decode");
    CHECK(std::abs(remote::dataUriSize(uri) - png.size()) <= 2, "blob: size estimate");
    CHECK(remote::sniffMime(QByteArray("\xFF\xD8\xFF\xE0", 4)) == QStringLiteral("image/jpeg") &&
              remote::sniffMime(QByteArray("<svg xmlns='x'/>")) == QStringLiteral("image/svg+xml") &&
              remote::sniffMime(QByteArray("abc")) == QStringLiteral("application/octet-stream"),
          "blob: mime sniffing");
    CHECK(remote::decodeDataUri(QStringLiteral("data:,a%20b")) == QByteArray("a b"), "blob: percent-encoded uri");

    LabelDocument idoc;
    idoc.label.width = 30;
    idoc.label.height = 10;
    auto im = std::make_unique<ImageObject>();
    ImageObject* imp = im.get();
    im->x = 0, im->y = 0, im->w = 10, im->h = 10;
    im->fit = ImageObject::FitStretch;
    im->dither = false;
    im->pathTemplate = QStringLiteral("{Фото}");
    idoc.objects.push_back(std::move(im));
    vals[1] = uri;
    CHECK(pix(renderLabelImage(idoc, ctx, 254, false), 5, 5) == QColor(255, 0, 0), "blob: image from data uri field");
    vals[1] = remote::makeDataUri(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'><rect width='10' height='10' fill='#000'/></svg>");
    imp->recolor = true;
    imp->colorFields.insert(QStringLiteral("tint"), QStringLiteral("{Цвет}"));
    CHECK(pix(renderLabelImage(idoc, ctx, 254, false), 5, 5) == QColor(0, 192, 0), "blob: svg data uri, tint from field");
    imp->recolor = false;
    imp->colorFields.clear();

    // URL images from a local HTTP server
    remote::clearCache();
    {
        TinyHttp http([png](const QByteArray& path) {
            if (path == "/red.png")
                return TinyHttp::response(200, "image/png", png);
            if (path == "/moved")
                return TinyHttp::response(302, QByteArray(), QByteArray(), "Location: /red.png\r\n");
            return TinyHttp::response(404, QByteArray(), QByteArray());
        });
        vals[1] = http.url("/red.png");
        CHECK(pix(renderLabelImage(idoc, ctx, 254, false), 5, 5) == QColor(255, 0, 0), "url: image downloaded");
        vals[1] = http.url("/moved");
        CHECK(pix(renderLabelImage(idoc, ctx, 254, false), 5, 5) == QColor(255, 0, 0), "url: redirect followed");
        vals[1] = http.url("/missing.png");
        CHECK(pix(renderLabelImage(idoc, ctx, 254, false), 5, 5) == QColor(Qt::white), "url: 404 draws nothing");
        const int before = http.requests();
        QString err;
        CHECK(remote::fetchUrl(http.url("/missing.png"), &err).isEmpty() && !err.isEmpty() && http.requests() == before,
              "url: failure is cached");
        QElapsedTimer t;
        t.start();
        CHECK(remote::fetchUrl(QStringLiteral("http://127.0.0.1:1/x.png"), &err).isEmpty() && !err.isEmpty() &&
                  t.elapsed() < 9000,
              "url: refused connection fails fast");
        // a whole print job with the URL image
        DataTable data;
        data.columns = cols;
        data.rows = {QStringList{QStringLiteral("#00c000"), http.url("/red.png")}};
        PrintJob job(idoc, data, idoc.print);
        CHECK(job.exportPdf(out.filePath(QStringLiteral("url_image.pdf")), &err), "url: pdf export");
    }
}

// Iconify response parsing, SVG building and the Russian word list.
static void testIconify(const QDir& out)
{
    using namespace iconify;
    CHECK(isFreeLicense(QStringLiteral("MIT")) && isFreeLicense(QStringLiteral("CC0-1.0")) &&
              !isFreeLicense(QStringLiteral("CC-BY-4.0")) && !isFreeLicense(QStringLiteral("CC-BY-NC-4.0")) &&
              !isFreeLicense(QString()),
          "iconify: license filter");

    const QByteArray cols = R"({
        "tabler": {"name": "Tabler Icons", "total": 6545, "author": {"name": "Pawel Kuna", "url": "https://x"},
                   "license": {"title": "MIT", "spdx": "MIT", "url": "https://l"}, "category": "UI 24px"},
        "old": {"name": "Old", "total": 3, "hidden": true, "license": {"spdx": "MIT"}},
        "art": {"name": "Art", "total": 9, "palette": true, "license": {"title": "CC BY 4.0", "spdx": "CC-BY-4.0"}}
    })";
    const QList<Collection> cl = parseCollections(cols);
    CHECK(cl.size() == 2 && cl[0].prefix == QStringLiteral("art") && cl[1].name == QStringLiteral("Tabler Icons") &&
              cl[1].total == 6545 && cl[1].author == QStringLiteral("Pawel Kuna") && cl[1].licenseSpdx == QStringLiteral("MIT") &&
              cl[0].palette,
          "iconify: collections parsed, hidden skipped, sorted");
    CHECK(licenseNote(cl[1]).contains(QStringLiteral("MIT")) && licenseNote(cl[1]).contains(QStringLiteral("tabler")),
          "iconify: license note");

    const QByteArray search = R"({"icons": ["tabler:box", "art:box", "tabler:box-off", "bad"], "total": 4,
        "collections": {"tabler": {"name": "Tabler Icons", "license": {"spdx": "MIT"}},
                        "art": {"name": "Art", "license": {"spdx": "CC-BY-4.0"}}}})";
    const SearchResult sr = parseSearch(search);
    CHECK(sr.icons == QStringList({QStringLiteral("tabler:box"), QStringLiteral("tabler:box-off")}) && sr.total == 4 &&
              sr.collections.size() == 2,
          "iconify: search drops non-free sets");

    // categories, hidden icons and style variants
    const QByteArray withCats = R"({"prefix": "m", "categories": {"Arrows": ["up", "up-rounded", "down"],
        "Home/Garden": ["home", "home-sharp", "tree"]}, "uncategorized": ["misc"], "hidden": ["tree"],
        "suffixes": {"": "Regular", "rounded": "Rounded", "sharp": "Sharp"}})";
    const QMap<QString, QStringList> f1 = parseCollectionFolders(withCats);
    CHECK(f1.value(QStringLiteral("Arrows")) == QStringList({QStringLiteral("down"), QStringLiteral("up")}) &&
              f1.value(QStringLiteral("Home_Garden")) == QStringList({QStringLiteral("home")}) &&
              f1.value(QStringLiteral("Другое")) == QStringList({QStringLiteral("misc")}) && f1.size() == 3,
          "iconify: categories become folders, one style kept");
    // no plain style: the most common style is kept
    const QByteArray styles = R"({"uncategorized": ["a-bold", "b-bold", "a-linear"],
        "suffixes": {"bold": "Bold", "linear": "Linear"}})";
    CHECK(parseCollectionFolders(styles).value(QString()) == QStringList({QStringLiteral("a-bold"), QStringLiteral("b-bold")}),
          "iconify: most common style without a plain one");
    // a big set without categories is split by first letter
    QJsonArray names;
    for (int i = 0; i < 320; ++i)
        names.append(QStringLiteral("%1icon-%2").arg(i % 3 == 0 ? QStringLiteral("a") : i % 3 == 1 ? QStringLiteral("B") : QStringLiteral("1")).arg(i));
    QJsonObject big;
    big[QStringLiteral("uncategorized")] = names;
    const QMap<QString, QStringList> f2 = parseCollectionFolders(QJsonDocument(big).toJson());
    CHECK(f2.keys() == QStringList({QStringLiteral("0-9"), QStringLiteral("A"), QStringLiteral("B")}) &&
              f2.value(QStringLiteral("A")).size() == 107,
          "iconify: big set split by letter");
    QJsonObject small;
    small[QStringLiteral("uncategorized")] = QJsonArray({QStringLiteral("x"), QStringLiteral("y")});
    CHECK(parseCollectionFolders(QJsonDocument(small).toJson()).keys() == QStringList({QString()}),
          "iconify: small set stays in one folder");
    // a huge category is split into parts of at most 500
    QJsonArray many;
    for (int i = 0; i < 1201; ++i)
        many.append(QStringLiteral("icon-%1").arg(i, 4, 10, QLatin1Char('0')));
    QJsonObject huge;
    huge[QStringLiteral("categories")] = QJsonObject({{QStringLiteral("Big"), many}});
    const QMap<QString, QStringList> f3 = parseCollectionFolders(QJsonDocument(huge).toJson());
    int f3total = 0, f3max = 0;
    for (const QStringList& v : f3) {
        f3total += int(v.size());
        f3max = std::max(f3max, int(v.size()));
    }
    CHECK(f3.size() == 3 && f3total == 1201 && f3max <= 500 && f3.firstKey().startsWith(QStringLiteral("Big (icon")),
          "iconify: huge folder split into parts");

    // icon data -> SVG files
    const QByteArray set = R"({"prefix": "t", "width": 24, "height": 24, "icons": {
        "sq": {"body": "<rect width=\"24\" height=\"24\" fill=\"currentColor\"/>"},
        "wide": {"body": "<rect width=\"32\" height=\"16\"/>", "width": 32, "height": 16}},
        "aliases": {"sq2": {"parent": "sq"}, "sq3": {"parent": "sq2", "hFlip": true}, "lost": {"parent": "nope"}}})";
    const QHash<QString, QByteArray> svgs = parseIconSet(set);
    CHECK(svgs.size() == 4 && !svgs.contains(QStringLiteral("lost")), "iconify: icons and aliases");
    const QByteArray sq = svgs.value(QStringLiteral("sq"));
    CHECK(sq.contains("viewBox=\"0 0 24 24\"") && sq.contains("#000000") && !sq.contains("currentColor"),
          "iconify: svg with black instead of currentColor");
    CHECK(svgs.value(QStringLiteral("wide")).contains("viewBox=\"0 0 32 16\""), "iconify: own icon size");
    CHECK(svgs.value(QStringLiteral("sq3")).contains("scale(-1 1)"), "iconify: alias transform");
    QSvgRenderer r(sq);
    CHECK(r.isValid() && r.defaultSize() == QSize(24, 24), "iconify: svg renders");
    QImage img(24, 24, QImage::Format_ARGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        r.render(&p);
    }
    CHECK(qGray(img.pixel(12, 12)) < 20, "iconify: currentColor drawn black");
    QFile f(out.filePath(QStringLiteral("iconify_sq.svg")));
    if (f.open(QIODevice::WriteOnly))
        f.write(sq);

    CHECK(safeFileName(QStringLiteral("a/b:c*?")) == QStringLiteral("a_b_c__") && safeFileName(QStringLiteral("CON")) ==
              QStringLiteral("CON_") && safeFileName(QStringLiteral(" dots... ")) == QStringLiteral("dots"),
          "iconify: safe file names");

    // Russian words
    using namespace iconwords;
    CHECK(translate(QStringLiteral("коробка")).contains(QStringLiteral("box")), "words: exact word");
    CHECK(translate(QStringLiteral("коробки")).contains(QStringLiteral("box")), "words: other ending");
    CHECK(translate(QStringLiteral("Стрел")).contains(QStringLiteral("arrow")), "words: beginning of a word");
    CHECK(translate(QStringLiteral("ёж")).isEmpty() && translate(QStringLiteral("box")).isEmpty(), "words: unknown");
    CHECK(englishQuery(QStringLiteral("красная коробка")) == QStringLiteral("красная box"), "words: query translation");
    CHECK(englishQuery(QStringLiteral("box")) == QStringLiteral("box"), "words: english query kept");
    CHECK(matches(QStringLiteral("box-off"), QStringLiteral("коробка")) &&
              matches(QStringLiteral("Хрупкое"), QStringLiteral("хрупк")) &&
              matches(QStringLiteral("arrow-up"), QStringLiteral("стрелка вверх")) &&
              !matches(QStringLiteral("arrow-down"), QStringLiteral("стрелка вверх")),
          "words: gallery matching");
}

// Multi-up layouts: quick setup, objects shown in one label only, printing.
static void testLayouts(const QDir& out)
{
    // as many 30x20 labels as fit on a 100x70 roll label, 2 mm apart
    LayoutSetup ls;
    ls.carrier = LayoutSetup::CarrierRollLabel;
    ls.carrierW = 100;
    ls.carrierH = 70;
    ls.gapX = ls.gapY = 2;
    SheetLayout l = ls.resolve(30, 20);
    CHECK(!l.roll && l.cols == 3 && l.rows == 3 && std::abs(l.marginLeft - 3) < 1e-6 && std::abs(l.pitchX - 32) < 1e-6,
          "layout: 3x3 of 30x20 fit on 100x70");
    ls.fit = false;
    ls.cols = 3;
    ls.rows = 1;
    l = ls.resolve(30, 20);
    CHECK(l.perPage() == 3 && std::abs(l.marginTop - 25) < 1e-6, "layout: 3 in a row, centred");
    LayoutSetup a4;
    a4.carrier = LayoutSetup::CarrierA4;
    a4.gapX = a4.gapY = 0;
    CHECK(a4.resolve(70, 37).perPage() == 24, "layout: 24 of 70x37 on A4");
    a4.margin = 10;
    CHECK(a4.resolve(70, 37).cols == 2, "layout: margins taken into account");
    LayoutSetup rollOne;
    rollOne.carrier = LayoutSetup::CarrierRoll;
    CHECK(rollOne.resolve(58, 40).roll, "layout: one per roll label");

    // the setup and object cells survive the file; old files stay as they were
    LabelDocument doc;
    doc.label.width = 30;
    doc.label.height = 20;
    doc.print.layoutId = QStringLiteral("custom");
    doc.print.layoutSetup = ls;
    auto all = std::make_unique<RectObject>();
    all->x = all->y = 0;
    all->w = all->h = 5;
    all->strokeWidth = 0;
    all->fillType = shapes::FillSolid;
    all->fill = Qt::black;
    doc.objects.push_back(std::move(all));
    auto second = std::make_unique<RectObject>();
    second->x = 10;
    second->y = 5;
    second->w = second->h = 10;
    second->strokeWidth = 0;
    second->fillType = shapes::FillSolid;
    second->fill = QColor(220, 0, 0);
    second->cell = 2;
    doc.objects.push_back(std::move(second));
    LabelDocument copy;
    QString err;
    CHECK(copy.fromBytes(doc.toBytes(), &err) && copy.toBytes() == doc.toBytes() && copy.objects[1]->cell == 2 &&
              copy.print.layoutSetup.cols == 3 && !copy.print.layoutSetup.fit,
          "layout: setup and cells round trip");
    CHECK(!LabelDocument().toBytes().contains("layoutSetup") && !LabelDocument().toBytes().contains("\"cell\""),
          "layout: nothing new in files without it");

    // one carrier: the red square only in the second label
    DataTable none;
    PrintSettings ps = doc.print;
    ps.copies = 3;
    PrintJob job(doc, none, ps);
    CHECK(job.pageCount() == 1 && job.layout().perPage() == 3, "layout: three labels on one page");
    QImage page(1000, 700, QImage::Format_RGB32);
    page.fill(Qt::white);
    {
        QPainter p(&page);
        p.scale(10, 10);
        job.renderPage(&p, 0, 0);
    }
    page.save(out.filePath(QStringLiteral("layout_page.png")));
    auto at = [&page](double x, double y) { return QColor(page.pixel(int(x * 10), int(y * 10))); };
    bool shared = true, onlySecond = true;
    for (int c = 0; c < 3; ++c) {
        const double x0 = 3 + c * 32, y0 = 25;
        shared = shared && at(x0 + 2.5, y0 + 2.5) == QColor(Qt::black);
        const bool red = at(x0 + 15, y0 + 10).red() > 180 && at(x0 + 15, y0 + 10).green() < 60;
        onlySecond = onlySecond && red == (c == 1);
    }
    CHECK(shared, "layout: shared object printed in every label");
    CHECK(onlySecond, "layout: own object printed only in its label");
    CHECK(job.exportPdf(out.filePath(QStringLiteral("layout.pdf")), &err), "layout: pdf export");
    // single label images get the object of their place on the carrier
    CHECK(job.exportPng(out.filePath(QStringLiteral("layout_%1.png")), 254, &err), "layout: png export");
    const QImage l1(out.filePath(QStringLiteral("layout_1.png"))), l2(out.filePath(QStringLiteral("layout_2.png")));
    CHECK(!l1.isNull() && !l2.isNull() && QColor(l2.pixel(150, 100)).red() > 180 && QColor(l1.pixel(150, 100)) == QColor(Qt::white),
          "layout: png of the second label has its own object");
}

// Shapes, frames, fills, shadows and line caps. Rendered at 254 dpi = 10 px/mm.
static void testShapes(const QDir& out)
{
    RenderContext ctx;
    auto docWith = [](std::unique_ptr<LabelObject> o, double w = 20, double h = 20) {
        auto d = std::make_unique<LabelDocument>();
        d->label.width = w;
        d->label.height = h;
        d->objects.push_back(std::move(o));
        return d;
    };

    // JSON: every new field survives, old files keep their meaning
    {
        PolyShapeObject s;
        s.setKind(shapes::Callout);
        s.param1 = 70;
        s.param2 = 30;
        s.frame = shapes::FrameThickThin;
        s.frameGap = 0.8;
        s.strokeStyle = 2;
        s.fillType = shapes::FillPattern;
        s.fill = QColor(1, 2, 3);
        s.fill2 = QColor(4, 5, 6);
        s.pattern = shapes::PatDots;
        s.patternSpacing = 2;
        s.patternWidth = 0.5;
        s.shadow = true;
        s.shadowDx = -2;
        s.shadowDy = 1.5;
        s.shadowBlur = 0.7;
        s.shadowOpacity = 0.4;
        s.shadowColor = QColor(10, 20, 30);
        auto c = LabelObject::fromJsonAny(s.toJson());
        auto* p = dynamic_cast<PolyShapeObject*>(c.get());
        CHECK(p && p->kind == shapes::Callout && p->param1 == 70 && p->param2 == 30 &&
                  p->frame == shapes::FrameThickThin && p->frameGap == 0.8 && p->strokeStyle == 2 &&
                  p->fillType == shapes::FillPattern && p->fill == QColor(1, 2, 3) && p->fill2 == QColor(4, 5, 6) &&
                  p->pattern == shapes::PatDots && p->patternSpacing == 2 && p->patternWidth == 0.5 && p->shadow &&
                  p->shadowDx == -2 && p->shadowDy == 1.5 && p->shadowBlur == 0.7 && p->shadowOpacity == 0.4 &&
                  p->shadowColor == QColor(10, 20, 30),
              "shape: json round trip");
        CHECK(c && c->toJson() == s.toJson(), "shape: json stable");
        LineObject l;
        l.startCap = LineObject::CapDot;
        l.endCap = LineObject::CapTriangle;
        l.capSize = 7;
        auto lc = LabelObject::fromJsonAny(l.toJson());
        auto* lp = dynamic_cast<LineObject*>(lc.get());
        CHECK(lp && lp->startCap == LineObject::CapDot && lp->endCap == LineObject::CapTriangle && lp->capSize == 7,
              "line caps: json round trip");

        QJsonObject old;
        old[QStringLiteral("type")] = QStringLiteral("rect");
        old[QStringLiteral("fill")] = QStringLiteral("#ffff0000");
        auto o1 = LabelObject::fromJsonAny(old);
        old[QStringLiteral("fill")] = QStringLiteral("#00000000");
        auto o2 = LabelObject::fromJsonAny(old);
        auto* r1 = dynamic_cast<RectObject*>(o1.get());
        auto* r2 = dynamic_cast<RectObject*>(o2.get());
        CHECK(r1 && r1->fillType == shapes::FillSolid && r2 && r2->fillType == shapes::FillNone && !r1->shadow &&
                  r1->frame == shapes::FrameSingle,
              "shape: old files keep their fill");
    }

    // every kind draws something inside its box and exports to PDF
    {
        LabelDocument all;
        all.label.width = 4 * 32;
        all.label.height = 4 * 22;
        for (int k = 0; k < shapes::KindCount; ++k) {
            auto s = std::make_unique<PolyShapeObject>();
            s->setKind(k);
            s->w = 30;
            s->h = 20;
            s->fillType = shapes::FillSolid;
            s->fill = Qt::black;
            const QImage img = renderLabelImage(*docWith(s->clone(), 30, 20), ctx, 100, false);
            int ink = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    ink += qGray(img.pixel(x, y)) < 128;
            const double share = double(ink) / (img.width() * img.height());
            CHECK(share > 0.15 && share < 0.98, ("shape draws: " + shapes::kindId(k).toUtf8()).constData());
            s->x = (k % 4) * 32 + 1;
            s->y = (k / 4) * 22 + 1;
            // vary the effects so the PDF shows them all
            s->fillType = k % 4 == 0 ? shapes::FillLinear : k % 4 == 1 ? shapes::FillRadial
                                                          : k % 4 == 2 ? shapes::FillPattern : shapes::FillSolid;
            s->fill = QColor(30, 90, 200);
            s->fill2 = QColor(255, 230, 120);
            s->frame = k % 3;
            s->strokeWidth = 0.6;
            s->shadow = k % 2 == 0;
            s->shadowBlur = k % 4 == 0 ? 1.0 : 0;
            all.objects.push_back(std::move(s));
        }
        // curved outlines with double frames must stay smooth
        auto e = std::make_unique<EllipseObject>();
        e->x = 33;
        e->y = 67;
        e->w = 30;
        e->h = 20;
        e->strokeWidth = 0.6;
        e->frame = shapes::FrameDouble;
        all.objects.push_back(std::move(e));
        auto rr = std::make_unique<RectObject>();
        rr->x = 65;
        rr->y = 67;
        rr->w = 30;
        rr->h = 20;
        rr->radius = 5;
        rr->strokeWidth = 0.8;
        rr->frame = shapes::FrameThickThin;
        all.objects.push_back(std::move(rr));
        auto ln = std::make_unique<LineObject>();
        ln->x = 97;
        ln->y = 70;
        ln->w = 28;
        ln->h = 14;
        ln->width = 0.5;
        ln->startCap = LineObject::CapDot;
        ln->endCap = LineObject::CapArrow;
        auto ln2 = ln->clone();
        static_cast<LineObject*>(ln2.get())->startCap = LineObject::CapBar;
        static_cast<LineObject*>(ln2.get())->endCap = LineObject::CapTriangle;
        ln2->y = 76;
        all.objects.push_back(std::move(ln));
        all.objects.push_back(std::move(ln2));
        QString err;
        const DataTable noData;
        PrintJob job(all, noData, all.print);
        CHECK(job.exportPdf(out.filePath(QStringLiteral("shapes.pdf")), &err), "shapes: pdf export");
        renderLabelImage(all, ctx, 200, false).save(out.filePath(QStringLiteral("shapes.png")));
    }

    // double frame: two lines with a white gap, nothing in the middle
    {
        auto r = std::make_unique<RectObject>();
        r->w = r->h = 20;
        r->strokeWidth = 1;
        r->frame = shapes::FrameDouble;
        r->frameGap = 1;
        const QImage img = renderLabelImage(*docWith(std::move(r)), ctx, 254, false);
        CHECK(dark(img, 0.5, 10) && light(img, 1.5, 10) && dark(img, 2.5, 10) && light(img, 10, 10),
              "double frame: line, gap, line");
    }

    // gradients: dark-to-light on screen, a halftone on a thermal printer
    {
        auto r = std::make_unique<RectObject>();
        r->w = r->h = 20;
        r->strokeWidth = 0;
        r->fillType = shapes::FillLinear;
        r->fill = Qt::black;
        r->fill2 = Qt::white;
        RectObject* rp = r.get();
        auto doc = docWith(std::move(r));
        const QImage img = renderLabelImage(*doc, ctx, 254, false);
        CHECK(dark(img, 1, 10) && light(img, 19, 10), "linear gradient: left dark, right light");
        const double half = blackShare(renderLabelImage(*doc, ctx, 203, true));
        CHECK(half > 0.3 && half < 0.7, "linear gradient: dithered on thermal");
        rp->fillType = shapes::FillSolid;
        rp->fill = QColor(0x77, 0x77, 0x77);
        CHECK(blackShare(renderLabelImage(*doc, ctx, 203, true)) > 0.95, "solid grey: threshold on thermal");
        rp->fillType = shapes::FillRadial;
        rp->fill = Qt::black;
        const QImage rad = renderLabelImage(*doc, ctx, 254, false);
        CHECK(dark(rad, 10, 10) && !dark(rad, 0.5, 0.5), "radial gradient: dark centre");
    }

    // hatching: both colours inside the shape
    {
        auto r = std::make_unique<RectObject>();
        r->w = r->h = 20;
        r->strokeWidth = 0;
        r->fillType = shapes::FillPattern;
        r->fill = Qt::black;
        r->fill2 = Qt::white;
        r->pattern = shapes::PatCross;
        r->patternSpacing = 2;
        r->patternWidth = 0.4;
        const QImage img = renderLabelImage(*docWith(std::move(r)), ctx, 254, false);
        int ink = 0, paper = 0;
        for (int y = 20; y < 180; ++y)
            for (int x = 20; x < 180; ++x)
                (qGray(img.pixel(x, y)) < 100 ? ink : paper) += qGray(img.pixel(x, y)) < 100 || qGray(img.pixel(x, y)) > 200;
        CHECK(ink > 1000 && paper > 1000, "pattern: lines and gaps");
    }

    // shadows: sharp and soft, for a shape and for an image
    {
        auto r = std::make_unique<RectObject>();
        r->x = r->y = 2;
        r->w = r->h = 10;
        r->strokeWidth = 0;
        r->fillType = shapes::FillSolid;
        r->fill = Qt::white;
        r->shadow = true;
        r->shadowDx = r->shadowDy = 3;
        r->shadowOpacity = 1;
        RectObject* rp = r.get();
        auto doc = docWith(std::move(r));
        QImage img = renderLabelImage(*doc, ctx, 254, false);
        CHECK(dark(img, 14, 14) && light(img, 5, 5) && light(img, 17, 17), "sharp shadow: offset under the shape");
        rp->shadowBlur = 2;
        img = renderLabelImage(*doc, ctx, 254, false);
        const int edge = qGray(img.pixel(150, 100));  // the shadow's right edge at x = 15 mm
        CHECK(edge > 40 && edge < 220 && dark(img, 10, 10.5 + 3), "soft shadow: soft edge");
        const double soft = blackShare(renderLabelImage(*doc, ctx, 203, true));
        CHECK(soft > 0.05 && soft < 0.6, "soft shadow: dithered on thermal");

        auto im = std::make_unique<ImageObject>();
        QImage paper(10, 10, QImage::Format_ARGB32);  // opaque white picture
        paper.fill(Qt::white);
        im->setEmbedded(paper);
        im->x = im->y = 2;
        im->w = im->h = 10;
        im->shadow = true;
        im->shadowDx = im->shadowDy = 3;
        im->shadowOpacity = 1;
        img = renderLabelImage(*docWith(std::move(im)), ctx, 254, false);
        CHECK(dark(img, 14, 14) && light(img, 5, 5), "image shadow");

        auto t = std::make_unique<TextObject>();
        t->text = QStringLiteral("█");
        t->shadow = true;
        CHECK(!t->silhouette(ctx).isEmpty(), "text shadow: vector outline");
        BarcodeObject bc;
        bc.shadow = true;
        CHECK(bc.shadowReach() == 0, "barcode: no shadow");
    }

    // line caps: a triangle head is wider than the line
    {
        auto l = std::make_unique<LineObject>();
        l->w = 30;
        l->h = 6;
        l->width = 0.5;
        l->endCap = LineObject::CapTriangle;
        l->capSize = 6;
        LineObject* lp = l.get();
        auto doc = docWith(std::move(l), 30, 6);
        QImage img = renderLabelImage(*doc, ctx, 254, false);
        CHECK(dark(img, 29, 3) && dark(img, 28, 2.2) && !dark(img, 15, 2.2), "line: triangle head");
        lp->endCap = LineObject::CapNone;
        img = renderLabelImage(*doc, ctx, 254, false);
        CHECK(!dark(img, 28, 2.2) && dark(img, 29, 3), "line: plain end");
    }
}

// Every shipped clipart file must load, survive recolouring and actually draw
// something; the contact sheet clipart_sheet.png is for eyeballing the set.
static void testClipart(const QDir& out)
{
    const QDir root(QCoreApplication::applicationDirPath() + QStringLiteral("/clipart"));
    CHECK(root.exists(), "clipart: folder next to the executable");
    QStringList files;
    for (const QString& sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        for (const QString& f : QDir(root.filePath(sub)).entryList({QStringLiteral("*.svg")}, QDir::Files, QDir::Name))
            files << root.filePath(sub + QLatin1Char('/') + f);
    CHECK(files.size() >= 50, "clipart: starter set present");
    const int cell = 72, cols = 10;
    QImage sheet(cols * cell, int((files.size() + cols - 1) / cols) * cell * 2, QImage::Format_ARGB32);
    sheet.fill(Qt::white);
    QPainter sp(&sheet);
    const QColor tint(200, 0, 0);
    for (int i = 0; i < files.size(); ++i) {
        QFile f(files.at(i));
        f.open(QIODevice::ReadOnly);
        const QByteArray svg = f.readAll();
        const QByteArray name = QFileInfo(files.at(i)).fileName().toUtf8();
        const QByteArray recoloured = recolorSvg(svg, tint);
        QSvgRenderer plain(svg), red(recoloured);
        if (!plain.isValid() || !red.isValid()) {
            CHECK(false, ("clipart: invalid svg " + name).constData());
            continue;
        }
        auto draw = [cell](QSvgRenderer& r) {
            QImage img(cell - 8, cell - 8, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter p(&img);
            QSizeF s = r.viewBoxF().size();
            s.scale(img.width(), img.height(), Qt::KeepAspectRatio);
            r.render(&p, QRectF((img.width() - s.width()) / 2, (img.height() - s.height()) / 2, s.width(), s.height()));
            return img;
        };
        const QImage a = draw(plain), b = draw(red);
        int inked = 0, tinted = 0;
        for (int y = 0; y < b.height(); ++y)
            for (int x = 0; x < b.width(); ++x) {
                const QRgb pa = a.pixel(x, y), pb = b.pixel(x, y);
                inked += qAlpha(pa) > 128 && qGray(pa) < 200;
                tinted += qAlpha(pb) > 200 && qRed(pb) > 150 && qGreen(pb) < 60 && qBlue(pb) < 60;
            }
        CHECK(inked > 50, ("clipart: draws something " + name).constData());
        CHECK(tinted > 50, ("clipart: recolours " + name).constData());
        const int cx = (i % cols) * cell + 4, cy = (i / cols) * cell * 2 + 4;
        sp.drawImage(cx, cy, a);
        sp.drawImage(cx, cy + cell, b);
    }
    sp.end();
    sheet.save(out.filePath(QStringLiteral("clipart_sheet.png")));
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
    testClipart(out);
    testShapes(out);
    testDataBinding(out);
    testIconify(out);
    testLayouts(out);

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
