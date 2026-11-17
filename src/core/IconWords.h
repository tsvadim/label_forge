// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Small Russian -> English dictionary of words common on labels, so that
// icons with English file names (downloaded sets, Iconify search) can be
// found by a Russian query.
namespace lf::iconwords {

bool hasCyrillic(const QString& s);

// English words for one Russian word or its beginning ("короб" -> box,
// package...); empty if unknown or not Cyrillic.
QStringList translate(const QString& word);

// Query for Iconify: Cyrillic words replaced by their first translation.
// Words without a translation stay as they are (check with hasCyrillic()).
QString englishQuery(const QString& query);

// Local gallery search: every word of the query must occur in the name,
// either as typed or, for a Russian word, as one of its translations.
// Build it once per query, then test many names.
class Matcher {
public:
    explicit Matcher(const QString& query);
    bool isEmpty() const { return m_words.isEmpty(); }
    bool operator()(const QString& name) const;

private:
    QStringList m_words;
    QList<QStringList> m_translations;
};
bool matches(const QString& name, const QString& query);

} // namespace lf::iconwords
