// SPDX-License-Identifier: GPL-3.0-or-later
#include "IconDownloadDialog.h"

#include "core/IconWords.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace lf {

namespace {
const int THUMB = 48;
const int MAX_RESULTS = 300;  // icons shown for one search
const int BATCH = 100;        // icons per request when downloading a set
const char* const MARKER = "iconify-set.json";

enum Roles { SvgRole = Qt::UserRole, PrefixRole, NameRole };

QIcon svgIcon(const QByteArray& svg)
{
    QSvgRenderer r(svg);
    QImage img(THUMB, THUMB, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (r.isValid()) {
        QSizeF s = r.viewBoxF().size();
        if (s.isEmpty())
            s = QSizeF(1, 1);
        s.scale(THUMB - 8, THUMB - 8, Qt::KeepAspectRatio);
        QPainter p(&img);
        r.render(&p, QRectF((THUMB - s.width()) / 2, (THUMB - s.height()) / 2, s.width(), s.height()));
    }
    return QIcon(QPixmap::fromImage(img));
}

// Writes one icon unless a file of that name exists; true if written.
bool writeIcon(const QString& folder, const QString& name, const QByteArray& svg)
{
    const QString path = folder + QLatin1Char('/') + iconify::safeFileName(name) + QStringLiteral(".svg");
    if (QFileInfo::exists(path))
        return false;
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(svg) == svg.size();
}

void writeLicense(const QString& folder, const iconify::Collection& c)
{
    QFile f(folder + QStringLiteral("/LICENSE.txt"));
    if (!f.exists() && f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(iconify::licenseNote(c).toUtf8());
}
} // namespace

IconDownloadDialog::IconDownloadDialog(const QString& galleryDir, QWidget* parent)
    : QDialog(parent), m_dir(galleryDir), m_net(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("Скачать значки"));
    resize(680, 560);
    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Значки из свободных наборов каталога Iconify. Показаны только наборы, которые можно "
                                "печатать на этикетках для продажи без указания автора (MIT, Apache, ISC, BSD, CC0, "
                                "OFL). Скачанное попадает в галерею клипарта: каждый набор — своя вкладка."));
    intro->setWordWrap(true);
    lay->addWidget(intro);
    m_tabs = new QTabWidget;
    lay->addWidget(m_tabs, 1);

    // ---- search
    auto* searchPage = new QWidget;
    auto* sl = new QVBoxLayout(searchPage);
    auto* row = new QHBoxLayout;
    m_query = new QLineEdit;
    m_query->setPlaceholderText(tr("Например: коробка, стрелка, box, fragile"));
    m_query->setClearButtonEnabled(true);
    auto* find = new QPushButton(tr("Найти"));
    find->setDefault(true);
    connect(m_query, &QLineEdit::returnPressed, this, &IconDownloadDialog::runSearch);
    connect(find, &QPushButton::clicked, this, &IconDownloadDialog::runSearch);
    row->addWidget(m_query, 1);
    row->addWidget(find);
    sl->addLayout(row);
    m_searchStatus = new QLabel(tr("Названия значков английские; частые русские слова переводятся сами."));
    m_searchStatus->setWordWrap(true);
    sl->addWidget(m_searchStatus);
    m_results = new QListWidget;
    m_results->setObjectName(QStringLiteral("iconResults"));
    m_results->setViewMode(QListView::IconMode);
    m_results->setIconSize(QSize(THUMB, THUMB));
    m_results->setGridSize(QSize(THUMB + 44, THUMB + 36));
    m_results->setUniformItemSizes(true);
    m_results->setResizeMode(QListView::Adjust);
    m_results->setMovement(QListView::Static);
    m_results->setWordWrap(true);
    m_results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_results, &QListWidget::itemSelectionChanged, this, &IconDownloadDialog::updateAddButton);
    connect(m_results, &QListWidget::itemDoubleClicked, this, [this] { addSelected(); });
    sl->addWidget(m_results, 1);
    auto* addRow = new QHBoxLayout;
    auto* hint = new QLabel(tr("Выделите значки (Ctrl, Shift — несколько) и добавьте их в галерею."));
    hint->setWordWrap(true);
    addRow->addWidget(hint, 1);
    m_add = new QPushButton(tr("Добавить в галерею"));
    m_add->setEnabled(false);
    connect(m_add, &QPushButton::clicked, this, &IconDownloadDialog::addSelected);
    addRow->addWidget(m_add);
    sl->addLayout(addRow);
    m_tabs->addTab(searchPage, tr("Поиск значков"));

    // ---- sets
    auto* setsPage = new QWidget;
    auto* tl = new QVBoxLayout(setsPage);
    m_setFilter = new QLineEdit;
    m_setFilter->setPlaceholderText(tr("Фильтр по названию набора"));
    m_setFilter->setClearButtonEnabled(true);
    connect(m_setFilter, &QLineEdit::textChanged, this, &IconDownloadDialog::fillSets);
    tl->addWidget(m_setFilter);
    m_sets = new QTableWidget(0, 5);
    m_sets->setObjectName(QStringLiteral("iconSets"));
    m_sets->setHorizontalHeaderLabels({tr("Набор"), tr("Значков"), tr("Лицензия"), tr("Вид"), tr("В галерее")});
    m_sets->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sets->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sets->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sets->verticalHeader()->setVisible(false);
    m_sets->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 5; ++c)
        m_sets->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    connect(m_sets, &QTableWidget::itemSelectionChanged, this,
            [this] { m_download->setEnabled(!m_jobActive && m_sets->currentRow() >= 0); });
    connect(m_sets, &QTableWidget::cellDoubleClicked, this, [this](int r) {
        downloadSet(m_sets->item(r, 0)->data(Qt::UserRole).toString());
    });
    tl->addWidget(m_sets, 1);
    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    tl->addWidget(m_progress);
    auto* setRow = new QHBoxLayout;
    m_setStatus = new QLabel(tr("Большие наборы раскладываются по разделам, чтобы в одной папке не было тысяч файлов. "
                                "Уже скачанные значки не перезаписываются."));
    m_setStatus->setWordWrap(true);
    setRow->addWidget(m_setStatus, 1);
    m_cancel = new QPushButton(tr("Остановить"));
    m_cancel->setVisible(false);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_cancelRequested = true;
        if (m_jobReply)
            m_jobReply->abort();
        if (m_jobActive)
            finishSet(true);
    });
    setRow->addWidget(m_cancel);
    m_download = new QPushButton(tr("Загрузить набор"));
    m_download->setEnabled(false);
    connect(m_download, &QPushButton::clicked, this, [this] {
        if (m_sets->currentRow() >= 0)
            downloadSet(m_sets->item(m_sets->currentRow(), 0)->data(Qt::UserRole).toString());
    });
    setRow->addWidget(m_download);
    tl->addLayout(setRow);
    m_tabs->addTab(setsPage, tr("Наборы целиком"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == 1)
            loadSets();
    });

    auto* close = new QPushButton(tr("Закрыть"));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    auto* bottom = new QHBoxLayout;
    bottom->addStretch();
    bottom->addWidget(close);
    lay->addLayout(bottom);
}

QString IconDownloadDialog::apiBase()
{
    QString base = qEnvironmentVariable("LABELFORGE_ICONIFY_URL", QStringLiteral("https://api.iconify.design"));
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

QString IconDownloadDialog::setFolder(const QString& galleryDir, const iconify::Collection& c)
{
    return galleryDir + QLatin1Char('/') + iconify::safeFileName(c.name.isEmpty() ? c.prefix : c.name);
}

QNetworkReply* IconDownloadDialog::get(const QString& pathAndQuery, Handler done)
{
    QNetworkRequest req(QUrl(apiBase() + pathAndQuery));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LabelForge"));
    req.setTransferTimeout(30000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;  // aborted on purpose: a newer search, Stop, or the window closed
        const QByteArray body = reply->readAll();
        QString err;
        if (reply->error() != QNetworkReply::NoError)
            err = reply->errorString();
        done(body, err);
    });
    return reply;
}

// ------------------------------------------------------------- search

void IconDownloadDialog::search(const QString& query)
{
    m_tabs->setCurrentIndex(0);
    m_query->setText(query);
    runSearch();
}

void IconDownloadDialog::runSearch()
{
    const QString text = m_query->text().trimmed();
    if (text.isEmpty())
        return;
    const QString q = iconwords::englishQuery(text);
    if (iconwords::hasCyrillic(q)) {
        m_searchStatus->setText(
            tr("Слова «%1» нет в словаре. Названия значков английские — попробуйте по-английски, например: box, "
               "arrow, fire, truck.")
                .arg(text));
        return;
    }
    if (m_reply)
        m_reply->abort();
    m_results->clear();
    m_previewQueue.clear();
    m_previewNames.clear();
    m_searchStatus->setText(q == text ? tr("Ищу «%1»…").arg(q) : tr("Ищу «%1» (%2)…").arg(q, text));
    m_reply = get(QStringLiteral("/search?limit=999&query=") + QString::fromLatin1(QUrl::toPercentEncoding(q)),
                  [this, q](const QByteArray& body, const QString& err) {
                      if (!err.isEmpty()) {
                          m_searchStatus->setText(tr("Не удалось связаться с каталогом Iconify: %1").arg(err));
                          return;
                      }
                      const iconify::SearchResult r = iconify::parseSearch(body);
                      m_searchCollections = r.collections;
                      const QStringList ids = r.icons.mid(0, MAX_RESULTS);
                      if (ids.isEmpty()) {
                          m_searchStatus->setText(tr("По запросу «%1» ничего не найдено в свободных наборах.").arg(q));
                          return;
                      }
                      for (const QString& id : ids) {
                          const QString prefix = id.section(QLatin1Char(':'), 0, 0);
                          if (!m_previewNames.contains(prefix))
                              m_previewQueue << prefix;
                          m_previewNames[prefix] << id.section(QLatin1Char(':'), 1);
                      }
                      m_searchStatus->setText(
                          tr("Найдено %1 в %2 наборах, загружаю картинки…").arg(ids.size()).arg(m_previewQueue.size()));
                      loadNextPreview();
                  });
}

void IconDownloadDialog::loadNextPreview()
{
    if (m_previewQueue.isEmpty()) {
        m_searchStatus->setText(tr("Найдено значков: %1. Наведите курсор, чтобы увидеть набор и лицензию.")
                                    .arg(m_results->count()));
        return;
    }
    const QString prefix = m_previewQueue.takeFirst();
    const QStringList names = m_previewNames.value(prefix);
    m_reply = get(QStringLiteral("/%1.json?icons=%2").arg(prefix, names.join(QLatin1Char(','))),
                  [this, prefix, names](const QByteArray& body, const QString& err) {
                      if (!err.isEmpty()) {
                          m_searchStatus->setText(tr("Не удалось загрузить картинки: %1").arg(err));
                          return;
                      }
                      const QHash<QString, QByteArray> svgs = iconify::parseIconSet(body);
                      const iconify::Collection c = m_searchCollections.value(prefix);
                      for (const QString& n : names) {
                          const QByteArray svg = svgs.value(n);
                          if (svg.isEmpty())
                              continue;
                          auto* it = new QListWidgetItem(svgIcon(svg), n);
                          it->setData(SvgRole, svg);
                          it->setData(PrefixRole, prefix);
                          it->setData(NameRole, n);
                          it->setToolTip(QStringLiteral("%1\n%2 — %3").arg(n, c.name, c.license));
                          m_results->addItem(it);
                      }
                      loadNextPreview();
                  });
}

int IconDownloadDialog::resultCount() const { return m_results->count(); }

void IconDownloadDialog::selectAllResults() { m_results->selectAll(); }

void IconDownloadDialog::updateAddButton()
{
    const int n = int(m_results->selectedItems().size());
    m_add->setEnabled(n > 0);
    m_add->setText(n > 1 ? tr("Добавить в галерею (%1)").arg(n) : tr("Добавить в галерею"));
}

void IconDownloadDialog::addSelected()
{
    int added = 0, existed = 0;
    QStringList sets;
    for (const QListWidgetItem* it : m_results->selectedItems()) {
        const iconify::Collection c = m_searchCollections.value(it->data(PrefixRole).toString());
        const QString folder = setFolder(m_dir, c);
        if (!QDir().mkpath(folder))
            continue;
        writeLicense(folder, c);
        if (writeIcon(folder, it->data(NameRole).toString(), it->data(SvgRole).toByteArray())) {
            ++added;
            if (!sets.contains(QFileInfo(folder).fileName()))
                sets << QFileInfo(folder).fileName();
        } else {
            ++existed;
        }
    }
    QString msg = added ? tr("Добавлено в галерею: %1 (вкладки: %2).").arg(added).arg(sets.join(QStringLiteral(", ")))
                        : tr("Ничего не добавлено.");
    if (existed)
        msg += QLatin1Char(' ') + tr("Уже были в галерее: %1.").arg(existed);
    m_searchStatus->setText(msg);
    if (added)
        emit galleryChanged();
}

// ------------------------------------------------------------- sets

void IconDownloadDialog::showSets()
{
    m_tabs->setCurrentIndex(1);
    loadSets();
}

void IconDownloadDialog::loadSets()
{
    if (m_setsLoaded || m_listReply)
        return;
    m_setStatus->setText(tr("Загружаю список наборов…"));
    m_listReply = get(QStringLiteral("/collections"), [this](const QByteArray& body, const QString& err) {
        if (!err.isEmpty()) {
            m_setStatus->setText(tr("Не удалось получить список наборов: %1").arg(err));
            return;
        }
        m_collections.clear();
        for (const iconify::Collection& c : iconify::parseCollections(body))
            if (iconify::isFreeLicense(c.licenseSpdx))
                m_collections << c;
        m_setsLoaded = true;
        m_setStatus->setText(tr("Свободных наборов: %1. Выберите набор и нажмите «Загрузить набор».")
                                 .arg(m_collections.size()));
        fillSets();
    });
}

bool IconDownloadDialog::isSetComplete(const iconify::Collection& c) const
{
    QFile f(setFolder(m_dir, c) + QLatin1Char('/') + QLatin1String(MARKER));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("prefix")).toString() == c.prefix;
}

void IconDownloadDialog::fillSets()
{
    const QString filter = m_setFilter->text().trimmed();
    m_sets->setRowCount(0);
    for (const iconify::Collection& c : m_collections) {
        if (!filter.isEmpty() && !c.name.contains(filter, Qt::CaseInsensitive) &&
            !c.prefix.contains(filter, Qt::CaseInsensitive) && !c.category.contains(filter, Qt::CaseInsensitive))
            continue;
        const int r = m_sets->rowCount();
        m_sets->insertRow(r);
        auto* name = new QTableWidgetItem(c.name);
        name->setData(Qt::UserRole, c.prefix);
        name->setToolTip(c.author.isEmpty() ? c.prefix : tr("%1, автор: %2").arg(c.prefix, c.author));
        m_sets->setItem(r, 0, name);
        auto* count = new QTableWidgetItem(QString::number(c.total));
        count->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_sets->setItem(r, 1, count);
        m_sets->setItem(r, 2, new QTableWidgetItem(c.license.isEmpty() ? c.licenseSpdx : c.license));
        m_sets->setItem(r, 3, new QTableWidgetItem(c.palette ? tr("%1, цветные").arg(c.category) : c.category));
        m_sets->setItem(r, 4, new QTableWidgetItem(isSetComplete(c) ? tr("да") : QString()));
    }
    m_download->setEnabled(!m_jobActive && m_sets->currentRow() >= 0);
}

int IconDownloadDialog::setCount() const { return m_sets->rowCount(); }

bool IconDownloadDialog::downloadSet(const QString& prefix)
{
    if (m_jobActive)
        return false;
    for (const iconify::Collection& c : m_collections)
        if (c.prefix == prefix) {
            startSetDownload(c);
            return true;
        }
    return false;
}

void IconDownloadDialog::startSetDownload(const iconify::Collection& c)
{
    m_job = c;
    m_jobActive = true;
    m_cancelRequested = false;
    m_jobTotal = m_jobDone = m_jobAdded = 0;
    m_batches.clear();
    m_download->setEnabled(false);
    m_cancel->setVisible(true);
    m_progress->setVisible(true);
    m_progress->setRange(0, 0);
    m_setStatus->setText(tr("Получаю список значков набора «%1»…").arg(c.name));
    m_jobReply = get(QStringLiteral("/collection?prefix=") + c.prefix, [this](const QByteArray& body, const QString& err) {
        if (!err.isEmpty()) {
            if (!m_cancelRequested)
                m_setStatus->setText(tr("Не удалось получить набор: %1").arg(err));
            finishSet(true);
            return;
        }
        const QMap<QString, QStringList> folders = iconify::parseCollectionFolders(body);
        for (auto it = folders.cbegin(); it != folders.cend(); ++it)
            for (int i = 0; i < it.value().size(); i += BATCH) {
                m_batches.append({it.key(), it.value().mid(i, BATCH)});
                m_jobTotal += int(it.value().mid(i, BATCH).size());
            }
        m_progress->setRange(0, std::max(1, m_jobTotal));
        m_progress->setValue(0);
        nextBatch();
    });
}

void IconDownloadDialog::nextBatch()
{
    if (m_cancelRequested) {
        finishSet(true);
        return;
    }
    if (m_batches.isEmpty()) {
        finishSet(false);
        return;
    }
    const auto [folderName, names] = m_batches.takeFirst();
    const QString root = setFolder(m_dir, m_job);
    const QString folder = folderName.isEmpty() ? root : root + QLatin1Char('/') + folderName;
    m_setStatus->setText(tr("Загружаю «%1»: %2 из %3…").arg(m_job.name).arg(m_jobDone).arg(m_jobTotal));
    m_jobReply = get(QStringLiteral("/%1.json?icons=%2").arg(m_job.prefix, names.join(QLatin1Char(','))),
                     [this, folder, names = names](const QByteArray& body, const QString& err) {
                         if (!err.isEmpty()) {
                             if (!m_cancelRequested)
                                 m_setStatus->setText(tr("Загрузка прервана: %1").arg(err));
                             finishSet(true);
                             return;
                         }
                         const QHash<QString, QByteArray> svgs = iconify::parseIconSet(body);
                         if (QDir().mkpath(folder))
                             for (const QString& n : names)
                                 if (svgs.contains(n) && writeIcon(folder, n, svgs.value(n)))
                                     ++m_jobAdded;
                         m_jobDone += int(names.size());
                         m_progress->setValue(m_jobDone);
                         nextBatch();
                     });
}

void IconDownloadDialog::finishSet(bool cancelled)
{
    const QString root = setFolder(m_dir, m_job);
    if (QFileInfo::exists(root))
        writeLicense(root, m_job);
    if (!cancelled) {
        QFile f(root + QLatin1Char('/') + QLatin1String(MARKER));
        QJsonObject o;
        o[QStringLiteral("prefix")] = m_job.prefix;
        o[QStringLiteral("name")] = m_job.name;
        o[QStringLiteral("icons")] = m_jobTotal;
        if (QDir().mkpath(root) && f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(o).toJson());
        m_setStatus->setText(tr("Набор «%1» в галерее: новых значков %2 из %3. Вкладка «%4» в панели «Клипарт».")
                                 .arg(m_job.name)
                                 .arg(m_jobAdded)
                                 .arg(m_jobTotal)
                                 .arg(QFileInfo(root).fileName()));
    } else if (m_cancelRequested) {
        m_setStatus->setText(tr("Загрузка остановлена, скачано значков: %1. Повторная загрузка докачает остальные.")
                                 .arg(m_jobAdded));
    }
    m_jobActive = false;
    m_cancelRequested = false;
    m_batches.clear();
    m_cancel->setVisible(false);
    m_progress->setVisible(false);
    fillSets();
    if (m_jobAdded)
        emit galleryChanged();
}

QString IconDownloadDialog::status() const
{
    return m_tabs->currentIndex() == 0 ? m_searchStatus->text() : m_setStatus->text();
}

} // namespace lf
