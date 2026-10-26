// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>
#include <cstdint>
#include <vector>

namespace lf::barcode {

struct Options {
    int qrEcc = 1;             // 0 L, 1 M, 2 Q, 3 H
    bool code39Check = false;
};

struct Result {
    bool ok = false;
    QString error;
    bool is2D = false;

    // 1D symbols: one entry per module, 1 = bar
    std::vector<uint8_t> bars;
    std::vector<uint8_t> guards;  // 1 = module belongs to an extended guard bar
    QString humanText;
    int eanType = 0;  // 13, 8, 12 (UPC-A) -> special human-readable layout, 0 = centred
    int quiet = 10;   // recommended quiet zone, modules

    // 2D symbols
    int width = 0, height = 0;
    std::vector<uint8_t> matrix;  // row-major, 1 = dark
    int matrixQuiet = 2;

    bool dark(int x, int y) const { return matrix[size_t(y) * width + x] != 0; }
};

struct SymbologyInfo {
    QString id;
    QString name;
    QString sample;
};

const QVector<SymbologyInfo>& symbologies();
bool is2D(const QString& id);

// Data escapes processed after variable expansion:
//   \F  FNC1 (GS1 separator),  \G  ASCII 29 (GS),  \R ASCII 30,  \E ASCII 4,
//   \n  newline,  \\  backslash
// GS1 symbologies also accept "(01)04601234567893(17)251231" notation.
Result encode(const QString& symbology, const QString& data, const Options& opt = {});

// EAN/UPC check digit for the given digits (without check digit).
int gtinCheckDigit(const QString& digits);

} // namespace lf::barcode
