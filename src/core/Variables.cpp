// SPDX-License-Identifier: GPL-3.0-or-later
#include "RenderContext.h"

#include <QLocale>
#include <QRegularExpression>

namespace lf {

bool RenderContext::fieldValue(const QString& name, QString* out) const
{
    if (!columns || !values)
        return false;
    const QString key = name.trimmed();
    for (int i = 0; i < columns->size(); ++i) {
        if (columns->at(i).compare(key, Qt::CaseInsensitive) == 0) {
            *out = i < values->size() ? values->at(i) : QString();
            return true;
        }
    }
    return false;
}

static QString formatField(const QString& value, const QString& arg)
{
    if (arg.isEmpty())
        return value;
    const QString a = arg.trimmed();
    if (a.compare(QLatin1String("upper"), Qt::CaseInsensitive) == 0)
        return value.toUpper();
    if (a.compare(QLatin1String("lower"), Qt::CaseInsensitive) == 0)
        return value.toLower();
    if (a.compare(QLatin1String("trim"), Qt::CaseInsensitive) == 0)
        return value.trimmed();
    static const QRegularExpression numFmt(QStringLiteral("^0+([.,](0+))?$"));
    const auto m = numFmt.match(a);
    if (m.hasMatch()) {
        QString v = value.trimmed();
        v.replace(QLatin1Char(','), QLatin1Char('.'));
        v.remove(QLatin1Char(' '));
        bool ok = false;
        const double d = v.toDouble(&ok);
        if (!ok)
            return value;
        const int decimals = m.captured(2).size();
        QString s = QString::number(d, 'f', decimals);
        if (a.contains(QLatin1Char(',')))
            s.replace(QLatin1Char('.'), QLatin1Char(','));
        const int intDigits = a.indexOf(QRegularExpression(QStringLiteral("[.,]"))) < 0
                                  ? a.size()
                                  : a.indexOf(QRegularExpression(QStringLiteral("[.,]")));
        // zero-pad the integer part if the format asks for it, e.g. {N:000}
        int curInt = s.indexOf(QRegularExpression(QStringLiteral("[.,]")));
        if (curInt < 0)
            curInt = s.size();
        const bool neg = s.startsWith(QLatin1Char('-'));
        const int digits = curInt - (neg ? 1 : 0);
        if (digits < intDigits)
            s.insert(neg ? 1 : 0, QString(intDigits - digits, QLatin1Char('0')));
        return s;
    }
    return value;
}

static bool evalVariable(const QString& expr, const RenderContext& ctx, QString* out)
{
    // 1) exact data field (the whole expression), so fields may contain ':'
    if (ctx.fieldValue(expr, out))
        return true;

    const int colon = expr.indexOf(QLatin1Char(':'));
    const QString name = (colon < 0 ? expr : expr.left(colon)).trimmed();
    const QString arg = colon < 0 ? QString() : expr.mid(colon + 1);

    // 2) data field with a format argument
    QString raw;
    if (colon >= 0 && ctx.fieldValue(name, &raw)) {
        *out = formatField(raw, arg);
        return true;
    }

    const QString lname = name.toLower();
    if (lname == QLatin1String("seq") || lname == QLatin1String("#")) {
        QString startStr = QStringLiteral("1");
        long long step = 1;
        if (!arg.isEmpty()) {
            const QStringList parts = arg.split(QLatin1Char(':'));
            startStr = parts.value(0).trimmed();
            if (parts.size() > 1)
                step = parts.at(1).trimmed().toLongLong();
        }
        bool ok = false;
        const long long start = startStr.toLongLong(&ok);
        if (!ok)
            return false;
        const long long v = start + static_cast<long long>(ctx.sequence - 1) * step;
        QString s = QString::number(v);
        if (startStr.size() > s.size() && v >= 0)
            s = s.rightJustified(startStr.size(), QLatin1Char('0'));
        *out = s;
        return true;
    }
    if (lname == QLatin1String("rec")) {
        *out = QString::number(ctx.recordNumber);
        return true;
    }
    static const QRegularExpression dateRe(QStringLiteral("^date\\s*([+-]\\s*\\d+)?$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const auto dm = dateRe.match(name);
    if (dm.hasMatch()) {
        QDate d = ctx.now.date();
        if (!dm.captured(1).isEmpty()) {
            QString off = dm.captured(1);
            off.remove(QLatin1Char(' '));
            d = d.addDays(off.toLongLong());
        }
        *out = QLocale::system().toString(d, arg.isEmpty() ? QStringLiteral("dd.MM.yyyy") : arg);
        return true;
    }
    if (lname == QLatin1String("time")) {
        *out = QLocale::system().toString(ctx.now.time(), arg.isEmpty() ? QStringLiteral("HH:mm") : arg);
        return true;
    }
    return false;
}

QString expandTemplate(const QString& tpl, const RenderContext& ctx)
{
    if (ctx.rawTemplates || (!tpl.contains(QLatin1Char('{')) && !tpl.contains(QLatin1Char('}'))))
        return tpl;
    QString out;
    out.reserve(tpl.size());
    for (int i = 0; i < tpl.size(); ++i) {
        const QChar c = tpl.at(i);
        if (c == QLatin1Char('{')) {
            if (i + 1 < tpl.size() && tpl.at(i + 1) == QLatin1Char('{')) {
                out += QLatin1Char('{');
                ++i;
                continue;
            }
            const int end = tpl.indexOf(QLatin1Char('}'), i + 1);
            if (end < 0) {
                out += tpl.mid(i);
                break;
            }
            const QString expr = tpl.mid(i + 1, end - i - 1);
            QString rep;
            if (evalVariable(expr, ctx, &rep))
                out += rep;
            else
                out += tpl.mid(i, end - i + 1);
            i = end;
            continue;
        }
        if (c == QLatin1Char('}') && i + 1 < tpl.size() && tpl.at(i + 1) == QLatin1Char('}')) {
            out += QLatin1Char('}');
            ++i;
            continue;
        }
        out += c;
    }
    return out;
}

QStringList builtinVariables()
{
    return {QStringLiteral("{seq}"),          QStringLiteral("{seq:0001}"),
            QStringLiteral("{rec}"),          QStringLiteral("{date}"),
            QStringLiteral("{date+30}"),      QStringLiteral("{date:dd.MM.yy}"),
            QStringLiteral("{time}"),         QStringLiteral("{time:HH:mm:ss}")};
}

} // namespace lf
