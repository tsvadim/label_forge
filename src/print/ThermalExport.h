// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <functional>

namespace lf {

class PrintJob;
class LabelDocument;
struct PrintSettings;

// ZPL II (Zebra and compatibles) for a 1-bit image. Uses ZPL ACS compression.
QByteArray zplLabel(const QImage& mono, int quantity, const PrintSettings& s);
// TSPL / TSPL2 (TSC, Xprinter, Gprinter, HPRT, ...).
QByteArray tsplLabel(const QImage& mono, int quantity, double widthMm, double heightMm, const PrintSettings& s);

// Renders the whole job at the printer resolution and returns printer commands.
// Identical consecutive labels are merged into one command with a quantity.
QByteArray buildThermalJob(const PrintJob& job, const LabelDocument& doc, const PrintSettings& s,
                           const std::function<bool(int, int)>& progress = {});

// Sends bytes straight to a printer queue, bypassing the driver (RAW).
bool sendRawToPrinter(const QString& printer, const QByteArray& data, const QString& docName, QString* err);

} // namespace lf
