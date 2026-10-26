// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPainterPath>
#include <QString>

namespace lf {

struct TextStyle {
    QString family = QStringLiteral("Arial");
    double sizePt = 10;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    double lineSpacing = 1.0;
    int hAlign = 0;  // 0 left, 1 center, 2 right
    int vAlign = 0;  // 0 top, 1 middle, 2 bottom
    bool wordWrap = true;
    bool autoShrink = false;
};

// Lays the text out into a box of boxW x boxH millimetres and returns it as
// outlines (millimetre coordinates). Using outlines instead of drawText keeps
// rendering identical on screen, PDF, office printers and thermal bitmaps.
QPainterPath layoutText(const QString& text, const TextStyle& st, double boxW, double boxH);

} // namespace lf
