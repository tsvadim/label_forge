// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImageEffects.h"

#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <vector>

namespace lf {

QImage applyAdjust(const QImage& src, const ImageAdjust& a)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    if (a.isIdentity() || img.isNull())
        return img;
    // contrast factor 0..4 around mid-grey, brightness as an offset
    const double c = std::pow((100.0 + std::clamp(a.contrast, -100, 100)) / 100.0, 2.0);
    const double b = std::clamp(a.brightness, -100, 100) * 2.55;
    uchar lut[256];
    for (int i = 0; i < 256; ++i)
        lut[i] = uchar(std::clamp(std::lround((i - 127.5) * c + 127.5 + b), 0L, 255L));
    const bool tint = a.tint.isValid();
    const bool gray = a.grayscale || tint;
    const QRgb t = tint ? a.tint.rgb() : 0;
    const int tintAlpha = tint ? a.tint.alpha() : 255;
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            int r = qRed(px), g = qGreen(px), bl = qBlue(px);
            if (gray)
                r = g = bl = qGray(r, g, bl);
            r = lut[r];
            g = lut[g];
            bl = lut[bl];
            if (tint) {
                const int alpha = qAlpha(px) * (255 - r) / 255 * tintAlpha / 255;
                line[x] = qRgba(qRed(t), qGreen(t), qBlue(t), alpha);
            } else {
                line[x] = qRgba(r, g, bl, qAlpha(px));
            }
        }
    }
    return img;
}

QImage ditherToMono(const QImage& src)
{
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    QImage out(w, h, QImage::Format_ARGB32);
    out.setDotsPerMeterX(img.dotsPerMeterX());
    out.setDotsPerMeterY(img.dotsPerMeterY());
    // error rows with one cell of padding on each side
    std::vector<double> cur(w + 2, 0.0), next(w + 2, 0.0);
    for (int y = 0; y < h; ++y) {
        std::fill(next.begin(), next.end(), 0.0);
        const QRgb* in = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int alpha = qAlpha(in[x]);
            if (alpha < 128) {
                o[x] = 0;
                continue;
            }
            const double v = (qGray(in[x]) * alpha + 255.0 * (255 - alpha)) / 255.0 + cur[x + 1];
            const bool black = v < 128;
            o[x] = black ? 0xFF000000u : 0xFFFFFFFFu;
            const double err = v - (black ? 0.0 : 255.0);
            cur[x + 2] += err * 7 / 16;
            next[x] += err * 3 / 16;
            next[x + 1] += err * 5 / 16;
            next[x + 2] += err * 1 / 16;
        }
        std::swap(cur, next);
    }
    return out;
}

namespace {

// One running-sum box pass over n values spaced `stride` apart.
void boxPass(std::vector<int>& a, int offset, int n, int stride, int r, std::vector<int>& tmp)
{
    tmp.resize(size_t(n));
    long sum = 0;
    const int win = 2 * r + 1;
    auto at = [&](int i) { return i < 0 || i >= n ? 0 : a[size_t(offset + i * stride)]; };
    for (int i = -r; i <= r; ++i)
        sum += at(i);
    for (int i = 0; i < n; ++i) {
        tmp[size_t(i)] = int(sum / win);
        sum += at(i + r + 1) - at(i - r);
    }
    for (int i = 0; i < n; ++i)
        a[size_t(offset + i * stride)] = tmp[size_t(i)];
}

} // namespace

QImage blurSilhouette(const QImage& src, int radiusPx, const QColor& color)
{
    const QImage in = src.convertToFormat(QImage::Format_ARGB32);
    const int w = in.width(), h = in.height();
    std::vector<int> alpha(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        for (int x = 0; x < w; ++x)
            alpha[size_t(y) * size_t(w) + size_t(x)] = qAlpha(line[x]);
    }
    const int r = std::max(1, radiusPx / 3);
    std::vector<int> tmp;
    if (radiusPx > 0) {
        for (int pass = 0; pass < 3; ++pass) {
            for (int y = 0; y < h; ++y)
                boxPass(alpha, y * w, w, 1, r, tmp);
            for (int x = 0; x < w; ++x)
                boxPass(alpha, x, h, w, r, tmp);
        }
    }
    QImage out(w, h, QImage::Format_ARGB32);
    const int ca = color.alpha();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            line[x] = qRgba(color.red(), color.green(), color.blue(),
                            alpha[size_t(y) * size_t(w) + size_t(x)] * ca / 255);
    }
    return out;
}

namespace {

bool keepColor(QStringView value)
{
    const QString s = value.trimmed().toString().toLower();
    if (s.isEmpty() || s == QLatin1String("none") || s == QLatin1String("transparent") ||
        s == QLatin1String("inherit") || s.startsWith(QLatin1String("url(")))
        return true;
    const QColor c(s);
    return c.isValid() && c.lightnessF() >= 0.95;
}

bool isColorProperty(QStringView name)
{
    return name == u"fill" || name == u"stroke" || name == u"stop-color" || name == u"color";
}

QString recolorCss(const QString& css, const QString& col)
{
    static const QRegularExpression re(QStringLiteral(R"((fill|stroke|stop-color|color)(\s*:\s*)([^;}"']+))"),
                                       QRegularExpression::CaseInsensitiveOption);
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(css);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += css.mid(last, m.capturedStart(3) - last);
        out += keepColor(m.capturedView(3)) ? m.captured(3) : col;
        last = m.capturedEnd(3);
    }
    out += css.mid(last);
    return out;
}

bool isStyleElement(QStringView qualifiedName)
{
    return qualifiedName == u"style" || qualifiedName.endsWith(u":style");
}

} // namespace

QByteArray recolorSvg(const QByteArray& svg, const QColor& color)
{
    if (!color.isValid())
        return svg;
    const QString col = color.name(QColor::HexRgb);
    QXmlStreamReader r(svg);
    r.setNamespaceProcessing(false);
    QByteArray out;
    QXmlStreamWriter w(&out);
    bool rootSeen = false;
    int styleDepth = 0;
    while (!r.atEnd()) {
        switch (r.readNext()) {
        case QXmlStreamReader::StartElement: {
            w.writeStartElement(r.qualifiedName().toString());
            QSet<QString> written;
            bool hasFill = false;
            for (const QXmlStreamAttribute& at : r.attributes()) {
                const QString name = at.qualifiedName().toString();
                QString value = at.value().toString();
                if (isColorProperty(name)) {
                    hasFill = hasFill || name == QLatin1String("fill");
                    if (!keepColor(value))
                        value = col;
                } else if (name == QLatin1String("style")) {
                    value = recolorCss(value, col);
                }
                w.writeAttribute(name, value);
                written.insert(name);
            }
            for (const QXmlStreamNamespaceDeclaration& ns : r.namespaceDeclarations()) {
                const QString name = ns.prefix().isEmpty() ? QStringLiteral("xmlns")
                                                           : QStringLiteral("xmlns:") + ns.prefix().toString();
                if (!written.contains(name))
                    w.writeAttribute(name, ns.namespaceUri().toString());
            }
            // shapes without an explicit fill inherit the root's (black by default)
            if (!rootSeen && !hasFill)
                w.writeAttribute(QStringLiteral("fill"), col);
            rootSeen = true;
            if (isStyleElement(r.qualifiedName()))
                ++styleDepth;
            break;
        }
        case QXmlStreamReader::EndElement:
            if (isStyleElement(r.qualifiedName()) && styleDepth > 0)
                --styleDepth;
            w.writeEndElement();
            break;
        case QXmlStreamReader::Characters:
            if (styleDepth > 0) {
                const QString css = recolorCss(r.text().toString(), col);
                if (r.isCDATA())
                    w.writeCDATA(css);
                else
                    w.writeCharacters(css);
            } else {
                w.writeCurrentToken(r);
            }
            break;
        case QXmlStreamReader::Invalid:
            return svg;
        default:
            w.writeCurrentToken(r);
            break;
        }
    }
    if (r.hasError() || !rootSeen)
        return svg;
    return out;
}

bool isSvgData(const QByteArray& data)
{
    if (data.size() >= 2 && uchar(data[0]) == 0x1f && uchar(data[1]) == 0x8b)
        return true;  // gzipped .svgz
    return data.left(4096).contains("<svg");
}

} // namespace lf
