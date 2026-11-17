// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QRectF>
#include <QString>

namespace lf::icons {

// Toolbar icons are drawn in code so the app ships without image resources.
QIcon get(const QString& name);
QIcon appIcon();
QIcon colorSwatch(const QColor& c);
QIcon shape(int kind);  // shapes::Kind, drawn from the same outline as the object

// Light rounded card a picture thumbnail is drawn on, so pictures stay
// readable in a dark theme and look as they will on a white label. Draw the
// picture inside tileInner(). Safe to use in worker threads (QImage only).
QImage thumbnailTile(int size);
QRectF tileInner(int size);

} // namespace lf::icons
