/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QObject>

namespace gui {

class Theme final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Theme)

public:
  Theme();

  const QIcon& getIcon(const QString& key, const QString& extension = QString{u"svg"},
                       bool useSvgIconEngine = true);
  void initStyle();
  bool isDark() const;

  static QColor errorColor();
  static QColor successColor();
  static QColor warningColor();

  // Design tokens for custom painting. The palette and stylesheets use the same values.
  enum class Color {
    Surface,    // lists and page content
    Raised,     // cards, headers, hover
    Sunken,     // progress track, poster placeholder
    Line,       // separators
    Text,
    Muted,      // secondary text
    Faint,      // placeholders, empty values
    Progress,   // watched episodes; the one strong color
    Available,  // episodes in library folders
    Accent,     // selection, from Windows when available
  };
  QColor color(Color token) const;

private:
  QString readStylesheet(const QString& name) const;
  void applyPalette() const;

  QHash<QString, QIcon> m_icons;
};

inline Theme theme;

}  // namespace gui
