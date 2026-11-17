// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

namespace lf::remote {

// "http://..." or "https://..."
bool isWebUrl(const QString& s);
// "data:[mime][;base64],payload"
bool isDataUri(const QString& s);

// Payload of a data: URI (base64 or percent-encoded); empty if malformed.
QByteArray decodeDataUri(const QString& uri, QString* mime = nullptr);
// Wraps binary data (an image from a database BLOB) into a base64 data: URI,
// the MIME type is guessed from the signature.
QString makeDataUri(const QByteArray& bytes);
// Approximate size of the decoded payload, bytes, without decoding it.
qint64 dataUriSize(const QString& uri);
// MIME type of a data: URI, "" if absent.
QString dataUriMime(const QString& uri);

// MIME type guessed from the first bytes ("application/octet-stream" if unknown).
QString sniffMime(const QByteArray& bytes);
bool looksLikeSvg(const QByteArray& bytes);

// Downloads a URL synchronously (10 s timeout, redirects followed, 20 MB
// limit). Results, failures included, are cached for the lifetime of the
// process, so an unreachable address costs the timeout only once. The
// download runs in its own thread: the caller is blocked but no events are
// processed, so it is safe to call while painting. Needs a QCoreApplication.
QByteArray fetchUrl(const QString& url, QString* err = nullptr);
// Forgets cached downloads (tests).
void clearCache();

} // namespace lf::remote
