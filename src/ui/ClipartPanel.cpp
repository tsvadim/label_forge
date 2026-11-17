// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClipartPanel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QImageReader>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTabBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace lf {

namespace {
const int THUMB = 48;

// List that drags its items out as file URLs, so the label view (and other
// programs) see an ordinary file drop.
class ClipartList : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QStringList mimeTypes() const override { return {QStringLiteral("text/uri-list")}; }
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
    {
        QList<QUrl> urls;
        for (const QListWidgetItem* it : items)
            urls << QUrl::fromLocalFile(it->data(Qt::UserRole).toString());
        auto* mime = new QMimeData;
        mime->setUrls(urls);
        return mime;
    }
};

bool isSvgFile(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("svg") || s == QLatin1String("svgz");
}

QIcon thumbnail(const QString& path)
{
    // thumbnails survive rescans; a changed file gets a new key
    static QHash<QString, QIcon> cache;
    const QFileInfo fi(path);
    const QString key = path + QLatin1Char('|') + QString::number(fi.lastModified().toMSecsSinceEpoch()) +
                        QLatin1Char('|') + QString::number(fi.size());
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    QImage img;
    if (isSvgFile(path)) {
        QSvgRenderer r(path);
        if (r.isValid()) {
            QSizeF s = r.defaultSize();
            if (s.isEmpty())
                s = r.viewBoxF().size();
            if (s.isEmpty())
                s = QSizeF(1, 1);
            s.scale(THUMB, THUMB, Qt::KeepAspectRatio);
            img = QImage(THUMB, THUMB, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            r.render(&p, QRectF((THUMB - s.width()) / 2, (THUMB - s.height()) / 2, s.width(), s.height()));
        }
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QSize s = reader.size();
        if (s.isValid()) {
            s.scale(THUMB, THUMB, Qt::KeepAspectRatio);
            reader.setScaledSize(s.expandedTo(QSize(1, 1)));
        }
        img = reader.read();
    }
    QIcon icon;
    if (!img.isNull())
        icon = QIcon(QPixmap::fromImage(img));
    cache.insert(key, icon);
    return icon;
}
} // namespace

ClipartPanel::ClipartPanel(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Поиск по названию"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    lay->addWidget(m_search);

    m_tabs = new QTabBar;
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setExpanding(false);
    m_tabs->setDocumentMode(true);
    m_tabs->setElideMode(Qt::ElideNone);
    connect(m_tabs, &QTabBar::currentChanged, this, [this] { applyFilter(); });
    lay->addWidget(m_tabs);

    m_list = new ClipartList;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(THUMB, THUMB));
    m_list->setGridSize(QSize(THUMB + 40, THUMB + 40));  // room for two lines of the name
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setDefaultDropAction(Qt::CopyAction);
    connect(m_list, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* it) { emit insertRequested(it->data(Qt::UserRole).toString()); });
    lay->addWidget(m_list, 1);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    auto button = [this, buttons](const QString& text, const QString& tip, auto fn) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        buttons->addWidget(b);
    };
    button(tr("Открыть папку"), tr("Папка для своих картинок: подпапки становятся вкладками"),
           [this] { openUserFolder(); });
    button(tr("Обновить"), tr("Перечитать папки клипарта"), [this] { rescan(); });
    buttons->addStretch();
    button(tr("Где взять ещё…"), tr("Бесплатные коллекции значков и картинок"), [this] { showSources(); });
    lay->addLayout(buttons);

    rescan();
}

QString ClipartPanel::builtinDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/clipart");
}

QString ClipartPanel::userDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/clipart");
}

void ClipartPanel::rescan()
{
    QStringList patterns{QStringLiteral("*.svg"), QStringLiteral("*.svgz")};
    for (const QByteArray& fmt : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(fmt);
    patterns.removeDuplicates();
    const QDir::SortFlags sort = QDir::Name | QDir::IgnoreCase | QDir::LocaleAware;

    m_entries.clear();
    QStringList cats;
    auto addFiles = [&](const QDir& dir, const QString& category) {
        for (const QFileInfo& fi : dir.entryInfoList(patterns, QDir::Files | QDir::Readable, sort))
            m_entries.append({fi.absoluteFilePath(), fi.completeBaseName(), category});
        if (!cats.contains(category) && !dir.entryList(patterns, QDir::Files).isEmpty())
            cats << category;
    };
    for (const QString& base : {builtinDir(), userDir()}) {
        const QDir root(base);
        if (!root.exists())
            continue;
        addFiles(root, tr("Разное"));
        for (const QFileInfo& sub : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, sort))
            addFiles(QDir(sub.absoluteFilePath()), sub.fileName());
    }
    std::sort(cats.begin(), cats.end(),
              [](const QString& a, const QString& b) { return QString::localeAwareCompare(a, b) < 0; });
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });

    const QString current = m_tabs->currentIndex() > 0 ? m_tabs->tabText(m_tabs->currentIndex()) : QString();
    m_categories = cats;
    const QSignalBlocker block(m_tabs);
    while (m_tabs->count())
        m_tabs->removeTab(0);
    m_tabs->addTab(tr("Все"));
    for (const QString& c : m_categories)
        m_tabs->addTab(c);
    m_tabs->setCurrentIndex(std::max(0, int(m_categories.indexOf(current)) + 1));
    applyFilter();
}

void ClipartPanel::applyFilter()
{
    const QString text = m_search->text().trimmed();
    const int tab = m_tabs->currentIndex();
    const QString category = tab > 0 ? m_tabs->tabText(tab) : QString();
    m_list->clear();
    for (const Entry& e : m_entries) {
        if (!category.isEmpty() && e.category != category)
            continue;
        if (!text.isEmpty() && !e.name.contains(text, Qt::CaseInsensitive))
            continue;
        auto* it = new QListWidgetItem(thumbnail(e.path), e.name);
        it->setData(Qt::UserRole, e.path);
        it->setToolTip(e.name + QLatin1Char('\n') + QDir::toNativeSeparators(e.path));
        m_list->addItem(it);
    }
}

int ClipartPanel::visibleCount() const { return m_list->count(); }

void ClipartPanel::setFilter(const QString& text) { m_search->setText(text); }

void ClipartPanel::setCategory(int index) { m_tabs->setCurrentIndex(index); }

void ClipartPanel::openUserFolder()
{
    const QString d = userDir();
    QDir().mkpath(d);
    QDesktopServices::openUrl(QUrl::fromLocalFile(d));
}

void ClipartPanel::showSources()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Где взять ещё картинки"));
    box.setIcon(QMessageBox::Information);
    box.setTextFormat(Qt::RichText);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.setText(
        tr("<p>Скачанные SVG и PNG положите в папку клипарта (кнопка «Открыть папку»), "
           "по подпапкам — они станут вкладками — и нажмите «Обновить».</p>"
           "<ul>"
           "<li><a href=\"https://tabler.io/icons\">Tabler Icons</a> — тысячи значков, лицензия MIT</li>"
           "<li><a href=\"https://fonts.google.com/icons\">Google Material Symbols</a> — значки, лицензия Apache 2.0</li>"
           "<li><a href=\"https://openclipart.org\">Openclipart</a> — клипарт, общественное достояние (CC0)</li>"
           "<li><a href=\"https://commons.wikimedia.org/wiki/Category:GHS_pictograms\">Wikimedia Commons</a> — "
           "знаки GHS, ISO 7010 и многое другое; лицензия указана на странице каждого файла</li>"
           "<li><a href=\"https://openmoji.org\">OpenMoji</a> — эмодзи, CC BY-SA 4.0: нужно указывать авторов</li>"
           "<li><a href=\"https://www.svgrepo.com\">SVG Repo</a> — большая подборка, но лицензии разные: "
           "проверяйте у каждого значка</li>"
           "</ul>"
           "<p>Для перекрашивания и чётких краёв на термопринтере лучше подходят одноцветные SVG.</p>"));
    box.exec();
}

} // namespace lf
