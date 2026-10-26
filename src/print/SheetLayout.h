// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QVector>
#include <algorithm>

namespace lf {

// Arrangement of labels on a printed page (A4 sheet of labels or a roll).
struct SheetLayout {
    QString id;
    QString name;
    QString category;
    QString description;
    bool builtin = false;
    bool roll = false;  // roll/thermal: one label per page, page = label size
    double pageW = 210, pageH = 297;
    int cols = 1, rows = 1;
    double labelW = 58, labelH = 40;
    double marginLeft = 0, marginTop = 0;
    double pitchX = 0, pitchY = 0;  // distance between left/top edges of neighbours
    double radius = 0;
    // auto layouts: cols/rows/margins are computed from the label size
    bool autoFit = false;
    double autoMargin = 0, autoGap = 0;

    int perPage() const { return std::max(1, cols * rows); }
    QRectF labelRect(int indexOnPage) const;  // row-major
    QString summary() const;
    QJsonObject toJson() const;
    static SheetLayout fromJson(const QJsonObject& o);
    static SheetLayout rollFor(double w, double h);  // auto roll for a label size
    // Fills the page with as many labels (labelW x labelH) as fit, centred.
    static void fitGrid(SheetLayout& l, double margin, double gap);
};

class SheetLayoutLibrary {
public:
    static QVector<SheetLayout> builtins();
    static QVector<SheetLayout> userLayouts();
    static QVector<SheetLayout> all();
    static QString userDir();
    static bool saveUser(SheetLayout& l, QString* err);
    static bool removeUser(const QString& id);
    // "roll" resolves to a roll matching the given label size
    static SheetLayout find(const QString& id, double labelW, double labelH, bool* found = nullptr);
};

} // namespace lf
