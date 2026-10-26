// LabelForge — open-source label designer
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace lf {

constexpr double MM_PER_INCH = 25.4;
constexpr double PT_TO_MM = 25.4 / 72.0;

inline double mmToDots(double mm, double dpi) { return mm * dpi / MM_PER_INCH; }
inline double dotsToMm(double dots, double dpi) { return dots * MM_PER_INCH / dpi; }

// Everything an object needs to know to draw itself for one concrete label.
struct RenderContext {
    const QStringList* columns = nullptr;  // data source column names
    const QStringList* values = nullptr;   // values of the current record
    int sequence = 1;                      // 1-based number of the label inside the job
    int recordNumber = 1;                  // 1-based record number
    bool rawTemplates = false;             // design view: show "{Field}" literally
    double deviceDpi = 0;                  // >0: raster device, snap barcode modules to dots
    QDateTime now = QDateTime::currentDateTime();
    QString baseDir;                       // resolves relative image paths

    bool fieldValue(const QString& name, QString* out) const;
};

// Expands variables in a template string:
//   {Field}            value of a data column (case-insensitive)
//   {Field:0.00}       numeric formatting, {Field:upper} / {Field:lower}
//   {seq} {seq:0001} {seq:100:5}   counter: start (width = digits of start), step
//   {rec}              record number
//   {date} {date:dd.MM.yyyy} {date+30:dd.MM.yy}  current date with optional day offset
//   {time} {time:HH:mm:ss}
//   {{ and }} produce literal braces
QString expandTemplate(const QString& tpl, const RenderContext& ctx);

// Variables offered in "insert field" menus.
QStringList builtinVariables();

} // namespace lf
