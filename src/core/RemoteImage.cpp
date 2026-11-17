// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemoteImage.h"

#include <QEventLoop>
#include <QHash>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>

namespace lf::remote {

namespace {
constexpr int TIMEOUT_MS = 10000;
constexpr qint64 MAX_BYTES = 20 * 1024 * 1024;
constexpr qint64 CACHE_LIMIT = 256 * 1024 * 1024;  // dropped as a whole when exceeded

struct Entry {
    QByteArray data;
    QString error;
};

QMutex& cacheMutex()
{
    static QMutex m;
    return m;
}

QHash<QString, Entry>& cacheMap()
{
    static QHash<QString, Entry> h;
    return h;
}

qint64& cacheBytes()
{
    static qint64 n = 0;
    return n;
}

bool isBase64Header(QStringView header) { return header.endsWith(QLatin1String(";base64"), Qt::CaseInsensitive); }

Entry download(const QString& url)
{
    Entry e;
    QThread* t = QThread::create([&e, url] {
        QNetworkAccessManager nam;
        QNetworkRequest req{QUrl(url)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LabelForge"));
        req.setTransferTimeout(TIMEOUT_MS);
        QNetworkReply* reply = nam.get(req);
        QEventLoop loop;
        bool tooBig = false, timedOut = false;
        QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [&](qint64 received, qint64 total) {
            if (received > MAX_BYTES || total > MAX_BYTES) {
                tooBig = true;
                reply->abort();
            }
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        // transferTimeout only counts inactivity; this caps the whole download
        QTimer::singleShot(TIMEOUT_MS, &loop, [&] {
            timedOut = true;
            reply->abort();
        });
        if (!reply->isFinished())
            loop.exec();
        if (tooBig)
            e.error = QStringLiteral("file is larger than 20 MB");
        else if (timedOut)
            e.error = QStringLiteral("timed out");
        else if (reply->error() != QNetworkReply::NoError)
            e.error = reply->errorString();
        else
            e.data = reply->readAll();
        delete reply;
    });
    t->start();
    t->wait();
    delete t;
    if (e.error.isEmpty() && e.data.isEmpty())
        e.error = QStringLiteral("empty response");
    return e;
}
} // namespace

bool isWebUrl(const QString& s)
{
    return s.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
           s.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
}

bool isDataUri(const QString& s)
{
    return s.startsWith(QLatin1String("data:"), Qt::CaseInsensitive) && s.indexOf(QLatin1Char(',')) > 0;
}

QString dataUriMime(const QString& uri)
{
    if (!isDataUri(uri))
        return QString();
    const qsizetype comma = uri.indexOf(QLatin1Char(','));
    const QString header = uri.mid(5, comma - 5);
    return header.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
}

QByteArray decodeDataUri(const QString& uri, QString* mime)
{
    if (!isDataUri(uri))
        return QByteArray();
    const qsizetype comma = uri.indexOf(QLatin1Char(','));
    const QStringView header = QStringView(uri).mid(5, comma - 5);
    const QStringView payload = QStringView(uri).mid(comma + 1);
    if (mime)
        *mime = dataUriMime(uri);
    if (isBase64Header(header))
        return QByteArray::fromBase64(payload.toLatin1());
    return QByteArray::fromPercentEncoding(payload.toUtf8());
}

qint64 dataUriSize(const QString& uri)
{
    if (!isDataUri(uri))
        return 0;
    const qsizetype comma = uri.indexOf(QLatin1Char(','));
    const qint64 n = uri.size() - comma - 1;
    return isBase64Header(QStringView(uri).mid(5, comma - 5)) ? n * 3 / 4 : n;
}

bool looksLikeSvg(const QByteArray& bytes)
{
    QByteArray head = bytes.left(4096);
    if (head.startsWith("\xEF\xBB\xBF"))
        head.remove(0, 3);
    head = head.trimmed();
    return head.startsWith('<') && head.toLower().contains("<svg");
}

QString sniffMime(const QByteArray& b)
{
    if (b.startsWith("\x89PNG"))
        return QStringLiteral("image/png");
    if (b.startsWith("\xFF\xD8\xFF"))
        return QStringLiteral("image/jpeg");
    if (b.startsWith("GIF8"))
        return QStringLiteral("image/gif");
    if (b.startsWith("BM"))
        return QStringLiteral("image/bmp");
    if (b.startsWith("RIFF") && b.mid(8, 4) == "WEBP")
        return QStringLiteral("image/webp");
    if (b.startsWith(QByteArray("II*\0", 4)) || b.startsWith(QByteArray("MM\0*", 4)))
        return QStringLiteral("image/tiff");
    if (b.startsWith(QByteArray("\0\0\1\0", 4)))
        return QStringLiteral("image/x-icon");
    if (looksLikeSvg(b))
        return QStringLiteral("image/svg+xml");
    return QStringLiteral("application/octet-stream");
}

QString makeDataUri(const QByteArray& bytes)
{
    return QStringLiteral("data:") + sniffMime(bytes) + QStringLiteral(";base64,") +
           QString::fromLatin1(bytes.toBase64());
}

QByteArray fetchUrl(const QString& url, QString* err)
{
    {
        QMutexLocker lock(&cacheMutex());
        const auto it = cacheMap().constFind(url);
        if (it != cacheMap().cend()) {
            if (err)
                *err = it->error;
            return it->data;
        }
    }
    const Entry e = download(url);
    {
        QMutexLocker lock(&cacheMutex());
        if (cacheBytes() + e.data.size() > CACHE_LIMIT) {
            cacheMap().clear();
            cacheBytes() = 0;
        }
        cacheMap().insert(url, e);
        cacheBytes() += e.data.size();
    }
    if (err)
        *err = e.error;
    return e.data;
}

void clearCache()
{
    QMutexLocker lock(&cacheMutex());
    cacheMap().clear();
    cacheBytes() = 0;
}

} // namespace lf::remote
