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

#include "theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyleHints>

#include "base/file.hpp"
#include "base/string.hpp"
#include "gui/utils/svg_icon_engine.hpp"
#include "taiga/settings.hpp"

namespace gui {

Theme::Theme() : QObject() {}

const QIcon& Theme::getIcon(const QString& key, const QString& extension, bool useSvgIconEngine) {
  if (!m_icons.contains(key)) {
    if (extension == "svg" && useSvgIconEngine) {
      m_icons[key] = QIcon(new SvgIconEngine(key));
    } else {
      m_icons[key] = QIcon(u":/icons/%1.%2"_s.arg(key, extension));
    }
  }

  return m_icons[key];
}

void Theme::initStyle() {
  qApp->styleHints()->setColorScheme(taiga::settings.appColorScheme());

  const auto style = QString::fromStdString(taiga::settings.appStyle());
  if (style.compare(taiga::Settings::kAppStyleSystem, Qt::CaseInsensitive) != 0) {
    qApp->setStyle(style);
  }

  const auto apply = [this, style] {
    if (style.compare("fusion", Qt::CaseInsensitive) != 0) return;
    applyPalette();
#ifdef Q_OS_WINDOWS
    const QString mainStylesheet = readStylesheet("main");
    const QString themeStylesheet = readStylesheet(isDark() ? "dark" : "light");
    qApp->setStyleSheet(mainStylesheet + themeStylesheet);
#endif
  };
  apply();

  // Follow the system when it switches between light and dark.
  connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, apply);
}

QColor Theme::color(Color token) const {
  const bool dark = isDark();
  switch (token) {
    case Color::Surface: return dark ? QColor{0x1a1c21} : QColor{0xffffff};
    case Color::Raised: return dark ? QColor{0x23262c} : QColor{0xf4f5f7};
    case Color::Sunken: return dark ? QColor{0x15171b} : QColor{0xeaecef};
    case Color::Line: return dark ? QColor{0x2c3037} : QColor{0xe1e3e7};
    case Color::Text: return dark ? QColor{0xe4e6ea} : QColor{0x1c1f24};
    case Color::Muted: return dark ? QColor{0x9ba1ab} : QColor{0x5c636e};
    case Color::Faint: return dark ? QColor{0x6b717b} : QColor{0x969ca6};
    case Color::Progress: return dark ? QColor{0x3fa864} : QColor{0x2f9a55};
    case Color::Available: return dark ? QColor{0xd9a24a} : QColor{0xc28324};
    case Color::Accent: {
      // Windows' accent color reaches Qt through the platform palette.
      const auto accent = QGuiApplication::palette().color(QPalette::Accent);
      return accent.isValid() && accent.alpha() ? accent : QColor{0x4a78c2};
    }
  }
  return {};
}

void Theme::applyPalette() const {
  // Read the accent before replacing the palette, so it stays the system's.
  const auto accent = color(Color::Accent);

  QPalette palette;
  const auto set = [&palette](QPalette::ColorRole role, const QColor& value) {
    palette.setColor(QPalette::All, role, value);
  };
  const auto surface = color(Color::Surface);
  const auto raised = color(Color::Raised);
  const auto line = color(Color::Line);

  set(QPalette::Window, isDark() ? QColor{0x1e2025} : QColor{0xf7f8fa});
  set(QPalette::WindowText, color(Color::Text));
  set(QPalette::Base, surface);
  set(QPalette::AlternateBase, isDark() ? QColor{0x1d1f25} : QColor{0xfafbfc});
  set(QPalette::Text, color(Color::Text));
  set(QPalette::PlaceholderText, color(Color::Faint));
  set(QPalette::Button, raised);
  set(QPalette::ButtonText, color(Color::Text));
  set(QPalette::BrightText, Qt::white);
  set(QPalette::ToolTipBase, raised);
  set(QPalette::ToolTipText, color(Color::Text));
  set(QPalette::Light, isDark() ? raised.lighter(115) : QColor{0xffffff});
  set(QPalette::Midlight, raised);
  set(QPalette::Mid, line);
  set(QPalette::Dark, color(Color::Sunken));
  set(QPalette::Shadow, isDark() ? QColor{0x0e0f12} : QColor{0xc9ccd1});
  set(QPalette::Highlight, accent);
  set(QPalette::HighlightedText, Qt::white);
  set(QPalette::Accent, accent);
  set(QPalette::Link, accent);

  palette.setColor(QPalette::Disabled, QPalette::Text, color(Color::Faint));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, color(Color::Faint));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, color(Color::Faint));

  qApp->setPalette(palette);
}

bool Theme::isDark() const {
  return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QString Theme::readStylesheet(const QString& name) const {
  return base::readFile(u":/styles/%1.qss"_s.arg(name));
}

QColor Theme::errorColor() {
  return QColor(0xe5, 0x39, 0x35);  // Red 600
}

QColor Theme::successColor() {
  return QColor(0x43, 0xa0, 0x47);  // Green 600
}

QColor Theme::warningColor() {
  return QColor(0xfb, 0x8c, 0x00);  // Orange 600
}

}  // namespace gui
