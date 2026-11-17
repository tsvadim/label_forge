// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClipartPanel.h"

#include "IconDownloadDialog.h"
#include "Icons.h"
#include "core/IconWords.h"

#include <QAbstractListModel>
#include <QCollator>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QImageReader>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTabBar>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace lf {

namespace {
const int THUMB = 48;
const QSize GRID(THUMB + 40, THUMB + 40);  // room for two lines of the name
const QString LOOSE = QStringLiteral("\x01loose");  // section item: files directly in the folder

std::atomic<int> g_rendered{0};

bool isSvgFile(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("svg") || s == QLatin1String("svgz");
}

// Runs in a worker thread: only QImage, no QPixmap.
QImage renderThumbnail(const QString& path)
{
    // the picture sits on a light card, whatever the theme
    QImage img = icons::thumbnailTile(THUMB);
    const QRectF inner = icons::tileInner(THUMB);
    QPainter p(&img);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    bool ok = false;
    if (isSvgFile(path)) {
        QSvgRenderer r(path);
        if (r.isValid()) {
            QSizeF s = r.defaultSize();
            if (s.isEmpty())
                s = r.viewBoxF().size();
            if (s.isEmpty())
                s = QSizeF(1, 1);
            s.scale(inner.size(), Qt::KeepAspectRatio);
            r.render(&p, QRectF(inner.center() - QPointF(s.width() / 2, s.height() / 2), s));
            ok = true;
        }
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QSize s = reader.size();
        if (s.isValid()) {
            s.scale(inner.size().toSize(), Qt::KeepAspectRatio);
            reader.setScaledSize(s.expandedTo(QSize(1, 1)));
        }
        const QImage pic = reader.read();
        if (!pic.isNull()) {
            p.drawImage(QRectF(inner.center() - QPointF(pic.width() / 2.0, pic.height() / 2.0), QSizeF(pic.size())), pic);
            ok = true;
        }
    }
    p.end();
    ++g_rendered;
    return ok ? img : QImage();
}
} // namespace

// Rows of the visible pictures. Thumbnails are requested by the view only
// for the rows it paints (all items have one size, so the view does not ask
// the others) and rendered in a thread pool; until then a blank is shown.
class ClipartModel : public QAbstractListModel {
public:
    struct Row {
        QString path, name, key;
    };

    explicit ClipartModel(QObject* parent) : QAbstractListModel(parent)
    {
        m_pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
        QPixmap blank(THUMB, THUMB);
        blank.fill(Qt::transparent);
        m_blank = blank;
    }
    ~ClipartModel() override
    {
        m_pool.clear();
        m_pool.waitForDone();
    }

    void setRows(QList<Row> rows)
    {
        // pictures that scrolled away or were filtered out need not be rendered
        m_pool.clear();
        m_pending.clear();
        beginResetModel();
        m_rows = std::move(rows);
        m_rowOf.clear();
        for (int i = 0; i < m_rows.size(); ++i)
            m_rowOf.insert(m_rows.at(i).key, i);
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size())
            return QVariant();
        const Row& r = m_rows.at(index.row());
        switch (role) {
        case Qt::DisplayRole: return r.name;
        case Qt::ToolTipRole: return r.name + QLatin1Char('\n') + QDir::toNativeSeparators(r.path);
        case Qt::UserRole: return r.path;
        case Qt::SizeHintRole: return QSize(GRID.width() - 4, GRID.height() - 4);
        case Qt::DecorationRole: {
            const auto it = s_cache.constFind(r.key);
            if (it != s_cache.constEnd())
                return *it;
            request(r);
            return m_blank;
        }
        default: return QVariant();
        }
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled : Qt::NoItemFlags;
    }

    // dragged out as file URLs, so the label view (and other programs) see an ordinary file drop
    QStringList mimeTypes() const override { return {QStringLiteral("text/uri-list")}; }
    QMimeData* mimeData(const QModelIndexList& indexes) const override
    {
        QList<QUrl> urls;
        for (const QModelIndex& i : indexes)
            urls << QUrl::fromLocalFile(data(i, Qt::UserRole).toString());
        auto* mime = new QMimeData;
        mime->setUrls(urls);
        return mime;
    }

private:
    void request(const Row& r) const
    {
        if (m_pending.contains(r.key))
            return;
        m_pending.insert(r.key);
        auto* self = const_cast<ClipartModel*>(this);
        const QString path = r.path, key = r.key;
        m_pool.start([self, path, key] {
            const QImage img = renderThumbnail(path);
            QMetaObject::invokeMethod(self, [self, key, img] { self->ready(key, img); }, Qt::QueuedConnection);
        });
    }

    void ready(const QString& key, const QImage& img)
    {
        m_pending.remove(key);
        // thumbnails survive rescans; a changed file gets a new key
        s_cache.insert(key, img.isNull() ? m_blank : QPixmap::fromImage(img));
        const int row = m_rowOf.value(key, -1);
        if (row >= 0) {
            const QModelIndex i = index(row);
            emit dataChanged(i, i, {Qt::DecorationRole});
        }
    }

    QList<Row> m_rows;
    QHash<QString, int> m_rowOf;
    QPixmap m_blank;
    mutable QThreadPool m_pool;
    mutable QSet<QString> m_pending;
    static QHash<QString, QPixmap> s_cache;
};

QHash<QString, QPixmap> ClipartModel::s_cache;

ClipartPanel::ClipartPanel(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(2);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Поиск по названию"));
    m_search->setToolTip(tr("Можно искать по-русски: «коробка» найдёт и значки box, package"));
    m_search->setClearButtonEnabled(true);
    auto* filterTimer = new QTimer(this);
    filterTimer->setSingleShot(true);
    filterTimer->setInterval(150);
    connect(filterTimer, &QTimer::timeout, this, [this] { applyFilter(); });
    connect(m_search, &QLineEdit::textChanged, filterTimer, qOverload<>(&QTimer::start));
    searchRow->addWidget(m_search, 1);
    auto* web = new QToolButton;
    web->setText(tr("В интернете"));
    web->setToolTip(tr("Искать этот значок в свободных наборах Iconify и скачать в галерею"));
    web->setAutoRaise(true);
    connect(web, &QToolButton::clicked, this, [this] { openDownloads(m_search->text().trimmed()); });
    searchRow->addWidget(web);
    lay->addLayout(searchRow);

    m_tabs = new QTabBar;
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setExpanding(false);
    m_tabs->setDocumentMode(true);
    m_tabs->setElideMode(Qt::ElideNone);
    connect(m_tabs, &QTabBar::currentChanged, this, [this] {
        updateSections();
        applyFilter();
    });
    // right click on a tab: remove a downloaded set or the user's own folder
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int i = m_tabs->tabAt(pos);
        if (i <= 0)
            return;
        const QString name = m_tabs->tabText(i);
        QMenu menu(this);
        if (canRemoveCategory(name)) {
            menu.addAction(tr("Открыть папку"), this, [name] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(userDir() + QLatin1Char('/') + name));
            });
            menu.addSeparator();
            menu.addAction(tr("Удалить вкладку «%1»…").arg(name), this, [this, name] { removeCategory(name); });
        } else {
            menu.addAction(tr("Встроенную вкладку удалить нельзя"))->setEnabled(false);
        }
        menu.exec(m_tabs->mapToGlobal(pos));
    });
    lay->addWidget(m_tabs);

    m_section = new QComboBox;
    m_section->setToolTip(tr("Раздел набора"));
    m_section->setVisible(false);
    connect(m_section, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyFilter(); });
    lay->addWidget(m_section);

    m_model = new ClipartModel(this);
    m_list = new QListView;
    m_list->setObjectName(QStringLiteral("clipartList"));
    m_list->setModel(m_model);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(THUMB, THUMB));
    m_list->setGridSize(GRID);
    m_list->setUniformItemSizes(true);  // the view then asks only the rows it shows
    m_list->setResizeMode(QListView::Adjust);
    m_list->setLayoutMode(QListView::Batched);
    m_list->setBatchSize(200);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setDefaultDropAction(Qt::CopyAction);
    connect(m_list, &QListView::activated, this,
            [this](const QModelIndex& i) { emit insertRequested(i.data(Qt::UserRole).toString()); });
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
        return b;
    };
    button(tr("Скачать значки…"), tr("Найти значки или загрузить целый набор из свободных коллекций Iconify"),
           [this] { openDownloads(); })
        ->setObjectName(QStringLiteral("clipartDownload"));
    buttons->addStretch();
    button(tr("Открыть папку"), tr("Папка для своих картинок: подпапки становятся вкладками"),
           [this] { openUserFolder(); });
    button(tr("Обновить"), tr("Перечитать папки клипарта"), [this] { rescan(); });
    lay->addLayout(buttons);

    rescan();
}

ClipartPanel::~ClipartPanel() = default;

QString ClipartPanel::builtinDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/clipart");
}

QString ClipartPanel::userDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/clipart");
}

int ClipartPanel::thumbnailsRendered() { return g_rendered; }

void ClipartPanel::rescan()
{
    QStringList patterns{QStringLiteral("*.svg"), QStringLiteral("*.svgz")};
    for (const QByteArray& fmt : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(fmt);
    patterns.removeDuplicates();
    const QDir::Filters files = QDir::Files | QDir::Readable;
    const QDir::Filters dirs = QDir::Dirs | QDir::NoDotAndDotDot;

    m_entries.clear();
    QSet<QString> cats;
    auto addFiles = [&](const QString& dir, const QString& category, const QString& section) {
        for (const QFileInfo& fi : QDir(dir).entryInfoList(patterns, files, QDir::NoSort)) {
            const QString stamp =
                QString::number(fi.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|') + QString::number(fi.size());
            m_entries.append({fi.absoluteFilePath(), fi.completeBaseName(), category, section, stamp});
            cats.insert(category);
        }
    };
    for (const QString& base : {builtinDir(), userDir()}) {
        const QDir root(base);
        if (!root.exists())
            continue;
        addFiles(base, tr("Разное"), QString());
        for (const QFileInfo& cat : root.entryInfoList(dirs, QDir::NoSort)) {
            addFiles(cat.absoluteFilePath(), cat.fileName(), QString());
            for (const QFileInfo& sec : QDir(cat.absoluteFilePath()).entryInfoList(dirs, QDir::NoSort))
                addFiles(sec.absoluteFilePath(), cat.fileName(), sec.fileName());
        }
    }
    // sort keys once: locale-aware comparison is too slow for thousands of names
    QCollator coll;
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    coll.setNumericMode(true);
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [&coll](const Entry& a, const Entry& b) { return coll.compare(a.name, b.name) < 0; });
    QStringList sorted(cats.begin(), cats.end());
    std::sort(sorted.begin(), sorted.end(), [&coll](const QString& a, const QString& b) { return coll.compare(a, b) < 0; });

    const QString current = m_tabs->currentIndex() > 0 ? m_tabs->tabText(m_tabs->currentIndex()) : QString();
    m_categories = sorted;
    {
        const QSignalBlocker block(m_tabs);
        while (m_tabs->count())
            m_tabs->removeTab(0);
        m_tabs->addTab(tr("Все"));
        for (const QString& c : m_categories)
            m_tabs->addTab(c);
        m_tabs->setCurrentIndex(std::max(0, int(m_categories.indexOf(current)) + 1));
    }
    updateSections();
    applyFilter();
}

QStringList ClipartPanel::sections() const
{
    QStringList out;
    for (int i = 1; i < m_section->count(); ++i)
        if (m_section->itemData(i).toString() != LOOSE)
            out << m_section->itemData(i).toString();
    return out;
}

void ClipartPanel::updateSections()
{
    const int tab = m_tabs->currentIndex();
    const QString category = tab > 0 ? m_tabs->tabText(tab) : QString();
    const QString current = m_section->currentData().toString();
    QSet<QString> secs;
    bool loose = false;  // files directly in the category folder
    if (!category.isEmpty())
        for (const Entry& e : m_entries)
            if (e.category == category) {
                if (e.section.isEmpty())
                    loose = true;
                else
                    secs.insert(e.section);
            }
    QStringList sorted(secs.begin(), secs.end());
    QCollator coll;
    coll.setNumericMode(true);
    std::sort(sorted.begin(), sorted.end(), [&coll](const QString& a, const QString& b) { return coll.compare(a, b) < 0; });

    const QSignalBlocker block(m_section);
    m_section->clear();
    m_section->addItem(tr("Все разделы"), QVariant());
    for (const QString& s : sorted)
        m_section->addItem(s, s);
    if (loose && !sorted.isEmpty())
        m_section->addItem(tr("Без раздела"), LOOSE);
    const int i = m_section->findData(current);
    m_section->setCurrentIndex(i > 0 ? i : 0);
    m_section->setVisible(!sorted.isEmpty());
}

void ClipartPanel::applyFilter()
{
    const iconwords::Matcher match(m_search->text().trimmed());
    const int tab = m_tabs->currentIndex();
    const QString category = tab > 0 ? m_tabs->tabText(tab) : QString();
    const QVariant secData = m_section->isHidden() ? QVariant() : m_section->currentData();
    const bool anySection = !secData.isValid();
    const QString section = secData.toString() == LOOSE ? QString() : secData.toString();
    QList<ClipartModel::Row> rows;
    for (const Entry& e : m_entries) {
        if (!category.isEmpty() && e.category != category)
            continue;
        if (!anySection && e.section != section)
            continue;
        if (!match.isEmpty() && !match(e.name))
            continue;
        rows.append({e.path, e.name, e.path + QLatin1Char('|') + e.stamp});
    }
    m_model->setRows(std::move(rows));
}

int ClipartPanel::visibleCount() const { return m_model->rowCount(); }

void ClipartPanel::setFilter(const QString& text)
{
    m_search->setText(text);
    applyFilter();  // at once, without the typing delay
}

void ClipartPanel::setCategory(int index) { m_tabs->setCurrentIndex(index); }

void ClipartPanel::setSection(int index) { m_section->setCurrentIndex(index); }

IconDownloadDialog* ClipartPanel::openDownloads(const QString& query)
{
    if (!m_downloads) {
        m_downloads = new IconDownloadDialog(userDir(), window());
        // new files appear in the panel while the window stays open
        auto* debounce = new QTimer(m_downloads);
        debounce->setSingleShot(true);
        debounce->setInterval(300);
        connect(debounce, &QTimer::timeout, this, [this] { rescan(); });
        connect(m_downloads, &IconDownloadDialog::galleryChanged, debounce, qOverload<>(&QTimer::start));
    }
    if (!query.isEmpty())
        m_downloads->search(query);
    m_downloads->show();
    m_downloads->raise();
    m_downloads->activateWindow();
    return m_downloads;
}

void ClipartPanel::openUserFolder()
{
    const QString d = userDir();
    QDir().mkpath(d);
    QDesktopServices::openUrl(QUrl::fromLocalFile(d));
}

bool ClipartPanel::canRemoveCategory(const QString& name)
{
    return !name.isEmpty() && QFileInfo(userDir() + QLatin1Char('/') + name).isDir();
}

bool ClipartPanel::removeCategory(const QString& name, bool confirm)
{
    if (!canRemoveCategory(name))
        return false;
    const bool ok = removeGalleryFolder(this, userDir() + QLatin1Char('/') + name, confirm);
    if (ok)
        rescan();
    return ok;
}

bool ClipartPanel::removeGalleryFolder(QWidget* parent, const QString& path, bool confirm)
{
    const QFileInfo fi(path);
    if (!fi.isDir())
        return false;
    // never anything outside the user's gallery
    const QString root = QDir(userDir()).canonicalPath();
    if (root.isEmpty() || !fi.canonicalFilePath().startsWith(root + QLatin1Char('/')))
        return false;
    const QString name = fi.fileName();

    if (confirm) {
        int pictures = 0;
        for (QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            it.next();
            const QString s = it.fileInfo().suffix().toLower();
            if (s != QLatin1String("txt") && s != QLatin1String("json"))
                ++pictures;
        }
        QString text = tr("Удалить «%1» из галереи?\n\nКартинок: %2. Папка будет перемещена в Корзину, оттуда её "
                          "можно восстановить. Этикетки, в которые эти картинки уже вставлены, не изменятся.")
                           .arg(name)
                           .arg(pictures);
        if (QFileInfo(builtinDir() + QLatin1Char('/') + name).isDir())
            text += QLatin1Char('\n') + tr("Встроенные картинки этой вкладки останутся.");
        if (QMessageBox::question(parent, tr("Удалить из галереи"), text, QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return false;
    }
    if (QFile::moveToTrash(path))
        return true;
    // no recycle bin (network drive, unusual setup): delete for good only after a second question
    if (!confirm ||
        QMessageBox::warning(parent, tr("Удалить из галереи"),
                             tr("Не получилось переместить «%1» в Корзину.\nУдалить папку окончательно? "
                                "Восстановить её будет нельзя.")
                                 .arg(name),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return false;
    if (QDir(path).removeRecursively())
        return true;
    QMessageBox::warning(parent, tr("Удалить из галереи"),
                         tr("Не удалось удалить папку «%1» полностью: возможно, какие-то файлы открыты в другой "
                            "программе.")
                             .arg(QDir::toNativeSeparators(path)));
    return false;
}

} // namespace lf
