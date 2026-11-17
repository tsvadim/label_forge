// SPDX-License-Identifier: GPL-3.0-or-later
#include "PropertyPanel.h"

#include "Icons.h"
#include "barcode/Barcode.h"
#include "core/LabelDocument.h"
#include "core/LabelObject.h"
#include "core/RenderContext.h"
#include "core/ShapePaths.h"
#include "print/SheetLayout.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
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

QWidget* PropertyPanel::colorRow(LabelObject* o, const QString& prop, QColor* target, bool allowNone,
                                  const QString& editKey)
{
    auto* btn = new ColorButton(*target, allowNone, [this, o, target, editKey](const QColor& c) {
        *target = c;
        edited(o, editKey);
    });
    if (prop.isEmpty())
        return btn;
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    l->addWidget(btn);
    auto* bind = new QToolButton;
    bind->setObjectName(QStringLiteral("colorField-") + prop);
    bind->setPopupMode(QToolButton::InstantPopup);
    auto refresh = [o, prop, bind, btn] {
        const QString t = o->colorFields.value(prop);
        QFont f = bind->font();
        f.setBold(!t.isEmpty());
        bind->setFont(f);
        // long field names are elided, the panel is often narrow
        bind->setText(t.isEmpty() ? QStringLiteral("{}") : QFontMetrics(f).elidedText(t, Qt::ElideMiddle, 70));
        bind->setStatusTip(t);
        const QString help = tr("Брать цвет из поля данных: #RRGGBB, #AARRGGBB или имя (red, navy).\n"
                                "Пустое или неверное значение — цвет объекта.");
        bind->setToolTip(t.isEmpty() ? help : tr("Цвет из %1").arg(t) + QStringLiteral("\n\n") + help);
        btn->setToolTip(t.isEmpty() ? QString() : tr("При печати цвет берётся из %1").arg(t));
    };
    auto set = [this, o, prop, refresh](const QString& t) {
        if (t.isEmpty())
            o->colorFields.remove(prop);
        else
            o->colorFields.insert(prop, t);
        refresh();
        edited(o, QStringLiteral("colorField-") + prop);
    };
    auto* menu = new QMenu(bind);
    connect(menu, &QMenu::aboutToShow, bind, [this, menu, o, prop, set, bind] {
        menu->clear();
        const QString cur = o->colorFields.value(prop);
        if (m_fields.isEmpty()) {
            menu->addAction(tr("Нет полей данных"))->setEnabled(false);
        } else {
            menu->addSection(tr("Цвет из поля"));
            for (const QString& f : m_fields) {
                const QString t = QStringLiteral("{%1}").arg(f);
                QAction* a = menu->addAction(t, bind, [set, t] { set(t); });
                a->setCheckable(true);
                a->setChecked(cur == t);
            }
        }
        menu->addSeparator();
        menu->addAction(tr("Шаблон вручную…"), bind, [this, set, cur] {
            bool ok = false;
            const QString t = QInputDialog::getText(this, tr("Цвет из данных"),
                                                    tr("Шаблон, например {Цвет}:"), QLineEdit::Normal, cur, &ok);
            if (ok)
                set(t.trimmed());
        });
        menu->addAction(tr("Не привязывать"), bind, [set] { set(QString()); })->setEnabled(!cur.isEmpty());
    });
    bind->setMenu(menu);
    refresh();
    l->addWidget(bind);
    l->addStretch();
    return w;
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
        else if (auto* sh = dynamic_cast<ShapeObject*>(o))
            buildShape(form, sh);
        else if (auto* ln = dynamic_cast<LineObject*>(o))
            buildLine(form, ln);
        else if (auto* im = dynamic_cast<ImageObject*>(o))
            buildImage(form, im);
        if (o->supportsShadow())
            buildShadow(form, o);
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
    f->addRow(tr("Цвет:"), colorRow(t, QStringLiteral("color"), &t->color, false, QStringLiteral("color")));
    f->addRow(tr("Заливка:"), colorRow(t, QStringLiteral("fill"), &t->fill, true, QStringLiteral("fill")));

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

namespace {
QStringList penStyleNames()
{
    return {QObject::tr("Сплошная"), QObject::tr("Штриховая"), QObject::tr("Пунктирная"), QObject::tr("Штрих-пунктир"),
            QObject::tr("Штрих-две точки")};
}
} // namespace

void PropertyPanel::buildShape(QFormLayout* f, ShapeObject* s)
{
    // combos that show or hide other rows rebuild the panel
    auto combo = [this, f](const QString& label, const QStringList& items, int current, std::function<void(int)> set,
                           bool rebuild) {
        auto* c = new QComboBox;
        c->addItems(items);
        c->setCurrentIndex(current);
        connect(c, &QComboBox::currentIndexChanged, this, [this, set, rebuild](int i) {
            if (m_loading)
                return;
            set(i);
            if (rebuild)
                rebuildLater();
        });
        f->addRow(label, c);
        return c;
    };
    auto color = [this, f, s](const QString& label, QColor* target, bool allowNone, const QString& key) {
        // the second gradient / pattern colour cannot be bound to a field
        const bool bindable = key == QLatin1String("stroke") || key == QLatin1String("fill");
        f->addRow(label, colorRow(s, bindable ? key : QString(), target, allowNone, key));
    };

    if (auto* ps = dynamic_cast<PolyShapeObject*>(s)) {
        f->addRow(section(tr("Фигура")));
        QStringList kinds;
        for (int k = 0; k < shapes::KindCount; ++k)
            kinds << shapes::kindName(k);
        auto* kind = combo(tr("Вид:"), kinds, ps->kind, [this, ps](int k) {
            ps->setKind(k);
            edited(ps, QStringLiteral("kind"));
        }, true);
        for (int k = 0; k < shapes::KindCount; ++k)
            kind->setItemIcon(k, icons::shape(k));
        for (int param = 1; param <= 2; ++param) {
            const QString label = param == 1 ? shapes::param1Label(ps->kind) : shapes::param2Label(ps->kind);
            if (label.isEmpty())
                continue;
            double mn, mx, def;
            int dec;
            shapes::paramRange(ps->kind, param, &mn, &mx, &def, &dec);
            const bool count = param == 1 && (ps->kind == shapes::Polygon || ps->kind == shapes::Star);
            double* field = param == 1 ? &ps->param1 : &ps->param2;
            f->addRow(label, spin(mn, mx, dec, count ? 1 : 5, count ? QString() : QStringLiteral(" %"),
                                  [field] { return *field; }, [this, ps, field, param](double v) {
                                      *field = v;
                                      edited(ps, QStringLiteral("param%1").arg(param));
                                  }));
        }
    }

    f->addRow(section(tr("Контур")));
    color(tr("Цвет:"), &s->stroke, true, QStringLiteral("stroke"));
    f->addRow(tr("Толщина:"), spin(0, 20, 2, 0.1, tr(" мм"), [s] { return s->strokeWidth; }, [this, s](double v) {
                  s->strokeWidth = v;
                  edited(s, QStringLiteral("strokeWidth"));
              }));
    combo(tr("Рамка:"), {tr("Одинарная"), tr("Двойная"), tr("Толстая + тонкая")}, s->frame, [this, s](int i) {
        s->frame = i;
        edited(s, QStringLiteral("frame"));
    }, true);
    if (s->frame == shapes::FrameSingle) {
        combo(tr("Тип линии:"), penStyleNames(), std::clamp(s->strokeStyle, 1, 5) - 1, [this, s](int i) {
            s->strokeStyle = i + 1;
            edited(s, QStringLiteral("strokeStyle"));
        }, false);
    } else {
        f->addRow(tr("Зазор:"), spin(0.05, 20, 2, 0.1, tr(" мм"), [s] { return s->frameGap; }, [this, s](double v) {
                      s->frameGap = v;
                      edited(s, QStringLiteral("frameGap"));
                  }));
    }
    if (auto* r = dynamic_cast<RectObject*>(s)) {
        f->addRow(tr("Скругление:"), spin(0, 200, 2, 0.5, tr(" мм"), [r] { return r->radius; }, [this, r](double v) {
                      r->radius = v;
                      edited(r, QStringLiteral("radius"));
                  }));
    }

    f->addRow(section(tr("Заливка")));
    combo(tr("Заливка:"), {tr("Нет"), tr("Сплошная"), tr("Линейный градиент"), tr("Радиальный градиент"), tr("Узор")},
          s->fillType, [this, s](int i) {
              s->fillType = i;
              // a freshly chosen fill should be visible
              if (s->fill.alpha() == 0)
                  s->fill = i == shapes::FillSolid ? QColor(200, 200, 200) : QColor(Qt::black);
              edited(s, QStringLiteral("fillType"));
          }, true);
    switch (s->fillType) {
    case shapes::FillSolid: color(tr("Цвет:"), &s->fill, false, QStringLiteral("fill")); break;
    case shapes::FillLinear:
        color(tr("Цвет 1:"), &s->fill, true, QStringLiteral("fill"));
        color(tr("Цвет 2:"), &s->fill2, true, QStringLiteral("fill2"));
        f->addRow(tr("Угол:"), spin(-360, 360, 0, 15, QStringLiteral("°"), [s] { return s->gradientAngle; },
                                    [this, s](double v) {
                                        s->gradientAngle = v;
                                        edited(s, QStringLiteral("gradientAngle"));
                                    }));
        break;
    case shapes::FillRadial:
        color(tr("В центре:"), &s->fill, true, QStringLiteral("fill"));
        color(tr("По краям:"), &s->fill2, true, QStringLiteral("fill2"));
        break;
    case shapes::FillPattern: {
        QStringList names;
        for (int i = 0; i < shapes::PatternCount; ++i)
            names << shapes::patternName(i);
        combo(tr("Узор:"), names, s->pattern, [this, s](int i) {
            s->pattern = i;
            edited(s, QStringLiteral("pattern"));
        }, false);
        color(tr("Цвет узора:"), &s->fill, false, QStringLiteral("fill"));
        color(tr("Фон:"), &s->fill2, true, QStringLiteral("fill2"));
        f->addRow(tr("Шаг:"), spin(0.2, 50, 2, 0.1, tr(" мм"), [s] { return s->patternSpacing; }, [this, s](double v) {
                      s->patternSpacing = v;
                      edited(s, QStringLiteral("patternSpacing"));
                  }));
        f->addRow(tr("Толщина узора:"), spin(0.05, 20, 2, 0.05, tr(" мм"), [s] { return s->patternWidth; },
                                            [this, s](double v) {
                                                s->patternWidth = v;
                                                edited(s, QStringLiteral("patternWidth"));
                                            }));
        break;
    }
    default: break;
    }
    if (s->fillType == shapes::FillLinear || s->fillType == shapes::FillRadial) {
        auto* hint = new QLabel(tr("На термопринтере градиент печатается точечным узором."));
        hint->setWordWrap(true);
        f->addRow(hint);
    }
}

void PropertyPanel::buildShadow(QFormLayout* f, LabelObject* o)
{
    f->addRow(section(tr("Тень")));
    auto* on = new QCheckBox(tr("Отбрасывать тень"));
    on->setChecked(o->shadow);
    connect(on, &QCheckBox::toggled, this, [this, o](bool v) {
        o->shadow = v;
        edited(o, QStringLiteral("shadow"));
        rebuildLater();
    });
    f->addRow(on);
    if (!o->shadow)
        return;
    f->addRow(tr("Сдвиг X:"), spin(-50, 50, 2, 0.1, tr(" мм"), [o] { return o->shadowDx; }, [this, o](double v) {
                  o->shadowDx = v;
                  edited(o, QStringLiteral("shadowDx"));
              }));
    f->addRow(tr("Сдвиг Y:"), spin(-50, 50, 2, 0.1, tr(" мм"), [o] { return o->shadowDy; }, [this, o](double v) {
                  o->shadowDy = v;
                  edited(o, QStringLiteral("shadowDy"));
              }));
    f->addRow(tr("Цвет тени:"), new ColorButton(o->shadowColor, false, [this, o](const QColor& c) {
                  o->shadowColor = c;
                  edited(o, QStringLiteral("shadowColor"));
              }));
    f->addRow(tr("Непрозрачность:"), spin(0, 100, 0, 5, QStringLiteral(" %"), [o] { return o->shadowOpacity * 100; },
                                          [this, o](double v) {
                                              o->shadowOpacity = v / 100;
                                              edited(o, QStringLiteral("shadowOpacity"));
                                          }));
    auto* blur = spin(0, 20, 2, 0.2, tr(" мм"), [o] { return o->shadowBlur; }, [this, o](double v) {
        o->shadowBlur = v;
        edited(o, QStringLiteral("shadowBlur"));
    });
    blur->setToolTip(tr("0 — резкая тень, остаётся векторной в PDF.\n"
                        "Размытая тень переводится в растр, на термопринтере — точечным узором."));
    f->addRow(tr("Размытие:"), blur);
}

void PropertyPanel::buildLine(QFormLayout* f, LineObject* l)
{
    f->addRow(tr("Цвет:"), colorRow(l, QStringLiteral("color"), &l->color, false, QStringLiteral("color")));
    f->addRow(tr("Толщина:"), spin(0.05, 20, 2, 0.1, tr(" мм"), [l] { return l->width; }, [this, l](double v) {
                  l->width = v;
                  edited(l, QStringLiteral("lineWidth"));
              }));
    auto* style = new QComboBox;
    style->addItems(penStyleNames());
    style->setCurrentIndex(qBound(1, l->penStyle, 5) - 1);
    connect(style, &QComboBox::currentIndexChanged, this, [this, l](int i) {
        l->penStyle = i + 1;
        edited(l, QStringLiteral("penStyle"));
    });
    f->addRow(tr("Тип:"), style);
    const QStringList caps{tr("Нет"), tr("Стрелка"), tr("Треугольник"), tr("Точка"), tr("Черта")};
    auto capCombo = [this, f, l, caps](const QString& label, int* field, const QString& key) {
        auto* c = new QComboBox;
        c->addItems(caps);
        c->setCurrentIndex(std::clamp(*field, 0, 4));
        connect(c, &QComboBox::currentIndexChanged, this, [this, l, field, key](int i) {
            if (m_loading)
                return;
            *field = i;
            edited(l, key);
            rebuildLater();
        });
        f->addRow(label, c);
    };
    capCombo(tr("Начало:"), &l->startCap, QStringLiteral("startCap"));
    capCombo(tr("Конец:"), &l->endCap, QStringLiteral("endCap"));
    if (l->startCap != LineObject::CapNone || l->endCap != LineObject::CapNone) {
        auto* size = spin(1, 20, 1, 0.5, tr(" × толщина"), [l] { return l->capSize; }, [this, l](double v) {
            l->capSize = v;
            edited(l, QStringLiteral("capSize"));
        });
        size->setToolTip(tr("Длина наконечника в толщинах линии"));
        f->addRow(tr("Наконечник:"), size);
    }
    auto* hint = new QLabel(tr("Для вертикальной или наклонной линии задайте поворот."));
    hint->setWordWrap(true);
    f->addRow(hint);
}

void PropertyPanel::buildImage(QFormLayout* f, ImageObject* im)
{
    auto* load = new QPushButton(tr("Загрузить и встроить…"));
    connect(load, &QPushButton::clicked, this, [this, im] {
        QStringList filters{QStringLiteral("*.svg"), QStringLiteral("*.svgz")};
        for (const QByteArray& fmt : QImageReader::supportedImageFormats())
            filters << QStringLiteral("*.") + QString::fromLatin1(fmt);
        filters.removeDuplicates();
        const QString path = QFileDialog::getOpenFileName(this, tr("Изображение"), QString(),
                                                          tr("Изображения (%1)").arg(filters.join(QLatin1Char(' '))));
        if (path.isEmpty())
            return;
        QString err;
        if (!im->loadFile(path, &err)) {
            QMessageBox::warning(this, tr("Изображение"), err);
            return;
        }
        const QSizeF s = im->sourceSizeMm(RenderContext());
        if (!s.isEmpty())
            im->h = im->w * s.height() / s.width();
        edited(im, QStringLiteral("image"));
        rebuildLater();
    });
    f->addRow(load);
    auto* saveAs = new QPushButton(tr("Сохранить изображение как…"));
    saveAs->setEnabled(!im->imageData.isEmpty() || !im->svgData.isEmpty());
    connect(saveAs, &QPushButton::clicked, this, [this, im] {
        const bool svg = !im->svgData.isEmpty();
        const bool gz = svg && im->svgData.startsWith("\x1f\x8b");
        const QString suffix = svg ? (gz ? QStringLiteral("svgz") : QStringLiteral("svg")) : QStringLiteral("png");
        const QString path = QFileDialog::getSaveFileName(this, tr("Сохранить изображение"),
                                                          tr("изображение.%1").arg(suffix),
                                                          QStringLiteral("%1 (*.%2)").arg(suffix.toUpper(), suffix));
        if (path.isEmpty())
            return;
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(svg ? im->svgData : im->imageData) < 0)
            QMessageBox::warning(this, tr("Изображение"), out.errorString());
    });
    f->addRow(saveAs);
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
    auto check = [this, im, f](const QString& text, bool value, const QString& key, std::function<void(bool)> set) {
        auto* cb = new QCheckBox(text);
        cb->setChecked(value);
        connect(cb, &QCheckBox::toggled, this, [this, im, key, set](bool v) {
            set(v);
            edited(im, key);
        });
        f->addRow(cb);
        return cb;
    };

    f->addRow(section(tr("Размещение")));
    auto* fit = new QComboBox;
    fit->addItems({tr("Без масштаба"), tr("Пропорционально"), tr("Растянуть")});
    fit->setCurrentIndex(im->fit);
    f->addRow(tr("Вписать:"), fit);
    auto* ha = new QComboBox;
    ha->addItems({tr("Слева"), tr("По центру"), tr("Справа")});
    ha->setCurrentIndex(im->hAlign);
    connect(ha, &QComboBox::currentIndexChanged, this, [this, im](int i) {
        im->hAlign = i;
        edited(im, QStringLiteral("hAlign"));
    });
    f->addRow(tr("По горизонтали:"), ha);
    auto* va = new QComboBox;
    va->addItems({tr("Сверху"), tr("По середине"), tr("Снизу")});
    va->setCurrentIndex(im->vAlign);
    connect(va, &QComboBox::currentIndexChanged, this, [this, im](int i) {
        im->vAlign = i;
        edited(im, QStringLiteral("vAlign"));
    });
    f->addRow(tr("По вертикали:"), va);
    auto syncAlign = [ha, va, fit] {
        ha->setEnabled(fit->currentIndex() != ImageObject::FitStretch);
        va->setEnabled(fit->currentIndex() != ImageObject::FitStretch);
    };
    syncAlign();
    connect(fit, &QComboBox::currentIndexChanged, this, [this, im, syncAlign](int i) {
        im->fit = i;
        syncAlign();
        edited(im, QStringLiteral("fit"));
    });
    auto cropSpin = [this, im](double ImageObject::*field, const QString& tip, const QString& key) {
        auto* s = spin(0, 99, 0, 1, QStringLiteral("%"), [im, field] { return im->*field; },
                       [this, im, field, key](double v) {
                           im->*field = v;
                           edited(im, key);
                       });
        s->setToolTip(tip);
        return s;
    };
    auto cropRow = [f](const QString& label, QWidget* a, QWidget* b) {
        auto* row = new QWidget;
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(a);
        l->addWidget(b);
        f->addRow(label, row);
    };
    cropRow(tr("Обрезка Л / П:"), cropSpin(&ImageObject::cropLeft, tr("Обрезать слева"), QStringLiteral("cropLeft")),
            cropSpin(&ImageObject::cropRight, tr("Обрезать справа"), QStringLiteral("cropRight")));
    cropRow(tr("Обрезка В / Н:"), cropSpin(&ImageObject::cropTop, tr("Обрезать сверху"), QStringLiteral("cropTop")),
            cropSpin(&ImageObject::cropBottom, tr("Обрезать снизу"), QStringLiteral("cropBottom")));
    check(tr("Отразить по горизонтали"), im->flipH, QStringLiteral("flipH"), [im](bool v) { im->flipH = v; });
    check(tr("Отразить по вертикали"), im->flipV, QStringLiteral("flipV"), [im](bool v) { im->flipV = v; });

    f->addRow(section(tr("Цвет")));
    f->addRow(tr("Непрозрачность:"), spin(0, 100, 0, 5, QStringLiteral("%"), [im] { return im->opacity * 100; },
                                           [this, im](double v) {
                                               im->opacity = v / 100;
                                               edited(im, QStringLiteral("opacity"));
                                           }));
    const QString rasterTip = tr("У SVG эта настройка переводит картинку в растр (300 dpi в PDF)");
    auto* bright = spin(-100, 100, 0, 5, QString(), [im] { return double(im->brightness); }, [this, im](double v) {
        im->brightness = int(v);
        edited(im, QStringLiteral("brightness"));
    });
    bright->setToolTip(rasterTip);
    f->addRow(tr("Яркость:"), bright);
    auto* contr = spin(-100, 100, 0, 5, QString(), [im] { return double(im->contrast); }, [this, im](double v) {
        im->contrast = int(v);
        edited(im, QStringLiteral("contrast"));
    });
    contr->setToolTip(rasterTip);
    f->addRow(tr("Контраст:"), contr);
    check(tr("Оттенки серого"), im->monochrome, QStringLiteral("monochrome"), [im](bool v) { im->monochrome = v; })
        ->setToolTip(rasterTip);
    auto* recolor = check(tr("Перекрасить значок"), im->recolor, QStringLiteral("recolor"),
                          [im](bool v) { im->recolor = v; });
    recolor->setToolTip(tr("Для одноцветных значков. SVG: все цвета, кроме белого, заменяются выбранным.\n"
                           "Растр: тёмные части получают выбранный цвет, светлые становятся прозрачными."));
    f->addRow(tr("Цвет значка:"), colorRow(im, QStringLiteral("tint"), &im->tintColor, false, QStringLiteral("tint")));
    check(tr("Дизеринг на термопринтере"), im->dither, QStringLiteral("dither"),
          [im](bool v) { im->dither = v; })
        ->setToolTip(tr("При прямой печати ZPL/TSPL фото и оттенки передаются точечным узором.\n"
                        "Выключите для логотипов и значков с чёткими краями."));
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
