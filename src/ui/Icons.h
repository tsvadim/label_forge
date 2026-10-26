// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace lf::icons {

// Toolbar icons are drawn in code so the app ships without image resources.
QIcon get(const QString& name);
QIcon appIcon();
QIcon colorSwatch(const QColor& c);

} // namespace lf::icons
