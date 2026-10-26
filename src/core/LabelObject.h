// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <memory>

class QPainter;

namespace lf {

struct RenderContext;

// Base class of everything that can be placed on a label.
// Geometry is in millimetres: (x, y) is the top-left corner of the unrotated
// box, rotation (degrees, clockwise) is applied around the box centre.
class LabelObject {
public:
    virtual ~LabelObject() = default;

    virtual QString typeName() const = 0;     // stable id used in files
    virtual QString displayType() const = 0;  // localized, for the UI
    // Paints in local coordinates: (0,0)-(w,h) millimetres.
    virtual void paint(QPainter* p, const RenderContext& ctx) const = 0;

    virtual QJsonObject toJson() const;
    virtual void fromJson(const QJsonObject& o);

    std::unique_ptr<LabelObject> clone() const;
    static std::unique_ptr<LabelObject> create(const QString& typeName);
    static std::unique_ptr<LabelObject> fromJsonAny(const QJsonObject& o);

    QRectF rect() const { return QRectF(x, y, w, h); }

    QString name;
    double x = 0, y = 0, w = 20, h = 10;
    double rotation = 0;
    bool locked = false;
    bool printable = true;
};

class TextObject : public LabelObject {
public:
    TextObject();
    QString typeName() const override { return QStringLiteral("text"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;

    QString text;
    QString fontFamily = QStringLiteral("Arial");
    double fontSize = 12;  // points
    bool bold = false, italic = false, underline = false;
    QColor color = Qt::black;
    QColor fill = Qt::transparent;
    int hAlign = 0, vAlign = 0;
    bool wordWrap = true;
    bool autoShrink = false;
    double lineSpacing = 1.0;

private:
    mutable QString m_cacheKey;
    mutable QPainterPath m_cachePath;
};

class ShapeObject : public LabelObject {
public:
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    QColor stroke = Qt::black;
    double strokeWidth = 0.3;  // mm, 0 = no outline
    QColor fill = Qt::transparent;
};

class RectObject : public ShapeObject {
public:
    QString typeName() const override { return QStringLiteral("rect"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    double radius = 0;
};

class EllipseObject : public ShapeObject {
public:
    QString typeName() const override { return QStringLiteral("ellipse"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
};

// Horizontal line through the middle of its box; rotate it for other angles.
class LineObject : public LabelObject {
public:
    LineObject();
    QString typeName() const override { return QStringLiteral("line"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    QColor color = Qt::black;
    double width = 0.3;
    int penStyle = 1;  // Qt::PenStyle
};

class ImageObject : public LabelObject {
public:
    QString typeName() const override { return QStringLiteral("image"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    void setEmbedded(const QImage& img);

    QByteArray imageData;  // embedded PNG
    QString pathTemplate;  // or a file path, may contain {Field}
    bool keepAspect = true;
    bool monochrome = false;

private:
    mutable QImage m_embedded;
    mutable QByteArray m_embeddedKey;
    mutable QString m_fileKey;
    mutable QImage m_file;
};

class BarcodeObject : public LabelObject {
public:
    BarcodeObject();
    QString typeName() const override { return QStringLiteral("barcode"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;

    QString symbology = QStringLiteral("code128");
    QString data = QStringLiteral("1234567890");
    bool showText = true;
    QString fontFamily = QStringLiteral("Arial");
    double textSize = 8;
    QColor color = Qt::black;
    bool quietZone = true;
    int qrEcc = 1;           // 0 L, 1 M, 2 Q, 3 H
    bool code39Check = false;
};

} // namespace lf
