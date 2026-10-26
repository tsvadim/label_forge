// SPDX-License-Identifier: GPL-3.0-or-later
#include "Commands.h"

#include <QHash>

namespace lf {

SnapshotCommand::SnapshotCommand(const QByteArray& before, const QByteArray& after, const QString& text,
                                 const QString& mergeKey, Apply apply)
    : QUndoCommand(text), m_before(before), m_after(after), m_key(mergeKey), m_apply(std::move(apply))
{
    if (!m_key.isEmpty())
        m_id = int(qHash(m_key) & 0x7fffffff) | 1;
}

void SnapshotCommand::undo() { m_apply(m_before); }

void SnapshotCommand::redo()
{
    if (m_first) {  // the change is already applied when the command is pushed
        m_first = false;
        return;
    }
    m_apply(m_after);
}

bool SnapshotCommand::mergeWith(const QUndoCommand* other)
{
    const auto* o = dynamic_cast<const SnapshotCommand*>(other);
    if (!o || o->m_key != m_key || m_key.isEmpty())
        return false;
    m_after = o->m_after;
    return true;
}

} // namespace lf
