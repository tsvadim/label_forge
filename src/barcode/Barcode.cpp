// SPDX-License-Identifier: GPL-3.0-or-later
// Barcode encoders: Code 128 / GS1-128, EAN-13, EAN-8, UPC-A, Code 39,
// Interleaved 2 of 5, ITF-14, QR Code (via Nayuki's qrcodegen),
// Data Matrix ECC 200 / GS1 DataMatrix.
#include "Barcode.h"

#include "qrcodegen.hpp"

#include <QCoreApplication>
#include <array>
#include <cstring>
#include <stdexcept>

namespace lf::barcode {

namespace {

constexpr int FNC1 = -1;

QString tr(const char* s) { return QCoreApplication::translate("Barcode", s); }

// ---------------------------------------------------------------- helpers

std::vector<int> parseEscapes(const QString& s, bool asUtf8)
{
    std::vector<int> out;
    const QByteArray bytes = asUtf8 ? s.toUtf8() : QByteArray();
    const int n = asUtf8 ? bytes.size() : s.size();
    auto at = [&](int i) -> int {
        return asUtf8 ? static_cast<unsigned char>(bytes.at(i)) : s.at(i).unicode();
    };
    for (int i = 0; i < n; ++i) {
        const int c = at(i);
        if (c == '\\' && i + 1 < n) {
            const int e = at(i + 1);
            int v = -2;
            switch (e) {
            case 'F': v = FNC1; break;
            case 'G': v = 29; break;
            case 'R': v = 30; break;
            case 'E': v = 4; break;
            case 'n': v = 10; break;
            case 't': v = 9; break;
            case '\\': v = '\\'; break;
            default: break;
            }
            if (v != -2) {
                out.push_back(v);
                ++i;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

QString humanFromTokens(const std::vector<int>& t)
{
    QString s;
    for (int c : t)
        if (c >= 32 && c != 127)
            s += QChar(c);
    return s;
}

bool gs1FixedLength(const QString& ai)
{
    static const char* fixed[] = {"00", "01", "02", "03", "04", "11", "12", "13", "14", "15", "16",
                                  "17", "18", "19", "20", "31", "32", "33", "34", "35", "36", "41"};
    const QString p = ai.left(2);
    for (const char* f : fixed)
        if (p == QLatin1String(f))
            return true;
    return false;
}

// "(01)04601234567893(21)ABC" -> FNC1 01 04601234567893 21 ABC
bool parseGs1(const QString& s, int separator, std::vector<int>& tok, QString& human, QString* err)
{
    tok.clear();
    human.clear();
    tok.push_back(FNC1);
    if (!s.startsWith(QLatin1Char('('))) {
        const std::vector<int> rest = parseEscapes(s, false);
        tok.insert(tok.end(), rest.begin(), rest.end());
        human = humanFromTokens(rest);
        return true;
    }
    int i = 0;
    while (i < s.size()) {
        if (s.at(i) != QLatin1Char('(')) {
            *err = tr("GS1: ожидается '(' в позиции %1").arg(i + 1);
            return false;
        }
        const int close = s.indexOf(QLatin1Char(')'), i);
        if (close < 0) {
            *err = tr("GS1: нет закрывающей скобки");
            return false;
        }
        const QString ai = s.mid(i + 1, close - i - 1);
        bool digits = ai.size() >= 2 && ai.size() <= 4;
        for (QChar c : ai)
            digits = digits && c.isDigit();
        if (!digits) {
            *err = tr("GS1: неверный идентификатор применения (%1)").arg(ai);
            return false;
        }
        int next = s.indexOf(QLatin1Char('('), close + 1);
        if (next < 0)
            next = s.size();
        const QString value = s.mid(close + 1, next - close - 1);
        for (QChar c : ai)
            tok.push_back(c.unicode());
        for (QChar c : value)
            tok.push_back(c.unicode());
        human += QLatin1Char('(') + ai + QLatin1Char(')') + value;
        if (next < s.size() && !gs1FixedLength(ai))
            tok.push_back(separator);
        i = next;
    }
    return true;
}

void appendWidths(std::vector<uint8_t>& bars, const char* widths, int narrow = 1, int wide = 3)
{
    bool bar = true;
    for (const char* p = widths; *p; ++p) {
        int w;
        if (*p == 'n')
            w = narrow;
        else if (*p == 'w')
            w = wide;
        else
            w = *p - '0';
        for (int k = 0; k < w; ++k)
            bars.push_back(bar ? 1 : 0);
        bar = !bar;
    }
}

bool allDigits(const QString& s)
{
    if (s.isEmpty())
        return false;
    for (QChar c : s)
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    return true;
}

// ---------------------------------------------------------------- Code 128

const char* const C128[107] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312", "132212",
    "221213", "221312", "231212", "112232", "122132", "122231", "113222", "123122", "123221",
    "223211", "221132", "221231", "213212", "223112", "312131", "311222", "321122", "321221",
    "312212", "322112", "322211", "212123", "212321", "232121", "111323", "131123", "131321",
    "112313", "132113", "132311", "211313", "231113", "231311", "112133", "112331", "132131",
    "113123", "113321", "133121", "313121", "211331", "231131", "213113", "213311", "213131",
    "311123", "311321", "331121", "312113", "312311", "332111", "314111", "221411", "431111",
    "111224", "111422", "121124", "121421", "141122", "141221", "112214", "112412", "122114",
    "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111", "111242",
    "121142", "121241", "114212", "124112", "124211", "411212", "421112", "421211", "212141",
    "214121", "412121", "111143", "111341", "131141", "114113", "114311", "411113", "411311",
    "113141", "114131", "311141", "411131", "211412", "211214", "211232", "2331112"};

Result code128(const std::vector<int>& t, const QString& human)
{
    Result r;
    const size_t n = t.size();
    if (n == 0) {
        r.error = tr("Пустые данные");
        return r;
    }
    for (int c : t)
        if (c != FNC1 && (c < 0 || c > 127)) {
            r.error = tr("Code 128 поддерживает только символы ASCII");
            return r;
        }
    auto isDigit = [&](size_t i) { return i < n && t[i] >= '0' && t[i] <= '9'; };
    auto digitRun = [&](size_t i) {
        size_t k = i;
        while (isDigit(k))
            ++k;
        return k - i;
    };
    auto preferA = [&](size_t i) {
        for (size_t k = i; k < n; ++k) {
            if (t[k] == FNC1)
                continue;
            if (t[k] < 32)
                return true;
            if (t[k] >= 96)
                return false;
        }
        return false;
    };

    enum { A = 0, B = 1, C = 2 };
    std::vector<int> codes;
    size_t first = 0;
    while (first < n && t[first] == FNC1)
        ++first;
    const size_t run0 = digitRun(first);
    int set;
    if (run0 >= 4 || (run0 >= 2 && run0 == n - first && run0 % 2 == 0))
        set = C;
    else
        set = preferA(0) ? A : B;
    codes.push_back(103 + set);

    size_t i = 0;
    while (i < n) {
        if (t[i] == FNC1) {
            codes.push_back(102);
            ++i;
            continue;
        }
        if (set == C) {
            if (isDigit(i) && isDigit(i + 1)) {
                codes.push_back((t[i] - '0') * 10 + (t[i + 1] - '0'));
                i += 2;
                continue;
            }
            const int ns = preferA(i) ? A : B;
            codes.push_back(ns == A ? 101 : 100);
            set = ns;
            continue;
        }
        const size_t run = digitRun(i);
        if (run >= 4) {
            if (run % 2 == 1) {
                codes.push_back(t[i] - 32);  // digit in A or B
                ++i;
            }
            codes.push_back(99);
            set = C;
            continue;
        }
        const int c = t[i];
        if (set == B && c < 32) {
            codes.push_back(101);
            set = A;
        } else if (set == A && c >= 96) {
            codes.push_back(100);
            set = B;
        }
        codes.push_back(set == A ? (c < 32 ? c + 64 : c - 32) : c - 32);
        ++i;
    }
    int sum = codes[0];
    for (size_t k = 1; k < codes.size(); ++k)
        sum += int(k) * codes[k];
    codes.push_back(sum % 103);
    codes.push_back(106);
    for (int c : codes)
        appendWidths(r.bars, C128[c]);
    r.humanText = human;
    r.quiet = 10;
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------- EAN / UPC

const char* const EAN_L[10] = {"0001101", "0011001", "0010011", "0111101", "0100011",
                               "0110001", "0101111", "0111011", "0110111", "0001011"};
const char* const EAN_G[10] = {"0100111", "0110011", "0011011", "0100001", "0011101",
                               "0111001", "0000101", "0010001", "0001001", "0010111"};
const char* const EAN_R[10] = {"1110010", "1100110", "1101100", "1000010", "1011100",
                               "1001110", "1010000", "1000100", "1001000", "1110100"};
const char* const EAN_PARITY[10] = {"LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
                                    "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL"};

void pushBits(Result& r, const char* bits, bool guard)
{
    for (const char* p = bits; *p; ++p) {
        r.bars.push_back(*p == '1');
        r.guards.push_back(guard);
    }
}

bool normalizeGtin(QString& d, int len, Result& r)
{
    d.remove(QLatin1Char(' '));
    if (!allDigits(d) || (d.size() != len - 1 && d.size() != len)) {
        r.error = tr("Нужно %1 или %2 цифр").arg(len - 1).arg(len);
        return false;
    }
    const int check = gtinCheckDigit(d.left(len - 1));
    if (d.size() == len) {
        if (d.at(len - 1).digitValue() != check) {
            r.error = tr("Неверная контрольная цифра (должна быть %1)").arg(check);
            return false;
        }
    } else {
        d += QChar('0' + check);
    }
    return true;
}

Result ean13(QString d, bool upca)
{
    Result r;
    if (upca) {
        if (!normalizeGtin(d, 12, r))
            return r;
        r.humanText = d;
        d.prepend(QLatin1Char('0'));
    } else {
        if (!normalizeGtin(d, 13, r))
            return r;
        r.humanText = d;
    }
    const char* parity = EAN_PARITY[d.at(0).digitValue()];
    pushBits(r, "101", true);
    for (int i = 1; i <= 6; ++i) {
        const int v = d.at(i).digitValue();
        pushBits(r, parity[i - 1] == 'L' ? EAN_L[v] : EAN_G[v], upca && i == 1);
    }
    pushBits(r, "01010", true);
    for (int i = 7; i <= 12; ++i)
        pushBits(r, EAN_R[d.at(i).digitValue()], upca && i == 12);
    pushBits(r, "101", true);
    r.eanType = upca ? 12 : 13;
    r.quiet = upca ? 9 : 11;
    r.ok = true;
    return r;
}

Result ean8(QString d)
{
    Result r;
    if (!normalizeGtin(d, 8, r))
        return r;
    pushBits(r, "101", true);
    for (int i = 0; i < 4; ++i)
        pushBits(r, EAN_L[d.at(i).digitValue()], false);
    pushBits(r, "01010", true);
    for (int i = 4; i < 8; ++i)
        pushBits(r, EAN_R[d.at(i).digitValue()], false);
    pushBits(r, "101", true);
    r.humanText = d;
    r.eanType = 8;
    r.quiet = 7;
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------- Code 39

const char CODE39_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%*";
const char* const CODE39[44] = {
    "nnnwwnwnn", "wnnwnnnnw", "nnwwnnnnw", "wnwwnnnnn", "nnnwwnnnw", "wnnwwnnnn", "nnwwwnnnn",
    "nnnwnnwnw", "wnnwnnwnn", "nnwwnnwnn", "wnnnnwnnw", "nnwnnwnnw", "wnwnnwnnn", "nnnnwwnnw",
    "wnnnwwnnn", "nnwnwwnnn", "nnnnnwwnw", "wnnnnwwnn", "nnwnnwwnn", "nnnnwwwnn", "wnnnnnnww",
    "nnwnnnnww", "wnwnnnnwn", "nnnnwnnww", "wnnnwnnwn", "nnwnwnnwn", "nnnnnnwww", "wnnnnnwwn",
    "nnwnnnwwn", "nnnnwnwwn", "wwnnnnnnw", "nwwnnnnnw", "wwwnnnnnn", "nwnnwnnnw", "wwnnwnnnn",
    "nwwnwnnnn", "nwnnnnwnw", "wwnnnnwnn", "nwwnnnwnn", "nwnwnwnnn", "nwnwnnnwn", "nwnnnwnwn",
    "nnnwnwnwn", "nwnnwnwnn"};

Result code39(QString d, bool check)
{
    Result r;
    d = d.toUpper();
    if (d.isEmpty()) {
        r.error = tr("Пустые данные");
        return r;
    }
    std::vector<int> idx;
    int sum = 0;
    for (QChar c : d) {
        const char* p = c.unicode() < 128 && c != QLatin1Char('*')
                            ? strchr(CODE39_CHARS, char(c.unicode()))
                            : nullptr;
        if (!p || !*p) {
            r.error = tr("Недопустимый символ для Code 39: '%1'").arg(c);
            return r;
        }
        const int v = int(p - CODE39_CHARS);
        idx.push_back(v);
        sum += v;
    }
    if (check)
        idx.push_back(sum % 43);
    idx.insert(idx.begin(), 43);
    idx.push_back(43);
    for (size_t i = 0; i < idx.size(); ++i) {
        appendWidths(r.bars, CODE39[idx[i]]);
        if (i + 1 < idx.size())
            r.bars.push_back(0);  // inter-character gap
    }
    r.humanText = d;
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------- ITF

const char* const ITF[10] = {"nnwwn", "wnnnw", "nwnnw", "wwnnn", "nnwnw", "wnwnn",
                             "nwwnn", "nnnww", "wnnwn", "nwnwn"};

Result itf(QString d, bool itf14)
{
    Result r;
    d.remove(QLatin1Char(' '));
    if (!allDigits(d)) {
        r.error = tr("ITF кодирует только цифры");
        return r;
    }
    if (itf14 && !normalizeGtin(d, 14, r))
        return r;
    if (d.size() % 2)
        d.prepend(QLatin1Char('0'));
    appendWidths(r.bars, "nnnn");
    for (int i = 0; i < d.size(); i += 2) {
        const char* a = ITF[d.at(i).digitValue()];
        const char* b = ITF[d.at(i + 1).digitValue()];
        char buf[11];
        for (int k = 0; k < 5; ++k) {
            buf[2 * k] = a[k];
            buf[2 * k + 1] = b[k];
        }
        buf[10] = 0;
        appendWidths(r.bars, buf);
    }
    appendWidths(r.bars, "wnn");
    r.humanText = d;
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------- QR

Result qr(const QString& data, int ecc)
{
    Result r;
    using qrcodegen::QrCode;
    const QrCode::Ecc levels[4] = {QrCode::Ecc::LOW, QrCode::Ecc::MEDIUM, QrCode::Ecc::QUARTILE,
                                   QrCode::Ecc::HIGH};
    std::vector<int> t = parseEscapes(data, true);
    std::vector<uint8_t> bytes;
    bool binary = false;
    for (int c : t) {
        if (c == FNC1)
            continue;
        if (c == 0)
            binary = true;
        bytes.push_back(uint8_t(c));
    }
    if (bytes.empty()) {
        r.error = tr("Пустые данные");
        return r;
    }
    try {
        const QrCode::Ecc lvl = levels[qBound(0, ecc, 3)];
        const QrCode q = binary ? QrCode::encodeBinary(bytes, lvl)
                                : QrCode::encodeText(std::string(bytes.begin(), bytes.end()).c_str(), lvl);
        r.width = r.height = q.getSize();
        r.matrix.resize(size_t(r.width) * r.height);
        for (int y = 0; y < r.height; ++y)
            for (int x = 0; x < r.width; ++x)
                r.matrix[size_t(y) * r.width + x] = q.getModule(x, y) ? 1 : 0;
    } catch (const std::exception&) {
        r.error = tr("Слишком много данных для QR-кода");
        return r;
    }
    r.is2D = true;
    r.matrixQuiet = 4;
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------- Data Matrix

struct DmSize {
    int size, regions, dataCw, ecCw, blocks;
};
const DmSize DM_SIZES[] = {
    {10, 1, 3, 5, 1},      {12, 1, 5, 7, 1},      {14, 1, 8, 10, 1},     {16, 1, 12, 12, 1},
    {18, 1, 18, 14, 1},    {20, 1, 22, 18, 1},    {22, 1, 30, 20, 1},    {24, 1, 36, 24, 1},
    {26, 1, 44, 28, 1},    {32, 2, 62, 36, 1},    {36, 2, 86, 42, 1},    {40, 2, 114, 48, 1},
    {44, 2, 144, 56, 1},   {48, 2, 174, 68, 1},   {52, 2, 204, 84, 2},   {64, 4, 280, 112, 2},
    {72, 4, 368, 144, 4},  {80, 4, 456, 192, 4},  {88, 4, 576, 224, 4},  {96, 4, 696, 272, 4},
    {104, 4, 816, 336, 6},
};

struct Gf256 {
    int exp[512];
    int log[256];
    Gf256()
    {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = x;
            log[x] = i;
            x <<= 1;
            if (x & 0x100)
                x ^= 0x12D;
        }
        for (int i = 255; i < 512; ++i)
            exp[i] = exp[i - 255];
        log[0] = 0;
    }
    int mul(int a, int b) const { return (a && b) ? exp[log[a] + log[b]] : 0; }
};

std::vector<int> rsEncode(const std::vector<int>& data, int nec)
{
    static const Gf256 gf;
    std::vector<int> gen(1, 1);
    for (int i = 1; i <= nec; ++i) {
        std::vector<int> ng(gen.size() + 1, 0);
        for (size_t j = 0; j < gen.size(); ++j) {
            ng[j] ^= gen[j];
            ng[j + 1] ^= gf.mul(gen[j], gf.exp[i]);
        }
        gen = ng;
    }
    std::vector<int> msg(data);
    msg.resize(data.size() + nec, 0);
    for (size_t k = 0; k < data.size(); ++k) {
        const int coef = msg[k];
        if (coef)
            for (int j = 1; j <= nec; ++j)
                msg[k + j] ^= gf.mul(gen[j], coef);
    }
    return std::vector<int>(msg.begin() + data.size(), msg.end());
}

class DmPlacement {
public:
    DmPlacement(int nrow, int ncol) : nrow(nrow), ncol(ncol), a(size_t(nrow) * ncol, 0) {}

    void run()
    {
        int chr = 1, row = 4, col = 0;
        do {
            if (row == nrow && col == 0)
                corner1(chr++);
            if (row == nrow - 2 && col == 0 && ncol % 4)
                corner2(chr++);
            if (row == nrow - 2 && col == 0 && ncol % 8 == 4)
                corner3(chr++);
            if (row == nrow + 4 && col == 2 && !(ncol % 8))
                corner4(chr++);
            do {
                if (row < nrow && col >= 0 && !a[size_t(row) * ncol + col])
                    utah(row, col, chr++);
                row -= 2;
                col += 2;
            } while (row >= 0 && col < ncol);
            row += 1;
            col += 3;
            do {
                if (row >= 0 && col < ncol && !a[size_t(row) * ncol + col])
                    utah(row, col, chr++);
                row += 2;
                col -= 2;
            } while (row < nrow && col >= 0);
            row += 3;
            col += 1;
        } while (row < nrow || col < ncol);
        if (!a[size_t(nrow) * ncol - 1])
            a[size_t(nrow) * ncol - 1] = a[size_t(nrow) * ncol - ncol - 2] = 1;
    }

    int nrow, ncol;
    std::vector<int> a;  // 1 = fixed dark, >=10: 10*codeword + bit (1 = MSB)

private:
    void module(int row, int col, int chr, int bit)
    {
        if (row < 0) {
            row += nrow;
            col += 4 - ((nrow + 4) % 8);
        }
        if (col < 0) {
            col += ncol;
            row += 4 - ((ncol + 4) % 8);
        }
        a[size_t(row) * ncol + col] = 10 * chr + bit;
    }
    void utah(int row, int col, int chr)
    {
        module(row - 2, col - 2, chr, 1);
        module(row - 2, col - 1, chr, 2);
        module(row - 1, col - 2, chr, 3);
        module(row - 1, col - 1, chr, 4);
        module(row - 1, col, chr, 5);
        module(row, col - 2, chr, 6);
        module(row, col - 1, chr, 7);
        module(row, col, chr, 8);
    }
    void corner1(int chr)
    {
        module(nrow - 1, 0, chr, 1);
        module(nrow - 1, 1, chr, 2);
        module(nrow - 1, 2, chr, 3);
        module(0, ncol - 2, chr, 4);
        module(0, ncol - 1, chr, 5);
        module(1, ncol - 1, chr, 6);
        module(2, ncol - 1, chr, 7);
        module(3, ncol - 1, chr, 8);
    }
    void corner2(int chr)
    {
        module(nrow - 3, 0, chr, 1);
        module(nrow - 2, 0, chr, 2);
        module(nrow - 1, 0, chr, 3);
        module(0, ncol - 4, chr, 4);
        module(0, ncol - 3, chr, 5);
        module(0, ncol - 2, chr, 6);
        module(0, ncol - 1, chr, 7);
        module(1, ncol - 1, chr, 8);
    }
    void corner3(int chr)
    {
        module(nrow - 3, 0, chr, 1);
        module(nrow - 2, 0, chr, 2);
        module(nrow - 1, 0, chr, 3);
        module(0, ncol - 2, chr, 4);
        module(0, ncol - 1, chr, 5);
        module(1, ncol - 1, chr, 6);
        module(2, ncol - 1, chr, 7);
        module(3, ncol - 1, chr, 8);
    }
    void corner4(int chr)
    {
        module(nrow - 1, 0, chr, 1);
        module(nrow - 1, ncol - 1, chr, 2);
        module(0, ncol - 3, chr, 3);
        module(0, ncol - 2, chr, 4);
        module(0, ncol - 1, chr, 5);
        module(1, ncol - 3, chr, 6);
        module(1, ncol - 2, chr, 7);
        module(1, ncol - 1, chr, 8);
    }
};

Result dataMatrix(const std::vector<int>& t)
{
    Result r;
    std::vector<int> cw;
    const size_t n = t.size();
    auto isDigit = [&](size_t i) { return i < n && t[i] >= '0' && t[i] <= '9'; };
    for (size_t i = 0; i < n;) {
        if (isDigit(i) && isDigit(i + 1)) {
            cw.push_back(130 + (t[i] - '0') * 10 + (t[i + 1] - '0'));
            i += 2;
        } else if (t[i] == FNC1) {
            cw.push_back(232);
            ++i;
        } else if (t[i] < 128) {
            cw.push_back(t[i] + 1);
            ++i;
        } else {
            cw.push_back(235);
            cw.push_back(t[i] - 128 + 1);
            ++i;
        }
    }
    if (cw.empty()) {
        r.error = tr("Пустые данные");
        return r;
    }
    const DmSize* sz = nullptr;
    for (const DmSize& s : DM_SIZES)
        if (s.dataCw >= int(cw.size())) {
            sz = &s;
            break;
        }
    if (!sz) {
        r.error = tr("Слишком много данных для Data Matrix");
        return r;
    }
    // padding
    if (int(cw.size()) < sz->dataCw)
        cw.push_back(129);
    while (int(cw.size()) < sz->dataCw) {
        const int pos = int(cw.size()) + 1;
        int v = 129 + ((149 * pos) % 253) + 1;
        if (v > 254)
            v -= 254;
        cw.push_back(v);
    }
    // error correction, interleaved blocks
    const int nb = sz->blocks;
    const int ecPerBlock = sz->ecCw / nb;
    std::vector<int> all(cw);
    all.resize(size_t(sz->dataCw + sz->ecCw), 0);
    for (int b = 0; b < nb; ++b) {
        std::vector<int> blockData;
        for (int k = b; k < sz->dataCw; k += nb)
            blockData.push_back(cw[size_t(k)]);
        const std::vector<int> ec = rsEncode(blockData, ecPerBlock);
        for (int j = 0; j < ecPerBlock; ++j)
            all[size_t(sz->dataCw + b + j * nb)] = ec[size_t(j)];
    }
    // module placement
    const int region = sz->size / sz->regions - 2;
    const int nrow = region * sz->regions;
    DmPlacement pl(nrow, nrow);
    pl.run();

    r.width = r.height = sz->size;
    r.matrix.assign(size_t(sz->size) * sz->size, 0);
    auto set = [&](int x, int y, bool v) { r.matrix[size_t(y) * sz->size + x] = v; };
    // finder / timing patterns of each region
    for (int ry = 0; ry < sz->regions; ++ry) {
        for (int rx = 0; rx < sz->regions; ++rx) {
            const int x0 = rx * (region + 2), y0 = ry * (region + 2);
            for (int k = 0; k < region + 2; ++k) {
                set(x0, y0 + k, true);                         // left: solid
                set(x0 + k, y0 + region + 1, true);            // bottom: solid
                set(x0 + k, y0, (k % 2) == 0);                 // top: alternating
                set(x0 + region + 1, y0 + k, (k % 2) == 1);    // right: alternating
            }
            set(x0 + region + 1, y0 + region + 1, true);
        }
    }
    for (int row = 0; row < nrow; ++row) {
        for (int col = 0; col < nrow; ++col) {
            const int v = pl.a[size_t(row) * nrow + col];
            bool dark;
            if (v == 1)
                dark = true;
            else if (v >= 10) {
                const int chr = v / 10, bit = v % 10;
                dark = (all[size_t(chr - 1)] >> (8 - bit)) & 1;
            } else
                dark = false;
            const int sx = (col / region) * (region + 2) + 1 + col % region;
            const int sy = (row / region) * (region + 2) + 1 + row % region;
            set(sx, sy, dark);
        }
    }
    r.is2D = true;
    r.matrixQuiet = 2;
    r.ok = true;
    return r;
}

} // namespace

// ---------------------------------------------------------------- public

int gtinCheckDigit(const QString& digits)
{
    int sum = 0;
    int weight = 3;
    for (int i = digits.size() - 1; i >= 0; --i) {
        sum += digits.at(i).digitValue() * weight;
        weight = weight == 3 ? 1 : 3;
    }
    return (10 - sum % 10) % 10;
}

const QVector<SymbologyInfo>& symbologies()
{
    static const QVector<SymbologyInfo> list = {
        {QStringLiteral("code128"), QStringLiteral("Code 128"), QStringLiteral("ABC-12345")},
        {QStringLiteral("gs1-128"), QStringLiteral("GS1-128 (EAN-128)"),
         QStringLiteral("(01)04601234567893(17)271231(10)A1")},
        {QStringLiteral("ean13"), QStringLiteral("EAN-13"), QStringLiteral("482000000001")},
        {QStringLiteral("ean8"), QStringLiteral("EAN-8"), QStringLiteral("9638507")},
        {QStringLiteral("upca"), QStringLiteral("UPC-A"), QStringLiteral("03600029145")},
        {QStringLiteral("code39"), QStringLiteral("Code 39"), QStringLiteral("CODE-39")},
        {QStringLiteral("itf"), QStringLiteral("Interleaved 2 of 5"), QStringLiteral("12345678")},
        {QStringLiteral("itf14"), QStringLiteral("ITF-14"), QStringLiteral("1540014128876")},
        {QStringLiteral("qr"), QStringLiteral("QR Code"), QStringLiteral("https://example.org")},
        {QStringLiteral("datamatrix"), QStringLiteral("Data Matrix"), QStringLiteral("DM-0001")},
        {QStringLiteral("gs1-datamatrix"), QStringLiteral("GS1 DataMatrix"),
         QStringLiteral("(01)04601234567893(21)abc123")},
    };
    return list;
}

bool is2D(const QString& id)
{
    return id == QLatin1String("qr") || id == QLatin1String("datamatrix") ||
           id == QLatin1String("gs1-datamatrix");
}

Result encode(const QString& symbology, const QString& data, const Options& opt)
{
    const QString id = symbology.toLower();
    if (id == QLatin1String("code128")) {
        const std::vector<int> t = parseEscapes(data, false);
        return code128(t, humanFromTokens(t));
    }
    if (id == QLatin1String("gs1-128")) {
        std::vector<int> t;
        QString human, err;
        if (!parseGs1(data, FNC1, t, human, &err)) {
            Result r;
            r.error = err;
            return r;
        }
        return code128(t, human);
    }
    if (id == QLatin1String("ean13"))
        return ean13(data, false);
    if (id == QLatin1String("upca"))
        return ean13(data, true);
    if (id == QLatin1String("ean8"))
        return ean8(data);
    if (id == QLatin1String("code39"))
        return code39(data, opt.code39Check);
    if (id == QLatin1String("itf"))
        return itf(data, false);
    if (id == QLatin1String("itf14"))
        return itf(data, true);
    if (id == QLatin1String("qr"))
        return qr(data, opt.qrEcc);
    if (id == QLatin1String("datamatrix"))
        return dataMatrix(parseEscapes(data, true));
    if (id == QLatin1String("gs1-datamatrix")) {
        std::vector<int> t;
        QString human, err;
        if (!parseGs1(data, 29, t, human, &err)) {
            Result r;
            r.error = err;
            return r;
        }
        // GS1 values are ASCII; re-encode any non-ASCII through UTF-8
        std::vector<int> bytes;
        for (int c : t) {
            if (c >= 128) {
                const QByteArray u = QString(QChar(c)).toUtf8();
                for (char b : u)
                    bytes.push_back(static_cast<unsigned char>(b));
            } else
                bytes.push_back(c);
        }
        return dataMatrix(bytes);
    }
    Result r;
    r.error = tr("Неизвестная символика: %1").arg(symbology);
    return r;
}

} // namespace lf::barcode
