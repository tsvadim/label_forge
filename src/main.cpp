// LabelForge — open-source label designer
// Copyright (C) 2026 LabelForge contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later version.

#include "core/LabelDocument.h"
#include "data/DataLoader.h"
#include "print/PrintJob.h"
#include "print/ThermalExport.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QPrinter>
#include <QPrinterInfo>
#include <QTranslator>
#include <cstdio>

using namespace lf;

static int fail(const QString& msg)
{
    std::fprintf(stderr, "LabelForge: %s\n", msg.toLocal8Bit().constData());
    return 1;
}

// Batch mode: LabelForge label.lfl --pdf out.pdf [--data file.csv] ...
static int runBatch(const QCommandLineParser& p, const QString& file)
{
    LabelDocument doc;
    QString err;
    if (!doc.load(file, &err))
        return fail(err);
    const QString baseDir = QFileInfo(file).absolutePath();

    if (p.isSet(QStringLiteral("data"))) {
        const QString dataPath = QFileInfo(p.value(QStringLiteral("data"))).absoluteFilePath();
        const bool xlsx = dataPath.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive) ||
                          dataPath.endsWith(QStringLiteral(".xlsm"), Qt::CaseInsensitive);
        doc.data = DataSourceConfig();
        doc.data.kind = xlsx ? DataKind::Excel : DataKind::Csv;
        doc.data.path = dataPath;
        doc.data.sheet = p.value(QStringLiteral("sheet"));
    }
    if (p.isSet(QStringLiteral("layout")))
        doc.print.layoutId = p.value(QStringLiteral("layout"));
    if (p.isSet(QStringLiteral("copies")))
        doc.print.copies = std::max(1, p.value(QStringLiteral("copies")).toInt());
    if (p.isSet(QStringLiteral("records")))
        doc.print.recordRange = p.value(QStringLiteral("records"));
    if (p.isSet(QStringLiteral("dpi")))
        doc.print.thermalDpi = p.value(QStringLiteral("dpi")).toInt();

    DataTable data;
    if (!loadData(doc.data, baseDir, &data, &err))
        return fail(err);
    PrintJob job(doc, data, doc.print, baseDir);

    if (p.isSet(QStringLiteral("pdf")) && !job.exportPdf(p.value(QStringLiteral("pdf")), &err))
        return fail(err);
    if (p.isSet(QStringLiteral("png")) && !job.exportPng(p.value(QStringLiteral("png")), 300, &err))
        return fail(err);
    auto thermal = [&](const QString& lang) {
        PrintSettings s = doc.print;
        s.thermalLang = lang;
        return buildThermalJob(job, doc, s);
    };
    for (const QString& lang : {QStringLiteral("zpl"), QStringLiteral("tspl")}) {
        if (!p.isSet(lang))
            continue;
        QFile f(p.value(lang));
        if (!f.open(QIODevice::WriteOnly))
            return fail(f.errorString());
        f.write(thermal(lang));
    }
    if (p.isSet(QStringLiteral("raw"))) {
        const QString lang = p.value(QStringLiteral("lang")).isEmpty() ? doc.print.thermalLang : p.value(QStringLiteral("lang"));
        if (!sendRawToPrinter(p.value(QStringLiteral("raw")), thermal(lang), QFileInfo(file).completeBaseName(), &err))
            return fail(err);
    }
    if (p.isSet(QStringLiteral("print"))) {
        QPrinter printer(QPrinter::HighResolution);
        const QString name = p.value(QStringLiteral("print"));
        printer.setPrinterName(name.isEmpty() || name == QLatin1String("default") ? QPrinterInfo::defaultPrinterName() : name);
        printer.setDocName(QFileInfo(file).completeBaseName());
        if (!job.printTo(&printer, &err))
            return fail(err);
    }
    std::printf("labels: %d, pages: %d\n", job.labelCount(), job.pageCount());
    return 0;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LabelForge"));
    QApplication::setOrganizationName(QStringLiteral("LabelForge"));
    QApplication::setApplicationVersion(QStringLiteral(LABELFORGE_VERSION));
    QApplication::setStyle(QStringLiteral("Fusion"));

    // standard Qt dialogs in the user's language, if translations are deployed
    // The interface is in Russian, so standard Qt buttons/dialogs use the Russian catalogue.
    QTranslator qtTr;
    const QStringList trDirs{QApplication::applicationDirPath() + QStringLiteral("/translations"),
                             QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
    for (const QString& dir : trDirs)
        if (qtTr.load(QStringLiteral("qtbase_ru"), dir)) {
            QApplication::installTranslator(&qtTr);
            break;
        }

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("LabelForge — label designer. Batch mode: pass a .lfl file and an output option."));
    p.addHelpOption();
    p.addVersionOption();
    p.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Label file (.lfl) to open"));
    p.addOptions({
        {QStringLiteral("pdf"), QStringLiteral("Export to PDF and exit."), QStringLiteral("out.pdf")},
        {QStringLiteral("png"), QStringLiteral("Export PNG per label (%1 = number) and exit."), QStringLiteral("pattern")},
        {QStringLiteral("zpl"), QStringLiteral("Write ZPL commands and exit."), QStringLiteral("out.zpl")},
        {QStringLiteral("tspl"), QStringLiteral("Write TSPL commands and exit."), QStringLiteral("out.prn")},
        {QStringLiteral("raw"), QStringLiteral("Send ZPL/TSPL directly to a printer queue."), QStringLiteral("printer")},
        {QStringLiteral("lang"), QStringLiteral("Thermal language for --raw: zpl or tspl."), QStringLiteral("lang")},
        {QStringLiteral("print"), QStringLiteral("Print through the system driver ('default' for the default printer)."),
         QStringLiteral("printer")},
        {QStringLiteral("data"), QStringLiteral("Use this CSV/XLSX file as the data source."), QStringLiteral("file")},
        {QStringLiteral("sheet"), QStringLiteral("Excel sheet name for --data."), QStringLiteral("name")},
        {QStringLiteral("layout"), QStringLiteral("Sheet layout id (e.g. avery-l7160, roll)."), QStringLiteral("id")},
        {QStringLiteral("copies"), QStringLiteral("Copies of every record."), QStringLiteral("n")},
        {QStringLiteral("records"), QStringLiteral("Record range, e.g. 1-10,15."), QStringLiteral("range")},
        {QStringLiteral("dpi"), QStringLiteral("Thermal printer resolution (203, 300, 600)."), QStringLiteral("dpi")},
    });
    p.process(app);

    const QStringList args = p.positionalArguments();
    const bool batch = p.isSet(QStringLiteral("pdf")) || p.isSet(QStringLiteral("png")) || p.isSet(QStringLiteral("zpl")) ||
                       p.isSet(QStringLiteral("tspl")) || p.isSet(QStringLiteral("raw")) || p.isSet(QStringLiteral("print"));
    if (batch) {
        if (args.isEmpty())
            return fail(QStringLiteral("no label file given"));
        return runBatch(p, args.first());
    }

    MainWindow w;
    w.show();
    if (!args.isEmpty())
        w.openFile(args.first());
    return app.exec();
}
