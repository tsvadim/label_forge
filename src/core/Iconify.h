// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Parsing of Iconify API responses (https://iconify.design/docs/api/) and
// conversion of its icon data to SVG files. No networking here: the UI does
// the requests, this part is tested offline.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace lf::iconify {

struct Collection {
    QString prefix;  // "tabler"
    QString name;    // "Tabler Icons"
    int total = 0;
    QString author, authorUrl;
    QString license, licenseSpdx, licenseUrl;
    QString category;      // Iconify's grouping, e.g. "UI 24px"
    bool palette = false;  // multicolour icons (cannot be recoloured)
};

// Licenses that allow printing on commercial labels without crediting the
// author on every label: MIT, Apache, ISC, BSD, CC0, OFL, Unlicense.
bool isFreeLicense(const QString& spdx);

// /collections: every visible collection, sorted by name.
QList<Collection> parseCollections(const QByteArray& json);

struct SearchResult {
    QStringList icons;  // "prefix:name", in the server's order
    QHash<QString, Collection> collections;
    int total = 0;
};
// /search; icons from collections with a non-free license are dropped.
SearchResult parseSearch(const QByteArray& json);

// /collection?prefix=...: icon names grouped into folders. Categories are
// used when the set has them, otherwise names are split by first letter so
// that no folder gets thousands of files. Hidden icons are left out. For sets
// with style variants ("-outline", "-rounded"...) only one style is kept.
// Folders bigger than 500 icons are split into parts named by their range.
QMap<QString, QStringList> parseCollectionFolders(const QByteArray& json);

// /<prefix>.json?icons=...: icon name -> complete SVG file. Aliases without
// their own data resolve to their parent.
QHash<QString, QByteArray> parseIconSet(const QByteArray& json);

// Builds the SVG file for one icon. currentColor becomes black so the file
// renders the same everywhere and can be recoloured like other clipart.
QByteArray makeSvg(const QString& body, double left, double top, double width, double height, int rotate = 0,
                   bool hFlip = false, bool vFlip = false);

// Contents of LICENSE.txt written next to downloaded icons.
QString licenseNote(const Collection& c);

// File and folder names: characters that Windows forbids are replaced.
QString safeFileName(const QString& s);

} // namespace lf::iconify
