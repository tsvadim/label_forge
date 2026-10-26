// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>

namespace lf {

// Decompresses a raw DEFLATE stream (RFC 1951), as stored inside ZIP/XLSX files.
// Returns false on corrupt input.
bool inflateRaw(const QByteArray& in, QByteArray* out, qsizetype expectedSize = -1);

} // namespace lf
