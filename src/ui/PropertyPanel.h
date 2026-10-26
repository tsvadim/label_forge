// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QScrollArea>
#include <QStringList>
#include <QVector>
#include <functional>

class QFormLayout;
class QDoubleSpinBox;

namespace lf {

class LabelDocument;
class LabelObject;
class ImageObject;

// Context-sensitive editor for the selected objects or, with no selection,
// for the label itself (size, corners, background).
class PropertyPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    void setTarget(LabelDocument* doc, const QList<LabelObject*>& selection, const QStringList& fields);
    void refreshGeometry();

signals:
    // key identifies the edited property, used to merge consecutive undo steps
    void objectEdited(LabelObject* obj, const QString& key);
    void labelEdited(const QString& key);
    void layoutApplied();

private:
    void build();
    void buildLabel(QFormLayout* f);
    void buildCommon(QFormLayout* f, LabelObject* o);
    void buildText(QFormLayout* f, class TextObject* t);
    void buildShape(QFormLayout* f, class ShapeObject* s, bool rect);
    void buildLine(QFormLayout* f, class LineObject* l);
    void buildImage(QFormLayout* f, ImageObject* im);
    void buildBarcode(QFormLayout* f, class BarcodeObject* b);
    QDoubleSpinBox* spin(double min, double max, int decimals, double step, const QString& suffix,
                         std::function<double()> get, std::function<void(double)> set);
    QWidget* fieldMenuButton(std::function<void(const QString&)> insert);
    void edited(LabelObject* o, const QString& key);
    void rebuildLater();

    LabelDocument* m_doc = nullptr;
    QList<LabelObject*> m_sel;
    QStringList m_fields;
    QVector<std::function<void()>> m_refreshers;
    bool m_loading = false;
};

} // namespace lf
