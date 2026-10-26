// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextLayout.h"
#include "RenderContext.h"

#include <QFont>
#include <QFontMetricsF>
#include <QTextLayout>
#include <QTransform>
#include <QVector>
#include <algorithm>

namespace lf {

namespace {
constexpr int REF_PX = 200;  // reference em size, layout happens in these units

struct Line {
    QString text;
    double width;
};

double doLayout(const QStringList& paragraphs, const QFont& f, const QFontMetricsF& fm,
                bool wrap, double availRef, QVector<Line>& lines)
{
    lines.clear();
    double maxW = 0;
    for (const QString& para : paragraphs) {
        if (!wrap || availRef <= 1 || para.isEmpty()) {
            const double w = fm.horizontalAdvance(para);
            lines.push_back({para, w});
            maxW = std::max(maxW, w);
            continue;
        }
        QTextLayout tl(para, f);
        QTextOption opt;
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        tl.setTextOption(opt);
        tl.beginLayout();
        for (;;) {
            QTextLine ln = tl.createLine();
            if (!ln.isValid())
                break;
            ln.setLineWidth(availRef);
            QString s = para.mid(ln.textStart(), ln.textLength());
            while (s.endsWith(QLatin1Char(' ')))
                s.chop(1);
            const double w = fm.horizontalAdvance(s);
            lines.push_back({s, w});
            maxW = std::max(maxW, w);
        }
        tl.endLayout();
    }
    return maxW;
}
} // namespace

QPainterPath layoutText(const QString& text, const TextStyle& st, double boxW, double boxH)
{
    QFont f(st.family);
    f.setPixelSize(REF_PX);
    f.setBold(st.bold);
    f.setItalic(st.italic);
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setStyleStrategy(QFont::StyleStrategy(QFont::ForceOutline | QFont::PreferQuality));
    const QFontMetricsF fm(f);

    double scale = std::max(0.1, st.sizePt) * PT_TO_MM / REF_PX;  // mm per reference unit
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const QStringList paragraphs = normalized.split(QLatin1Char('\n'));
    const double lineH = fm.lineSpacing() * std::max(0.3, st.lineSpacing);
    const double blockExtra = fm.ascent() + fm.descent();

    QVector<Line> lines;
    double maxW = doLayout(paragraphs, f, fm, st.wordWrap, boxW / scale, lines);
    auto totalHeight = [&]() { return (lines.size() - 1) * lineH + blockExtra; };

    if (st.autoShrink && boxW > 0 && boxH > 0) {
        if (!st.wordWrap) {
            const double k = std::min(boxW / std::max(1e-6, maxW * scale),
                                      boxH / std::max(1e-6, totalHeight() * scale));
            if (k < 1.0)
                scale *= k;
        } else {
            for (int iter = 0; iter < 60; ++iter) {
                if (maxW * scale <= boxW + 1e-6 && totalHeight() * scale <= boxH + 1e-6)
                    break;
                scale *= 0.95;
                maxW = doLayout(paragraphs, f, fm, true, boxW / scale, lines);
            }
        }
    }

    const double availRef = boxW / scale;
    QPainterPath path;
    double y = fm.ascent();
    for (const Line& l : lines) {
        double x = 0;
        if (st.hAlign == 1)
            x = (availRef - l.width) / 2.0;
        else if (st.hAlign == 2)
            x = availRef - l.width;
        if (!l.text.isEmpty()) {
            path.addText(QPointF(x, y), f, l.text);
            if (st.underline) {
                const double th = std::max(fm.lineWidth(), REF_PX * 0.05);
                path.addRect(QRectF(x, y + fm.underlinePos(), l.width, th));
            }
        }
        y += lineH;
    }

    const double boxHRef = boxH / scale;
    const double th = totalHeight();
    double offY = 0;
    if (st.vAlign == 1)
        offY = (boxHRef - th) / 2.0;
    else if (st.vAlign == 2)
        offY = boxHRef - th;

    QTransform t;
    t.scale(scale, scale);
    t.translate(0, offY);
    QPainterPath out = t.map(path);
    out.setFillRule(Qt::WindingFill);
    return out;
}

} // namespace lf
