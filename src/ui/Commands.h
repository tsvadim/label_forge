// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QUndoCommand>
#include <functional>

namespace lf {

// Undo step that stores the whole document before and after a change.
// Label documents are small, so snapshots are simple and robust.
class SnapshotCommand : public QUndoCommand {
public:
    using Apply = std::function<void(const QByteArray&)>;
    SnapshotCommand(const QByteArray& before, const QByteArray& after, const QString& text,
                    const QString& mergeKey, Apply apply);

    void undo() override;
    void redo() override;
    int id() const override { return m_id; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QByteArray m_before, m_after;
    QString m_key;
    int m_id = -1;
    bool m_first = true;
    Apply m_apply;
};

} // namespace lf
