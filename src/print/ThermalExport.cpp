// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThermalExport.h"

#include "PrintJob.h"
#include "core/LabelDocument.h"
#include "core/Renderer.h"

#include <QCoreApplication>
#include <QPainter>
#include <QProcess>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winspool.h>
#endif

namespace lf {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Thermal", s); }

// ZPL ACS run-length encoding of one hex row.
QByteArray zplCompressRow(const QByteArray& hex)
{
    QByteArray out;
    // trailing zeros -> ','
    int end = hex.size();
    while (end > 0 && hex.at(end - 1) == '0')
        --end;
    const bool zeroTail = end < hex.size();
    if (end == 0)
        return QByteArrayLiteral(",");
    int i = 0;
    while (i < end) {
        const char c = hex.at(i);
        int run = 1;
        while (i + run < end && hex.at(i + run) == c)
            ++run;
        int n = run;
        while (n >= 400) {
            out += 'z';
            n -= 400;
        }
        if (n >= 20) {
            out += char('f' + n / 20);  // g=20, h=40, ... y=380
            n %= 20;
        }
        if (n > 0 && run > 1)
            out += char('F' + n);  // G=1 ... Y=19
        out += c;
        i += run;
    }
    if (zeroTail)
        out += ',';
    return out;
}

int bytesPerRow(const QImage& img) { return (img.width() + 7) / 8; }
} // namespace

QByteArray zplLabel(const QImage& mono, int quantity, const PrintSettings& s)
{
    const int bpr = bytesPerRow(mono);
    const int total = bpr * mono.height();
    QByteArray data;
    QByteArray prevRow;
    static const char hexd[] = "0123456789ABCDEF";
    for (int y = 0; y < mono.height(); ++y) {
        const uchar* line = mono.constScanLine(y);
        QByteArray hex(bpr * 2, '0');
        for (int b = 0; b < bpr; ++b) {
            hex[2 * b] = hexd[line[b] >> 4];
            hex[2 * b + 1] = hexd[line[b] & 15];
        }
        if (y > 0 && hex == prevRow)
            data += ':';
        else
            data += zplCompressRow(hex);
        prevRow = hex;
    }
    QByteArray z;
    z += "^XA\n";
    z += "~SD" + QByteArray::number(qBound(0, s.darkness, 30)).rightJustified(2, '0') + "\n";
    z += "^PR" + QByteArray::number(qBound(1, s.speed, 14)) + "\n";
    z += "^PW" + QByteArray::number(mono.width()) + "\n";
    z += "^LL" + QByteArray::number(mono.height()) + "\n";
    z += "^LH0,0\n";
    z += "^FO0,0^GFA," + QByteArray::number(total) + "," + QByteArray::number(total) + "," +
         QByteArray::number(bpr) + "," + data + "^FS\n";
    z += "^PQ" + QByteArray::number(std::max(1, quantity)) + "\n";
    z += "^XZ\n";
    return z;
}

QByteArray tsplLabel(const QImage& mono, int quantity, double widthMm, double heightMm, const PrintSettings& s)
{
    const int bpr = bytesPerRow(mono);
    QByteArray t;
    t += "SIZE " + QByteArray::number(widthMm, 'f', 1) + " mm," + QByteArray::number(heightMm, 'f', 1) + " mm\r\n";
    t += "GAP " + QByteArray::number(s.gapMm, 'f', 1) + " mm,0 mm\r\n";
    t += "SPEED " + QByteArray::number(qBound(1, s.speed, 12)) + "\r\n";
    t += "DENSITY " + QByteArray::number(qBound(0, s.darkness / 2, 15)) + "\r\n";
    t += "DIRECTION 1\r\nREFERENCE 0,0\r\nCLS\r\n";
    t += "BITMAP 0,0," + QByteArray::number(bpr) + "," + QByteArray::number(mono.height()) + ",0,";
    QByteArray bits;
    bits.reserve(bpr * mono.height());
    for (int y = 0; y < mono.height(); ++y) {
        const uchar* line = mono.constScanLine(y);
        for (int b = 0; b < bpr; ++b)
            bits += char(s.tsplInvert ? uchar(~line[b]) : line[b]);
    }
    t += bits;
    t += "\r\nPRINT 1," + QByteArray::number(std::max(1, quantity)) + "\r\n";
    return t;
}

QByteArray buildThermalJob(const PrintJob& job, const LabelDocument& doc, const PrintSettings& s,
                           const std::function<bool(int, int)>& progress)
{
    QByteArray out;
    const bool tspl = s.thermalLang.compare(QLatin1String("tspl"), Qt::CaseInsensitive) == 0;
    const double dpi = s.thermalDpi > 0 ? s.thermalDpi : 203;
    QImage prev;
    int qty = 0;
    auto flush = [&]() {
        if (qty <= 0 || prev.isNull())
            return;
        out += tspl ? tsplLabel(prev, qty, doc.label.width, doc.label.height, s) : zplLabel(prev, qty, s);
    };
    if (!job.layout().roll) {
        // Several labels per printed page (e.g. small stickers on a 100x70 roll label):
        // rasterize whole pages; offsets and outlines are applied by renderPage().
        const SheetLayout& l = job.layout();
        const int wpx = std::max(1, int(std::lround(mmToDots(l.pageW, dpi))));
        const int hpx = std::max(1, int(std::lround(mmToDots(l.pageH, dpi))));
        const int pages = job.pageCount();
        for (int pg = 0; pg < pages; ++pg) {
            if (progress && !progress(pg, pages))
                return QByteArray();
            QImage rgb(wpx, hpx, QImage::Format_ARGB32_Premultiplied);
            rgb.fill(Qt::white);
            {
                QPainter p(&rgb);
                p.scale(dpi / MM_PER_INCH, dpi / MM_PER_INCH);
                job.renderPage(&p, pg, dpi);
            }
            QImage img(wpx, hpx, QImage::Format_Mono);
            img.setColor(0, qRgb(255, 255, 255));
            img.setColor(1, qRgb(0, 0, 0));
            img.fill(0);
            for (int y = 0; y < hpx; ++y) {
                const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
                for (int x = 0; x < wpx; ++x)
                    if (qGray(line[x]) < 128)
                        img.setPixel(x, y, 1);
            }
            if (!prev.isNull() && img == prev) {
                ++qty;
                continue;
            }
            if (qty > 0)
                out += tspl ? tsplLabel(prev, qty, l.pageW, l.pageH, s) : zplLabel(prev, qty, s);
            prev = img;
            qty = 1;
        }
        if (qty > 0)
            out += tspl ? tsplLabel(prev, qty, l.pageW, l.pageH, s) : zplLabel(prev, qty, s);
        return out;
    }

    const auto& inst = job.instances();
    for (int i = 0; i < inst.size(); ++i) {
        if (progress && !progress(i, inst.size()))
            return QByteArray();
        RenderOptions opt;
        opt.outline = s.printOutlines;
        QImage img = renderLabelImage(doc, job.contextFor(inst.at(i)), dpi, true, opt);
        if (s.offsetX != 0 || s.offsetY != 0) {
            QImage shifted(img.size(), QImage::Format_Mono);
            shifted.setColor(0, qRgb(255, 255, 255));
            shifted.setColor(1, qRgb(0, 0, 0));
            shifted.fill(0);
            const int dx = int(std::lround(mmToDots(s.offsetX, dpi)));
            const int dy = int(std::lround(mmToDots(s.offsetY, dpi)));
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (img.pixelIndex(x, y) == 1) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < img.width() && ny < img.height())
                            shifted.setPixel(nx, ny, 1);
                    }
            img = shifted;
        }
        if (!prev.isNull() && img == prev) {
            ++qty;
            continue;
        }
        flush();
        prev = img;
        qty = 1;
    }
    flush();
    return out;
}

bool sendRawToPrinter(const QString& printer, const QByteArray& data, const QString& docName, QString* err)
{
#ifdef Q_OS_WIN
    HANDLE h = nullptr;
    std::wstring name = printer.toStdWString();
    if (!OpenPrinterW(name.data(), &h, nullptr)) {
        *err = tr("Не удалось открыть принтер «%1» (код %2)").arg(printer).arg(GetLastError());
        return false;
    }
    std::wstring doc = docName.toStdWString();
    wchar_t raw[] = L"RAW";
    DOC_INFO_1W di;
    di.pDocName = doc.data();
    di.pOutputFile = nullptr;
    di.pDatatype = raw;
    bool ok = false;
    if (StartDocPrinterW(h, 1, reinterpret_cast<LPBYTE>(&di))) {
        if (StartPagePrinter(h)) {
            DWORD written = 0;
            ok = WritePrinter(h, const_cast<char*>(data.constData()), DWORD(data.size()), &written) &&
                 written == DWORD(data.size());
            EndPagePrinter(h);
        }
        EndDocPrinter(h);
    }
    if (!ok)
        *err = tr("Ошибка отправки данных на принтер (код %1)").arg(GetLastError());
    ClosePrinter(h);
    return ok;
#else
    QProcess lp;
    QStringList args{QStringLiteral("-o"), QStringLiteral("raw"), QStringLiteral("-t"), docName};
    if (!printer.isEmpty())
        args << QStringLiteral("-d") << printer;
    lp.start(QStringLiteral("lp"), args);
    if (!lp.waitForStarted()) {
        *err = tr("Не удалось запустить lp");
        return false;
    }
    lp.write(data);
    lp.closeWriteChannel();
    lp.waitForFinished(60000);
    if (lp.exitCode() != 0) {
        *err = QString::fromLocal8Bit(lp.readAllStandardError());
        return false;
    }
    return true;
#endif
}

} // namespace lf
