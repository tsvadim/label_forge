// SPDX-License-Identifier: GPL-3.0-or-later
#include "Icons.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace lf::icons {

namespace {
const QColor INK(52, 58, 64);
const QColor ACCENT(33, 113, 181);

void drawBars(QPainter& p, const QRectF& r)
{
    const int pattern[] = {2, 1, 1, 2, 3, 1, 1, 1, 2, 2, 1, 3, 1, 1, 2};
    double x = r.left();
    bool bar = true;
    const double unit = r.width() / 25.0;
    for (int w : pattern) {
        if (bar)
            p.fillRect(QRectF(x, r.top(), w * unit, r.height()), INK);
        x += w * unit;
        bar = !bar;
        if (x > r.right())
            break;
    }
}

void drawQr(QPainter& p, const QRectF& r)
{
    const double m = r.width() / 7.0;
    auto finder = [&](double x, double y) {
        p.fillRect(QRectF(x, y, 3 * m, 3 * m), INK);
        p.fillRect(QRectF(x + m * 0.6, y + m * 0.6, 1.8 * m, 1.8 * m), Qt::white);
        p.fillRect(QRectF(x + m, y + m, m, m), INK);
    };
    finder(r.left(), r.top());
    finder(r.right() - 3 * m, r.top());
    finder(r.left(), r.bottom() - 3 * m);
    const int dots[][2] = {{4, 4}, {5, 5}, {6, 4}, {4, 6}, {6, 6}, {5, 3}};
    for (auto& d : dots)
        p.fillRect(QRectF(r.left() + d[0] * m, r.top() + d[1] * m, m, m), INK);
}

QPixmap paint(const QString& name, int s)
{
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const double u = s / 32.0;
    p.scale(u, u);
    QPen pen(INK, 2.2);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QFont f(QStringLiteral("Arial"));
    f.setPixelSize(24);
    f.setBold(true);

    if (name == QLatin1String("select")) {
        QPainterPath a;
        a.moveTo(9, 4);
        a.lineTo(9, 25);
        a.lineTo(14, 20);
        a.lineTo(18, 28);
        a.lineTo(21, 27);
        a.lineTo(17, 19);
        a.lineTo(24, 19);
        a.closeSubpath();
        p.setBrush(Qt::white);
        p.drawPath(a);
    } else if (name == QLatin1String("text")) {
        p.setFont(f);
        p.drawText(QRectF(0, 0, 32, 32), Qt::AlignCenter, QStringLiteral("T"));
    } else if (name == QLatin1String("rect")) {
        p.drawRoundedRect(QRectF(5, 8, 22, 16), 2, 2);
    } else if (name == QLatin1String("ellipse")) {
        p.drawEllipse(QRectF(5, 7, 22, 18));
    } else if (name == QLatin1String("line")) {
        p.drawLine(QPointF(5, 26), QPointF(27, 6));
    } else if (name == QLatin1String("image")) {
        p.drawRoundedRect(QRectF(4, 6, 24, 20), 2, 2);
        p.setBrush(INK);
        QPainterPath m;
        m.moveTo(7, 23);
        m.lineTo(13, 14);
        m.lineTo(18, 20);
        m.lineTo(21, 16);
        m.lineTo(25, 23);
        m.closeSubpath();
        p.drawPath(m);
        p.drawEllipse(QPointF(22, 11), 2, 2);
    } else if (name == QLatin1String("barcode")) {
        drawBars(p, QRectF(4, 6, 24, 20));
    } else if (name == QLatin1String("qr")) {
        drawQr(p, QRectF(5, 5, 22, 22));
    } else if (name == QLatin1String("new")) {
        p.drawRoundedRect(QRectF(6, 9, 20, 15), 2, 2);
        p.setPen(QPen(ACCENT, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(24, 3), QPointF(24, 11));
        p.drawLine(QPointF(20, 7), QPointF(28, 7));
    } else if (name == QLatin1String("open")) {
        QPainterPath fo;
        fo.moveTo(4, 9);
        fo.lineTo(12, 9);
        fo.lineTo(14, 12);
        fo.lineTo(28, 12);
        fo.lineTo(28, 25);
        fo.lineTo(4, 25);
        fo.closeSubpath();
        p.drawPath(fo);
    } else if (name == QLatin1String("save")) {
        p.drawRoundedRect(QRectF(5, 5, 22, 22), 2, 2);
        p.drawRect(QRectF(10, 5, 12, 7));
        p.drawRect(QRectF(9, 17, 14, 10));
    } else if (name == QLatin1String("print")) {
        p.drawRect(QRectF(9, 4, 14, 8));
        p.drawRoundedRect(QRectF(4, 12, 24, 10), 2, 2);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(9, 18, 14, 10));
    } else if (name == QLatin1String("pdf")) {
        p.drawRect(QRectF(7, 3, 18, 26));
        QFont sf = f;
        sf.setPixelSize(9);
        p.setFont(sf);
        p.setPen(QColor(200, 30, 30));
        p.drawText(QRectF(7, 10, 18, 12), Qt::AlignCenter, QStringLiteral("PDF"));
    } else if (name == QLatin1String("data")) {
        p.drawEllipse(QRectF(6, 4, 20, 7));
        p.drawLine(QPointF(6, 7.5), QPointF(6, 24));
        p.drawLine(QPointF(26, 7.5), QPointF(26, 24));
        p.drawArc(QRectF(6, 20.5, 20, 7), 180 * 16, 180 * 16);
        p.drawArc(QRectF(6, 13, 20, 7), 180 * 16, 180 * 16);
    } else if (name == QLatin1String("undo") || name == QLatin1String("redo")) {
        if (name == QLatin1String("redo")) {
            p.translate(32, 0);
            p.scale(-1, 1);
        }
        QPainterPath a;
        a.moveTo(9, 14);
        a.cubicTo(13, 7, 27, 7, 26, 20);
        p.drawPath(a);
        p.setBrush(INK);
        QPainterPath h;
        h.moveTo(4, 10);
        h.lineTo(12, 18);
        h.lineTo(13, 9);
        h.closeSubpath();
        p.drawPath(h);
    } else if (name == QLatin1String("zoom-in") || name == QLatin1String("zoom-out") ||
               name == QLatin1String("zoom-fit")) {
        p.drawEllipse(QRectF(4, 4, 18, 18));
        p.drawLine(QPointF(19, 19), QPointF(28, 28));
        if (name != QLatin1String("zoom-fit"))
            p.drawLine(QPointF(9, 13), QPointF(17, 13));
        if (name == QLatin1String("zoom-in"))
            p.drawLine(QPointF(13, 9), QPointF(13, 17));
        if (name == QLatin1String("zoom-fit"))
            p.drawRect(QRectF(9, 9, 8, 8));
    } else if (name.startsWith(QLatin1String("align-"))) {
        p.setPen(QPen(ACCENT, 2.2));
        const QString a = name.mid(6);
        if (a == QLatin1String("left"))
            p.drawLine(QPointF(5, 3), QPointF(5, 29));
        else if (a == QLatin1String("hcenter"))
            p.drawLine(QPointF(16, 3), QPointF(16, 29));
        else if (a == QLatin1String("right"))
            p.drawLine(QPointF(27, 3), QPointF(27, 29));
        else if (a == QLatin1String("top"))
            p.drawLine(QPointF(3, 5), QPointF(29, 5));
        else if (a == QLatin1String("vcenter"))
            p.drawLine(QPointF(3, 16), QPointF(29, 16));
        else
            p.drawLine(QPointF(3, 27), QPointF(29, 27));
        p.setPen(Qt::NoPen);
        p.setBrush(INK);
        const bool vertical = a == QLatin1String("left") || a == QLatin1String("hcenter") ||
                              a == QLatin1String("right");
        if (vertical) {
            const double x1 = a == QLatin1String("left") ? 7 : a == QLatin1String("right") ? 25 - 16 : 8;
            const double x2 = a == QLatin1String("left") ? 7 : a == QLatin1String("right") ? 25 - 10 : 11;
            p.drawRect(QRectF(x1, 7, 16, 6));
            p.drawRect(QRectF(x2, 18, 10, 6));
        } else {
            const double y1 = a == QLatin1String("top") ? 7 : a == QLatin1String("bottom") ? 25 - 16 : 8;
            const double y2 = a == QLatin1String("top") ? 7 : a == QLatin1String("bottom") ? 25 - 10 : 11;
            p.drawRect(QRectF(7, y1, 6, 16));
            p.drawRect(QRectF(18, y2, 6, 10));
        }
    } else if (name == QLatin1String("front") || name == QLatin1String("back")) {
        const bool front = name == QLatin1String("front");
        p.setBrush(front ? Qt::white : QBrush(ACCENT));
        p.drawRect(QRectF(4, 4, 16, 16));
        p.setBrush(front ? QBrush(ACCENT) : Qt::white);
        p.drawRect(QRectF(12, 12, 16, 16));
    } else if (name == QLatin1String("delete")) {
        p.setPen(QPen(QColor(190, 40, 40), 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(8, 8), QPointF(24, 24));
        p.drawLine(QPointF(24, 8), QPointF(8, 24));
    } else if (name == QLatin1String("template")) {
        p.drawRoundedRect(QRectF(4, 4, 24, 24), 2, 2);
        p.setBrush(INK);
        p.drawRect(QRectF(8, 8, 16, 4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(8, 15, 7, 9));
        p.drawRect(QRectF(18, 15, 6, 9));
    } else if (name == QLatin1String("grid")) {
        p.setPen(QPen(INK, 1.6));
        for (int i = 6; i <= 26; i += 5) {
            p.drawLine(QPointF(i, 4), QPointF(i, 28));
            p.drawLine(QPointF(4, i), QPointF(28, i));
        }
    } else if (name == QLatin1String("snap")) {
        // horseshoe magnet
        QPen mp(QColor(190, 40, 40), 5.0, Qt::SolidLine, Qt::FlatCap);
        p.setPen(mp);
        p.drawArc(QRectF(8, 8, 16, 16), 180 * 16, 180 * 16);
        p.drawLine(QPointF(8, 16), QPointF(8, 8));
        p.drawLine(QPointF(24, 16), QPointF(24, 8));
        p.setPen(QPen(QColor(150, 150, 150), 5.0, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(8, 8), QPointF(8, 4));
        p.drawLine(QPointF(24, 8), QPointF(24, 4));
    } else if (name == QLatin1String("label")) {
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(3, 7, 26, 18), 4, 4);
        drawBars(p, QRectF(8, 11, 16, 10));
    }
    p.end();
    return pm;
}
} // namespace

QIcon get(const QString& name)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.find(name);
    if (it != cache.end())
        return *it;
    QIcon icon;
    for (int s : {16, 24, 32, 48})
        icon.addPixmap(paint(name, s));
    cache.insert(name, icon);
    return icon;
}

QIcon appIcon()
{
    QIcon icon;
    for (int s : {16, 32, 48, 64, 128, 256}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(s / 32.0, s / 32.0);
        p.setPen(Qt::NoPen);
        p.setBrush(ACCENT);
        p.drawRoundedRect(QRectF(1, 4, 30, 24), 5, 5);
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(4, 7, 24, 18), 3, 3);
        drawBars(p, QRectF(7, 10, 18, 12));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QIcon colorSwatch(const QColor& c)
{
    QPixmap pm(20, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    // checkerboard for transparency
    for (int y = 0; y < 14; y += 4)
        for (int x = 0; x < 20; x += 4)
            p.fillRect(x, y, 4, 4, ((x + y) / 4) % 2 ? QColor(200, 200, 200) : Qt::white);
    p.fillRect(pm.rect(), c);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    p.end();
    return QIcon(pm);
}

} // namespace lf::icons
