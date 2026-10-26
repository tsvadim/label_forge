// SPDX-License-Identifier: GPL-3.0-or-later
#include "PropertyPanel.h"

#include "Icons.h"
#include "barcode/Barcode.h"
#include "core/LabelDocument.h"
#include "core/LabelObject.h"
#include "core/RenderContext.h"
#include "print/SheetLayout.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf {

namespace {

class ColorButton : public QToolButton {
public:
    ColorButton(const QColor& c, bool allowNone, std::function<void(const QColor&)> onChange, QWidget* parent = nullptr)
        : QToolButton(parent), m_color(c), m_onChange(std::move(onChange))
    {
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        setPopupMode(QToolButton::MenuButtonPopup);
        auto* menu = new QMenu(this);
        if (allowNone)
            menu->addAction(QObject::tr("Без цвета (прозрачный)"), this, [this] { setColorAndNotify(Qt::transparent); });
        menu->addAction(QObject::tr("Чёрный"), this, [this] { setColorAndNotify(Qt::black); });
        menu->addAction(QObject::tr("Белый"), this, [this] { setColorAndNotify(Qt::white); });
        setMenu(menu);
        connect(this, &QToolButton::clicked, this, [this] {
            const QColor c2 = QColorDialog::getColor(m_color.alpha() ? m_color : QColor(Qt::black), this,
                                                     QObject::tr("Выбор цвета"), QColorDialog::ShowAlphaChannel);
            if (c2.isValid())
                setColorAndNotify(c2);
        });
        refresh();
    }
    void setColorAndNotify(const QColor& c)
    {
        m_color = c;
        refresh();
        m_onChange(c);
    }

private:
    void refresh()
    {
        setIcon(icons::colorSwatch(m_color));
        setText(m_color.alpha() == 0 ? QObject::tr("нет") : m_color.name().toUpper());
    }
    QColor m_color;
    std::function<void(const QColor&)> m_onChange;
};

QLabel* section(const QString& title)
{
    auto* l = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
    l->setContentsMargins(0, 8, 0, 2);
    return l;
}

} // namespace

PropertyPanel::PropertyPanel(QWidget* parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setMinimumWidth(270);
}

void PropertyPanel::setTarget(LabelDocument* doc, const QList<LabelObject*>& selection, const QStringList& fields)
{
    m_doc = doc;
    m_sel = selection;
    m_fields = fields;
    build();
}

void PropertyPanel::rebuildLater()
{
    QTimer::singleShot(0, this, [this] { build(); });
}

void PropertyPanel::refreshGeometry()
{
    m_loading = true;
    for (auto& f : m_refreshers)
        f();
    m_loading = false;
}

void PropertyPanel::edited(LabelObject* o, const QString& key)
{
    if (!m_loading)
        emit objectEdited(o, key);
}

QDoubleSpinBox* PropertyPanel::spin(double min, double max, int decimals, double step, const QString& suffix,
                                    std::function<double()> get, std::function<void(double)> set)
{
    auto* s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    s->setValue(get());
    s->setAccelerated(true);
    connect(s, &QDoubleSpinBox::valueChanged, this, [this, set](double v) {
        if (!m_loading)
            set(v);
    });
    m_refreshers.push_back([s, get] {
        const QSignalBlocker b(s);
        s->setValue(get());
    });
    return s;
}

QWidget* PropertyPanel::fieldMenuButton(std::function<void(const QString&)> insert)
{
    auto* b = new QToolButton;
    b->setText(tr("Поле ▾"));
    b->setToolTip(tr("Вставить поле данных или переменную"));
    b->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(b);
    if (!m_fields.isEmpty()) {
        menu->addSection(tr("Поля данных"));
        for (const QString& f : m_fields)
            menu->addAction(QStringLiteral("{%1}").arg(f), b, [insert, f] { insert(QStringLiteral("{%1}").arg(f)); });
    }
    menu->addSection(tr("Переменные"));
    const QStringList vars = builtinVariables();
    const QStringList descr = {tr("номер этикетки"),   tr("номер с ведущими нулями"), tr("номер записи"),
                               tr("текущая дата"),     tr("дата + 30 дней"),         tr("дата в формате"),
                               tr("текущее время"),    tr("время с секундами")};
    for (int i = 0; i < vars.size(); ++i) {
        const QString v = vars.at(i);
        menu->addAction(QStringLiteral("%1  — %2").arg(v, descr.value(i)), b, [insert, v] { insert(v); });
    }
    b->setMenu(menu);
    return b;
}

void PropertyPanel::build()
{
    m_refreshers.clear();
    m_loading = true;
    auto* content = new QWidget;
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(8, 4, 8, 8);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    outer->addLayout(form);

    if (!m_doc) {
        // nothing
    } else if (m_sel.isEmpty()) {
        buildLabel(form);
    } else if (m_sel.size() > 1) {
        auto* l = new QLabel(tr("Выбрано объектов: %1\n\nИспользуйте меню «Упорядочить» для выравнивания "
                                "и распределения.").arg(m_sel.size()));
        l->setWordWrap(true);
        form->addRow(l);
    } else {
        LabelObject* o = m_sel.first();
        auto* title = new QLabel(QStringLiteral("<b>%1</b>").arg(o->displayType()));
        form->addRow(title);
        if (auto* t = dynamic_cast<TextObject*>(o))
            buildText(form, t);
        else if (auto* b = dynamic_cast<BarcodeObject*>(o))
            buildBarcode(form, b);
        else if (auto* r = dynamic_cast<RectObject*>(o))
            buildShape(form, r, true);
        else if (auto* e = dynamic_cast<EllipseObject*>(o))
            buildShape(form, e, false);
        else if (auto* ln = dynamic_cast<LineObject*>(o))
            buildLine(form, ln);
        else if (auto* im = dynamic_cast<ImageObject*>(o))
            buildImage(form, im);
        buildCommon(form, o);
    }
    outer->addStretch(1);
    QWidget* old = takeWidget();
    setWidget(content);
    if (old)
        old->deleteLater();
    m_loading = false;
}

void PropertyPanel::buildLabel(QFormLayout* f)
{
    LabelDocument* d = m_doc;
    f->addRow(section(tr("Этикетка")));
    f->addRow(tr("Ширина:"), spin(1, 1000, 2, 0.5, tr(" мм"), [d] { return d->label.width; }, [this, d](double v) {
                  d->label.width = v;
                  emit labelEdited(QStringLiteral("label.width"));
              }));
    f->addRow(tr("Высота:"), spin(1, 1000, 2, 0.5, tr(" мм"), [d] { return d->label.height; }, [this, d](double v) {
                  d->label.height = v;
                  emit labelEdited(QStringLiteral("label.height"));
              }));
    f->addRow(tr("Скругление:"), spin(0, 100, 2, 0.5, tr(" мм"), [d] { return d->label.radius; }, [this, d](double v) {
                  d->label.radius = v;
                  emit labelEdited(QStringLiteral("label.radius"));
              }));
    f->addRow(tr("Фон:"), new ColorButton(d->label.background, false, [this, d](const QColor& c) {
                  d->label.background = c;
                  emit labelEdited(QStringLiteral("label.bg"));
              }));

    f->addRow(section(tr("Макет печати")));
    auto* combo = new QComboBox;
    combo->addItem(tr("Рулон / термопринтер"), QStringLiteral("roll"));
    int cur = 0;
    for (const SheetLayout& l : SheetLayoutLibrary::all()) {
        if (l.id == QLatin1String("roll"))
            continue;
        combo->addItem(l.name, l.id);
        if (l.id == d->print.layoutId)
            cur = combo->count() - 1;
    }
    combo->setCurrentIndex(cur);
    combo->setToolTip(tr("Выбор листа задаёт размер этикетки и раскладку при печати"));
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, d](int) {
        if (m_loading)
            return;
        const QString id = combo->currentData().toString();
        d->print.layoutId = id;
        if (id != QLatin1String("roll")) {
            const SheetLayout l = SheetLayoutLibrary::find(id, d->label.width, d->label.height);
            if (!l.roll) {
                d->label.width = l.labelW;
                d->label.height = l.labelH;
                d->label.radius = l.radius;
            } else {
                d->label.width = l.labelW;
                d->label.height = l.labelH;
            }
        }
        emit labelEdited(QStringLiteral("label.layout"));
        emit layoutApplied();
        rebuildLater();
    });
    f->addRow(combo);
    auto* hint = new QLabel(tr("Щёлкните по объекту, чтобы изменить его свойства. "
                               "Инструменты добавления — на панели слева."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    f->addRow(hint);
}

void PropertyPanel::buildCommon(QFormLayout* f, LabelObject* o)
{
    f->addRow(section(tr("Положение и размер")));
    f->addRow(tr("X:"), spin(-1000, 1000, 2, 0.1, tr(" мм"), [o] { return o->x; }, [this, o](double v) {
                  o->x = v;
                  edited(o, QStringLiteral("x"));
              }));
    f->addRow(tr("Y:"), spin(-1000, 1000, 2, 0.1, tr(" мм"), [o] { return o->y; }, [this, o](double v) {
                  o->y = v;
                  edited(o, QStringLiteral("y"));
              }));
    f->addRow(tr("Ширина:"), spin(0.1, 1000, 2, 0.1, tr(" мм"), [o] { return o->w; }, [this, o](double v) {
                  o->w = v;
                  edited(o, QStringLiteral("w"));
              }));
    f->addRow(tr("Высота:"), spin(0.1, 1000, 2, 0.1, tr(" мм"), [o] { return o->h; }, [this, o](double v) {
                  o->h = v;
                  edited(o, QStringLiteral("h"));
              }));
    f->addRow(tr("Поворот:"), spin(-360, 360, 1, 90, QStringLiteral("°"), [o] { return o->rotation; },
                                   [this, o](double v) {
                                       o->rotation = v;
                                       edited(o, QStringLiteral("rotation"));
                                   }));
    auto* name = new QLineEdit(o->name);
    name->setPlaceholderText(tr("необязательно"));
    connect(name, &QLineEdit::textEdited, this, [this, o](const QString& s) {
        o->name = s;
        edited(o, QStringLiteral("name"));
    });
    f->addRow(tr("Имя:"), name);
    auto* printable = new QCheckBox(tr("Печатать объект"));
    printable->setChecked(o->printable);
    connect(printable, &QCheckBox::toggled, this, [this, o](bool on) {
        o->printable = on;
        edited(o, QStringLiteral("printable"));
    });
    f->addRow(printable);
    auto* locked = new QCheckBox(tr("Заблокировать перемещение"));
    locked->setChecked(o->locked);
    connect(locked, &QCheckBox::toggled, this, [this, o](bool on) {
        o->locked = on;
        edited(o, QStringLiteral("locked"));
    });
    f->addRow(locked);
}

void PropertyPanel::buildText(QFormLayout* f, TextObject* t)
{
    auto* edit = new QPlainTextEdit(t->text);
    edit->setTabChangesFocus(true);
    edit->setMinimumHeight(70);
    edit->setMaximumHeight(140);
    connect(edit, &QPlainTextEdit::textChanged, this, [this, t, edit] {
        t->text = edit->toPlainText();
        edited(t, QStringLiteral("text"));
    });
    f->addRow(edit);
    f->addRow(fieldMenuButton([edit](const QString& s) {
        edit->insertPlainText(s);
        edit->setFocus();
    }));

    f->addRow(section(tr("Шрифт")));
    auto* font = new QFontComboBox;
    font->setCurrentFont(QFont(t->fontFamily));
    connect(font, &QFontComboBox::currentFontChanged, this, [this, t](const QFont& fnt) {
        t->fontFamily = fnt.family();
        edited(t, QStringLiteral("font"));
    });
    f->addRow(tr("Гарнитура:"), font);
    f->addRow(tr("Размер:"), spin(1, 500, 1, 1, tr(" пт"), [t] { return t->fontSize; }, [this, t](double v) {
                  t->fontSize = v;
                  edited(t, QStringLiteral("size"));
              }));
    auto* styleRow = new QWidget;
    auto* sl = new QHBoxLayout(styleRow);
    sl->setContentsMargins(0, 0, 0, 0);
    auto addStyle = [&](const QString& txt, bool on, const QString& key, std::function<void(bool)> set) {
        auto* b = new QToolButton;
        b->setText(txt);
        b->setCheckable(true);
        b->setChecked(on);
        QFont bf = b->font();
        if (key == QLatin1String("bold"))
            bf.setBold(true);
        if (key == QLatin1String("italic"))
            bf.setItalic(true);
        if (key == QLatin1String("underline"))
            bf.setUnderline(true);
        b->setFont(bf);
        connect(b, &QToolButton::toggled, this, [this, t, key, set](bool v) {
            set(v);
            edited(t, key);
        });
        sl->addWidget(b);
    };
    addStyle(tr("Ж"), t->bold, QStringLiteral("bold"), [t](bool v) { t->bold = v; });
    addStyle(tr("К"), t->italic, QStringLiteral("italic"), [t](bool v) { t->italic = v; });
    addStyle(tr("Ч"), t->underline, QStringLiteral("underline"), [t](bool v) { t->underline = v; });
    sl->addStretch();
    f->addRow(tr("Стиль:"), styleRow);
    f->addRow(tr("Цвет:"), new ColorButton(t->color, false, [this, t](const QColor& c) {
                  t->color = c;
                  edited(t, QStringLiteral("color"));
              }));
    f->addRow(tr("Заливка:"), new ColorButton(t->fill, true, [this, t](const QColor& c) {
                  t->fill = c;
                  edited(t, QStringLiteral("fill"));
              }));

    f->addRow(section(tr("Абзац")));
    auto* ha = new QComboBox;
    ha->addItems({tr("По левому краю"), tr("По центру"), tr("По правому краю")});
    ha->setCurrentIndex(t->hAlign);
    connect(ha, &QComboBox::currentIndexChanged, this, [this, t](int i) {
        t->hAlign = i;
        edited(t, QStringLiteral("hAlign"));
    });
    f->addRow(tr("По горизонтали:"), ha);
    auto* va = new QComboBox;
    va->addItems({tr("Сверху"), tr("По середине"), tr("Снизу")});
    va->setCurrentIndex(t->vAlign);
    connect(va, &QComboBox::currentIndexChanged, this, [this, t](int i) {
        t->vAlign = i;
        edited(t, QStringLiteral("vAlign"));
    });
    f->addRow(tr("По вертикали:"), va);
    f->addRow(tr("Межстрочный:"), spin(0.5, 3, 2, 0.05, QStringLiteral("×"), [t] { return t->lineSpacing; },
                                       [this, t](double v) {
                                           t->lineSpacing = v;
                                           edited(t, QStringLiteral("lineSpacing"));
                                       }));
    auto* wrap = new QCheckBox(tr("Переносить по словам"));
    wrap->setChecked(t->wordWrap);
    connect(wrap, &QCheckBox::toggled, this, [this, t](bool v) {
        t->wordWrap = v;
        edited(t, QStringLiteral("wrap"));
    });
    f->addRow(wrap);
    auto* shrink = new QCheckBox(tr("Уменьшать шрифт, чтобы текст поместился"));
    shrink->setChecked(t->autoShrink);
    connect(shrink, &QCheckBox::toggled, this, [this, t](bool v) {
        t->autoShrink = v;
        edited(t, QStringLiteral("autoShrink"));
    });
    f->addRow(shrink);
}

void PropertyPanel::buildShape(QFormLayout* f, ShapeObject* s, bool isRect)
{
    f->addRow(tr("Контур:"), new ColorButton(s->stroke, true, [this, s](const QColor& c) {
                  s->stroke = c;
                  edited(s, QStringLiteral("stroke"));
              }));
    f->addRow(tr("Толщина:"), spin(0, 20, 2, 0.1, tr(" мм"), [s] { return s->strokeWidth; }, [this, s](double v) {
                  s->strokeWidth = v;
                  edited(s, QStringLiteral("strokeWidth"));
              }));
    f->addRow(tr("Заливка:"), new ColorButton(s->fill, true, [this, s](const QColor& c) {
                  s->fill = c;
                  edited(s, QStringLiteral("fill"));
              }));
    if (isRect) {
        auto* r = static_cast<RectObject*>(s);
        f->addRow(tr("Скругление:"), spin(0, 200, 2, 0.5, tr(" мм"), [r] { return r->radius; }, [this, r](double v) {
                      r->radius = v;
                      edited(r, QStringLiteral("radius"));
                  }));
    }
}

void PropertyPanel::buildLine(QFormLayout* f, LineObject* l)
{
    f->addRow(tr("Цвет:"), new ColorButton(l->color, false, [this, l](const QColor& c) {
                  l->color = c;
                  edited(l, QStringLiteral("color"));
              }));
    f->addRow(tr("Толщина:"), spin(0.05, 20, 2, 0.1, tr(" мм"), [l] { return l->width; }, [this, l](double v) {
                  l->width = v;
                  edited(l, QStringLiteral("lineWidth"));
              }));
    auto* style = new QComboBox;
    style->addItems({tr("Сплошная"), tr("Штриховая"), tr("Пунктирная"), tr("Штрих-пунктир"), tr("Штрих-две точки")});
    style->setCurrentIndex(qBound(1, l->penStyle, 5) - 1);
    connect(style, &QComboBox::currentIndexChanged, this, [this, l](int i) {
        l->penStyle = i + 1;
        edited(l, QStringLiteral("penStyle"));
    });
    f->addRow(tr("Тип:"), style);
    auto* hint = new QLabel(tr("Для вертикальной или наклонной линии задайте поворот."));
    hint->setWordWrap(true);
    f->addRow(hint);
}

void PropertyPanel::buildImage(QFormLayout* f, ImageObject* im)
{
    auto* load = new QPushButton(tr("Загрузить и встроить…"));
    connect(load, &QPushButton::clicked, this, [this, im] {
        QStringList filters;
        for (const QByteArray& fmt : QImageReader::supportedImageFormats())
            filters << QStringLiteral("*.") + QString::fromLatin1(fmt);
        const QString path = QFileDialog::getOpenFileName(this, tr("Изображение"), QString(),
                                                          tr("Изображения (%1)").arg(filters.join(QLatin1Char(' '))));
        if (path.isEmpty())
            return;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (img.isNull()) {
            QMessageBox::warning(this, tr("Изображение"), reader.errorString());
            return;
        }
        if (img.width() > 2000 || img.height() > 2000)
            img = img.scaled(2000, 2000, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        im->setEmbedded(img);
        if (img.width() > 0 && img.height() > 0)
            im->h = im->w * img.height() / img.width();
        edited(im, QStringLiteral("image"));
        rebuildLater();
    });
    f->addRow(load);
    auto* path = new QLineEdit(im->pathTemplate);
    path->setPlaceholderText(tr("или путь к файлу, напр. photos\\{Артикул}.jpg"));
    connect(path, &QLineEdit::textEdited, this, [this, im](const QString& s) {
        im->pathTemplate = s;
        if (!s.isEmpty())
            im->imageData.clear();
        edited(im, QStringLiteral("imagePath"));
    });
    f->addRow(tr("Файл:"), path);
    f->addRow(fieldMenuButton([path, im, this](const QString& s) {
        path->insert(s);
        im->pathTemplate = path->text();
        im->imageData.clear();
        edited(im, QStringLiteral("imagePath"));
    }));
    auto* aspect = new QCheckBox(tr("Сохранять пропорции"));
    aspect->setChecked(im->keepAspect);
    connect(aspect, &QCheckBox::toggled, this, [this, im](bool v) {
        im->keepAspect = v;
        edited(im, QStringLiteral("keepAspect"));
    });
    f->addRow(aspect);
    auto* mono = new QCheckBox(tr("Оттенки серого"));
    mono->setChecked(im->monochrome);
    connect(mono, &QCheckBox::toggled, this, [this, im](bool v) {
        im->monochrome = v;
        edited(im, QStringLiteral("monochrome"));
    });
    f->addRow(mono);
}

void PropertyPanel::buildBarcode(QFormLayout* f, BarcodeObject* b)
{
    auto* sym = new QComboBox;
    int cur = 0;
    for (const auto& s : barcode::symbologies()) {
        sym->addItem(s.name, s.id);
        if (s.id == b->symbology)
            cur = sym->count() - 1;
    }
    sym->setCurrentIndex(cur);
    connect(sym, &QComboBox::currentIndexChanged, this, [this, b, sym](int) {
        const QString old = b->symbology;
        b->symbology = sym->currentData().toString();
        if (barcode::is2D(b->symbology) && !barcode::is2D(old)) {
            const double side = std::min(b->w, b->h);
            b->w = b->h = side;
            b->showText = false;
        }
        edited(b, QStringLiteral("symbology"));
        rebuildLater();
    });
    f->addRow(tr("Тип:"), sym);

    auto* data = new QLineEdit(b->data);
    data->setToolTip(tr("Данные штрихкода. Можно использовать поля {Поле} и переменные.\n"
                        "Спецсимволы: \\F — FNC1, \\G — разделитель GS, \\n — перевод строки."));
    connect(data, &QLineEdit::textEdited, this, [this, b](const QString& s) {
        b->data = s;
        edited(b, QStringLiteral("data"));
    });
    f->addRow(tr("Данные:"), data);
    f->addRow(fieldMenuButton([this, data, b](const QString& s) {
        data->insert(s);
        b->data = data->text();
        edited(b, QStringLiteral("data"));
    }));
    for (const auto& s : barcode::symbologies())
        if (s.id == b->symbology) {
            auto* ex = new QLabel(tr("Пример: %1").arg(s.sample));
            ex->setStyleSheet(QStringLiteral("color: palette(mid);"));
            ex->setWordWrap(true);
            ex->setTextInteractionFlags(Qt::TextSelectableByMouse);
            f->addRow(ex);
        }

    const bool twoD = barcode::is2D(b->symbology);
    if (b->symbology == QLatin1String("qr")) {
        auto* ecc = new QComboBox;
        ecc->addItems({tr("L — 7%"), tr("M — 15%"), tr("Q — 25%"), tr("H — 30%")});
        ecc->setCurrentIndex(qBound(0, b->qrEcc, 3));
        connect(ecc, &QComboBox::currentIndexChanged, this, [this, b](int i) {
            b->qrEcc = i;
            edited(b, QStringLiteral("qrEcc"));
        });
        f->addRow(tr("Коррекция:"), ecc);
    }
    if (b->symbology == QLatin1String("code39")) {
        auto* chk = new QCheckBox(tr("Контрольный символ (mod 43)"));
        chk->setChecked(b->code39Check);
        connect(chk, &QCheckBox::toggled, this, [this, b](bool v) {
            b->code39Check = v;
            edited(b, QStringLiteral("code39Check"));
        });
        f->addRow(chk);
    }
    auto* quiet = new QCheckBox(tr("Свободная зона по краям"));
    quiet->setChecked(b->quietZone);
    connect(quiet, &QCheckBox::toggled, this, [this, b](bool v) {
        b->quietZone = v;
        edited(b, QStringLiteral("quietZone"));
    });
    f->addRow(quiet);
    f->addRow(tr("Цвет:"), new ColorButton(b->color, false, [this, b](const QColor& c) {
                  b->color = c;
                  edited(b, QStringLiteral("color"));
              }));
    if (!twoD) {
        f->addRow(section(tr("Подпись")));
        auto* show = new QCheckBox(tr("Показывать текст под штрихкодом"));
        show->setChecked(b->showText);
        connect(show, &QCheckBox::toggled, this, [this, b](bool v) {
            b->showText = v;
            edited(b, QStringLiteral("showText"));
        });
        f->addRow(show);
        auto* font = new QFontComboBox;
        font->setCurrentFont(QFont(b->fontFamily));
        connect(font, &QFontComboBox::currentFontChanged, this, [this, b](const QFont& fnt) {
            b->fontFamily = fnt.family();
            edited(b, QStringLiteral("font"));
        });
        f->addRow(tr("Шрифт:"), font);
        f->addRow(tr("Размер:"), spin(3, 72, 1, 0.5, tr(" пт"), [b] { return b->textSize; }, [this, b](double v) {
                      b->textSize = v;
                      edited(b, QStringLiteral("textSize"));
                  }));
    }
}

} // namespace lf
