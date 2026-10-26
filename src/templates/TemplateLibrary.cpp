// SPDX-License-Identifier: GPL-3.0-or-later
#include "TemplateLibrary.h"

#include "core/LabelDocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

namespace lf {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Templates", s); }

// ------------------------------------------------ builders for built-in templates
TextObject* text(LabelDocument& d, double x, double y, double w, double h, const QString& s, double size,
                 bool bold = false, int hAlign = 0, int vAlign = 0)
{
    auto t = std::make_unique<TextObject>();
    t->x = x;
    t->y = y;
    t->w = w;
    t->h = h;
    t->text = s;
    t->fontSize = size;
    t->bold = bold;
    t->hAlign = hAlign;
    t->vAlign = vAlign;
    TextObject* raw = t.get();
    d.objects.push_back(std::move(t));
    return raw;
}

BarcodeObject* code(LabelDocument& d, double x, double y, double w, double h, const QString& sym,
                    const QString& data, double textSize = 8)
{
    auto b = std::make_unique<BarcodeObject>();
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->symbology = sym;
    b->data = data;
    b->textSize = textSize;
    BarcodeObject* raw = b.get();
    d.objects.push_back(std::move(b));
    return raw;
}

RectObject* rect(LabelDocument& d, double x, double y, double w, double h, double stroke, double radius = 0,
                 const QColor& fill = Qt::transparent)
{
    auto r = std::make_unique<RectObject>();
    r->x = x;
    r->y = y;
    r->w = w;
    r->h = h;
    r->strokeWidth = stroke;
    r->radius = radius;
    r->fill = fill;
    RectObject* raw = r.get();
    d.objects.push_back(std::move(r));
    return raw;
}

void line(LabelDocument& d, double x, double y, double w, double width = 0.3)
{
    auto l = std::make_unique<LineObject>();
    l->x = x;
    l->y = y - 1;
    l->w = w;
    l->h = 2;
    l->width = width;
    d.objects.push_back(std::move(l));
}

void inlineData(LabelDocument& d, const QStringList& cols, const QVector<QStringList>& rows)
{
    d.data.kind = DataKind::Inline;
    d.data.table.columns = cols;
    d.data.table.rows = rows;
}

constexpr double PIN_PITCH = 2.54;
constexpr int PIN_COUNTS[] = {2, 3, 4, 5, 6, 8, 9, 10, 12, 16, 20, 40};

// Pin labels for a single-row 2.54 mm header: one cell per pin with a vertical
// signal name and the pin number underneath; pin 1 is highlighted.
void buildPinHeader(LabelDocument& d, int n)
{
    const double h = 8.5;  // side face of a PBS socket
    const double w = n * PIN_PITCH;
    const double numH = 1.6;
    d.label.width = w;
    d.label.height = h;
    d.print.layoutId = QStringLiteral("a4-auto");
    rect(d, 0, 0, w, h, 0.12);
    for (int i = 1; i < n; ++i) {  // separators between pins
        auto l = std::make_unique<LineObject>();
        l->w = h;
        l->h = 1;
        l->x = i * PIN_PITCH - h / 2;
        l->y = h / 2 - 0.5;
        l->rotation = 90;
        l->width = 0.08;
        d.objects.push_back(std::move(l));
    }
    line(d, 0, h - numH, w, 0.08);
    auto* first = rect(d, 0, h - numH, PIN_PITCH, numH, 0, 0, Qt::black);
    first->stroke = Qt::transparent;
    const double nameLen = h - numH - 0.5;
    for (int i = 0; i < n; ++i) {
        const double cx = (i + 0.5) * PIN_PITCH;
        // vertical name, read bottom-to-top
        auto t = std::make_unique<TextObject>();
        t->text = QStringLiteral("{%1}").arg(i + 1);
        t->w = nameLen;
        t->h = PIN_PITCH - 0.25;
        t->x = cx - t->w / 2;
        t->y = 0.25 + nameLen / 2 - t->h / 2;
        t->rotation = -90;
        t->fontSize = 6;
        t->bold = true;
        t->hAlign = 1;
        t->vAlign = 1;
        t->wordWrap = false;
        t->autoShrink = true;
        d.objects.push_back(std::move(t));
        auto* num = text(d, i * PIN_PITCH, h - numH, PIN_PITCH, numH, QString::number(i + 1), 3.8, false, 1, 1);
        num->autoShrink = true;
        num->wordWrap = false;
        if (i == 0)
            num->color = Qt::white;
    }
    static const char* const POOL[] = {"GND", "VCC", "3V3", "SDA", "SCL", "TX",  "RX",  "D2",  "D3",  "D4",
                                       "D5",  "D6",  "D7",  "A0",  "A1",  "A2",  "A3",  "MISO", "MOSI", "SCK"};
    QStringList cols{QStringLiteral("Разъём")};
    QStringList row1{QStringLiteral("J1")}, row2{QStringLiteral("J2")};
    for (int i = 0; i < n; ++i) {
        cols << QString::number(i + 1);
        row1 << QLatin1String(POOL[i % 20]);
        row2 << (i == 0 ? QStringLiteral("GND") : i == 1 ? QStringLiteral("5V") : QStringLiteral("IO%1").arg(i - 1));
    }
    inlineData(d, cols, {row1, row2});
}

QString sanitize(const QString& s)
{
    QString out = s;
    out.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return out.trimmed().left(80);
}
} // namespace

QString TemplateLibrary::userDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                      QStringLiteral("/templates");
    QDir().mkpath(d);
    return d;
}

QVector<TemplateInfo> TemplateLibrary::builtins()
{
    struct B {
        const char* id;
        QString title, category, description;
    };
    const QVector<B> list = {
        {"price-58x40", tr("Ценник 58×40"), tr("Торговля"),
         tr("Ценник с наименованием, ценой и EAN-13 для термопринтера.")},
        {"price-a4-21", tr("Ценник на листе A4 (21 шт.)"), tr("Торговля"),
         tr("Ценник 63,5×38,1 мм для листов Avery L7160.")},
        {"product-70x37", tr("Товарная этикетка 70×37"), tr("Склад"),
         tr("Наименование, артикул и Code 128; лист A4 по 24 этикетки.")},
        {"address-l7163", tr("Адресная наклейка 99,1×38,1"), tr("Почта"),
         tr("Получатель и адрес, листы Avery L7163 (14 шт.).")},
        {"shipping-100x150", tr("Транспортная этикетка 100×150"), tr("Почта"),
         tr("Отправитель, получатель и трек-номер (Code 128) для термопринтера.")},
        {"asset-qr-50x30", tr("Инвентарная бирка с QR 50×30"), tr("Склад"),
         tr("QR-код и инвентарный номер со сквозной нумерацией {seq}.")},
        {"dm-40x30", tr("Маркировка GS1 DataMatrix 40×30"), tr("Маркировка"),
         tr("GS1 DataMatrix с GTIN и серийным номером.")},
        {"badge-90x55", tr("Бейдж 90×55"), tr("Мероприятия"),
         tr("Имя, должность и компания участника.")},
    };
    QVector<TemplateInfo> v;
    for (int n : PIN_COUNTS) {
        TemplateInfo t;
        t.id = QStringLiteral("pin-header-1x%1").arg(n);
        t.title = tr("Штыревое гнездо 1×%1 (шаг 2,54)").arg(n);
        t.category = tr("Электроника");
        t.description = tr("Подписи пинов на боковую грань гнезда PBS / штырей PLS 1×%1: полоска %2×8,5 мм, "
                           "по ячейке на пин. Имена пинов — в колонках 1…%1 таблицы данных, "
                           "одна строка — один разъём.")
                            .arg(n)
                            .arg(QString::number(n * PIN_PITCH, 'f', 2).replace(QLatin1Char('.'), QLatin1Char(',')));
        t.builtin = true;
        v << t;
    }
    for (const B& b : list) {
        TemplateInfo t;
        t.id = QLatin1String(b.id);
        t.title = b.title;
        t.category = b.category;
        t.description = b.description;
        t.builtin = true;
        v << t;
    }
    return v;
}

QVector<TemplateInfo> TemplateLibrary::userTemplates()
{
    QVector<TemplateInfo> v;
    QDir dir(userDir());
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.lfl")}, QDir::Files, QDir::Name)) {
        LabelDocument doc;
        if (!doc.load(fi.absoluteFilePath()))
            continue;
        TemplateInfo t;
        t.id = t.path = fi.absoluteFilePath();
        t.title = doc.title.isEmpty() ? fi.completeBaseName() : doc.title;
        t.category = doc.category.isEmpty() ? tr("Мои шаблоны") : doc.category;
        t.description = doc.description;
        v << t;
    }
    return v;
}

QVector<TemplateInfo> TemplateLibrary::all() { return builtins() + userTemplates(); }

QStringList TemplateLibrary::categories()
{
    QStringList c;
    for (const TemplateInfo& t : all())
        if (!c.contains(t.category))
            c << t.category;
    return c;
}

bool TemplateLibrary::load(const TemplateInfo& info, LabelDocument& doc, QString* err)
{
    if (info.builtin) {
        doc.clear();
        buildBuiltin(info.id, doc);
        doc.title = info.title;
        doc.category = info.category;
        doc.description = info.description;
        return true;
    }
    return doc.load(info.path, err);
}

bool TemplateLibrary::saveUser(const LabelDocument& doc, const QString& title, const QString& category,
                               const QString& description, bool includeData, QString* outPath, QString* err)
{
    LabelDocument copy;
    copy.copyFrom(doc);
    copy.title = title;
    copy.category = category;
    copy.description = description;
    if (!includeData)
        copy.data = DataSourceConfig();
    QString base = sanitize(title);
    if (base.isEmpty())
        base = QStringLiteral("template");
    const QString path = QDir(userDir()).filePath(base + QStringLiteral(".lfl"));
    if (outPath)
        *outPath = path;
    return copy.save(path, err);
}

bool TemplateLibrary::remove(const TemplateInfo& info)
{
    if (info.builtin)
        return false;
    return QFile::remove(info.path);
}

bool TemplateLibrary::importFile(const QString& path, QString* err)
{
    LabelDocument doc;
    if (!doc.load(path, err))
        return false;
    const QString title = doc.title.isEmpty() ? QFileInfo(path).completeBaseName() : doc.title;
    return saveUser(doc, title, doc.category, doc.description, true, nullptr, err);
}

void TemplateLibrary::buildBuiltin(const QString& id, LabelDocument& d)
{
    if (id == QLatin1String("price-58x40")) {
        d.label.width = 58;
        d.label.height = 40;
        d.label.radius = 2;
        d.print.layoutId = QStringLiteral("roll");
        auto* name = text(d, 2, 1.5, 54, 9, QStringLiteral("{Наименование}"), 9, true, 1, 1);
        name->autoShrink = true;
        auto* price = text(d, 2, 10.5, 40, 11, QStringLiteral("{Цена:0,00}"), 26, true, 2, 1);
        price->autoShrink = true;
        price->wordWrap = false;
        text(d, 42.5, 10.5, 13.5, 11, tr("грн"), 11, true, 0, 1);
        line(d, 2, 22, 54, 0.25);
        code(d, 3, 23.5, 38, 14.5, QStringLiteral("ean13"), QStringLiteral("{Штрихкод}"), 7);
        text(d, 41, 25, 15, 5, QStringLiteral("{Ед}"), 7, false, 2);
        text(d, 41, 33, 15, 5, QStringLiteral("{date}"), 6, false, 2, 2);
        inlineData(d, {QStringLiteral("Наименование"), QStringLiteral("Цена"), QStringLiteral("Штрихкод"),
                       QStringLiteral("Ед")},
                   {{QStringLiteral("Молоко 2,5% 900 г"), QStringLiteral("42.5"), QStringLiteral("482012345678"),
                     QStringLiteral("шт.")},
                    {QStringLiteral("Хлеб «Украинский» нарезной 650 г"), QStringLiteral("31.9"),
                     QStringLiteral("482098765432"), QStringLiteral("шт.")},
                    {QStringLiteral("Яблоки Голден"), QStringLiteral("39"), QStringLiteral("200000001234"),
                     QStringLiteral("кг")}});
    } else if (id == QLatin1String("price-a4-21")) {
        d.label.width = 63.5;
        d.label.height = 38.1;
        d.label.radius = 2;
        d.print.layoutId = QStringLiteral("avery-l7160");
        auto* name = text(d, 3, 2, 57.5, 9, QStringLiteral("{Наименование}"), 10, true, 1, 1);
        name->autoShrink = true;
        auto* price = text(d, 3, 11, 57.5, 12, QStringLiteral("{Цена:0,00} грн"), 24, true, 1, 1);
        price->autoShrink = true;
        price->wordWrap = false;
        code(d, 8, 23.5, 47.5, 13, QStringLiteral("ean13"), QStringLiteral("{Штрихкод}"), 7);
        inlineData(d, {QStringLiteral("Наименование"), QStringLiteral("Цена"), QStringLiteral("Штрихкод")},
                   {{QStringLiteral("Кофе молотый 250 г"), QStringLiteral("189"), QStringLiteral("482011122233")},
                    {QStringLiteral("Чай чёрный 100 пак."), QStringLiteral("124.5"), QStringLiteral("482044455566")}});
    } else if (id == QLatin1String("product-70x37")) {
        d.label.width = 70;
        d.label.height = 37;
        d.print.layoutId = QStringLiteral("a4-3x8-70x37");
        auto* name = text(d, 4, 3, 62, 10, QStringLiteral("{Наименование}"), 10, true, 0, 0);
        name->autoShrink = true;
        text(d, 4, 13, 62, 5, tr("Арт.: {Артикул}"), 8);
        code(d, 4, 19, 62, 15, QStringLiteral("code128"), QStringLiteral("{Артикул}"), 8);
        inlineData(d, {QStringLiteral("Наименование"), QStringLiteral("Артикул")},
                   {{QStringLiteral("Саморез 4,2×19 (уп. 100 шт.)"), QStringLiteral("SR-4219-100")},
                    {QStringLiteral("Дюбель 6×40 нейлон"), QStringLiteral("DB-0640-50")}});
    } else if (id == QLatin1String("address-l7163")) {
        d.label.width = 99.1;
        d.label.height = 38.1;
        d.label.radius = 2;
        d.print.layoutId = QStringLiteral("avery-l7163");
        text(d, 5, 3, 89, 4, tr("Кому:"), 7);
        auto* to = text(d, 5, 7, 89, 28,
                        QStringLiteral("{Получатель}\n{Адрес}\n{Город}, {Индекс}"), 11, false, 0, 0);
        to->autoShrink = true;
        inlineData(d, {QStringLiteral("Получатель"), QStringLiteral("Адрес"), QStringLiteral("Город"),
                       QStringLiteral("Индекс")},
                   {{QStringLiteral("Иванов Иван Иванович"), QStringLiteral("ул. Шевченко, 1, кв. 5"),
                     QStringLiteral("Хмельницкий"), QStringLiteral("29000")},
                    {QStringLiteral("ООО «Ромашка»"), QStringLiteral("пр. Мира, 10, офис 12"),
                     QStringLiteral("Киев"), QStringLiteral("01001")}});
    } else if (id == QLatin1String("shipping-100x150")) {
        d.label.width = 100;
        d.label.height = 150;
        d.print.layoutId = QStringLiteral("roll");
        rect(d, 2, 2, 96, 146, 0.5);
        text(d, 5, 5, 90, 4, tr("ОТПРАВИТЕЛЬ"), 7, true);
        text(d, 5, 9, 90, 16, QStringLiteral("{Отправитель}\n{АдресОтправителя}"), 9);
        line(d, 2, 27, 96, 0.5);
        text(d, 5, 30, 90, 4, tr("ПОЛУЧАТЕЛЬ"), 7, true);
        auto* to = text(d, 5, 35, 90, 35, QStringLiteral("{Получатель}\n{Адрес}\n{Телефон}"), 14, true);
        to->autoShrink = true;
        line(d, 2, 73, 96, 0.5);
        text(d, 5, 76, 45, 6, tr("Вес: {Вес} кг"), 10);
        text(d, 50, 76, 45, 6, tr("Мест: {Мест}"), 10, false, 2);
        line(d, 2, 85, 96, 0.5);
        code(d, 7, 90, 86, 40, QStringLiteral("code128"), QStringLiteral("{Трек}"), 12);
        text(d, 5, 136, 90, 8, QStringLiteral("{date} {time}"), 8, false, 1, 1);
        inlineData(d, {QStringLiteral("Отправитель"), QStringLiteral("АдресОтправителя"), QStringLiteral("Получатель"),
                       QStringLiteral("Адрес"), QStringLiteral("Телефон"), QStringLiteral("Вес"),
                       QStringLiteral("Мест"), QStringLiteral("Трек")},
                   {{QStringLiteral("ФОП Петренко"), QStringLiteral("Хмельницкий, ул. Проскуровская, 2"),
                     QStringLiteral("Сидоренко Ольга"), QStringLiteral("Львов, ул. Городоцкая, 15"),
                     QStringLiteral("+380 67 000 00 00"), QStringLiteral("1,2"), QStringLiteral("1"),
                     QStringLiteral("20450012345678")}});
    } else if (id == QLatin1String("asset-qr-50x30")) {
        d.label.width = 50;
        d.label.height = 30;
        d.label.radius = 1.5;
        d.print.layoutId = QStringLiteral("roll");
        auto* q = code(d, 1.5, 1.5, 27, 27, QStringLiteral("qr"), QStringLiteral("INV-{seq:000001}"));
        q->showText = false;
        q->quietZone = false;
        text(d, 30, 3, 18.5, 5, tr("Инв. №"), 7);
        auto* n = text(d, 30, 8, 18.5, 8, QStringLiteral("{seq:000001}"), 12, true);
        n->autoShrink = true;
        n->wordWrap = false;
        auto* own = text(d, 30, 18, 18.5, 10, tr("Собственность компании"), 6);
        own->vAlign = 2;
    } else if (id == QLatin1String("dm-40x30")) {
        d.label.width = 40;
        d.label.height = 30;
        d.print.layoutId = QStringLiteral("roll");
        auto* dm = code(d, 1.5, 3, 18, 18, QStringLiteral("gs1-datamatrix"),
                        QStringLiteral("(01){GTIN}(21){Серийный}"));
        dm->showText = false;
        auto* name = text(d, 21, 2.5, 17.5, 12, QStringLiteral("{Наименование}"), 7, true);
        name->autoShrink = true;
        text(d, 21, 15, 17.5, 3.5, QStringLiteral("GTIN {GTIN}"), 4.5);
        text(d, 21, 18.5, 17.5, 3.5, QStringLiteral("SN {Серийный}"), 4.5);
        text(d, 1.5, 23, 37, 5, QStringLiteral("(01){GTIN}(21){Серийный}"), 5, false, 1, 1);
        inlineData(d, {QStringLiteral("Наименование"), QStringLiteral("GTIN"), QStringLiteral("Серийный")},
                   {{QStringLiteral("Кроссовки беговые 42"), QStringLiteral("04601234567893"),
                     QStringLiteral("5aB0x9Kq2")},
                    {QStringLiteral("Кроссовки беговые 43"), QStringLiteral("04601234567893"),
                     QStringLiteral("7cD1y8Lr4")}});
    } else if (id == QLatin1String("badge-90x55")) {
        d.label.width = 90;
        d.label.height = 55;
        d.label.radius = 3;
        d.print.layoutId = QStringLiteral("roll");
        auto* band = rect(d, 0, 0, 90, 12, 0, 0, QColor(0x1f, 0x4e, 0x79));
        band->stroke = Qt::transparent;
        auto* ev = text(d, 4, 0, 82, 12, QStringLiteral("{Событие}"), 12, true, 1, 1);
        ev->color = Qt::white;
        auto* nm = text(d, 4, 16, 82, 16, QStringLiteral("{Имя}"), 22, true, 1, 1);
        nm->autoShrink = true;
        nm->wordWrap = false;
        text(d, 4, 33, 82, 7, QStringLiteral("{Должность}"), 11, false, 1, 1);
        text(d, 4, 41, 82, 7, QStringLiteral("{Компания}"), 11, true, 1, 1);
        inlineData(d, {QStringLiteral("Событие"), QStringLiteral("Имя"), QStringLiteral("Должность"),
                       QStringLiteral("Компания")},
                   {{QStringLiteral("Конференция 2026"), QStringLiteral("Ирина Коваль"),
                     QStringLiteral("Инженер-технолог"), QStringLiteral("ТОВ «Прогрес»")}});
    } else if (id.startsWith(QLatin1String("pin-header-1x"))) {
        buildPinHeader(d, std::clamp(id.mid(13).toInt(), 1, 80));
    } else {
        d.label.width = 58;
        d.label.height = 40;
    }
}

} // namespace lf
