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

#include <QStringList>
#include <functional>
#include <vector>

#include "gui/settings/settings_page.hpp"
#include "taiga/options.hpp"

class QFormLayout;
class QWidget;

namespace gui {

// A settings page built in code from `taiga::opt` options, for pages that are only a list of
// checkboxes and fields.
class SettingsPageOptions final : public SettingsPage {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SettingsPageOptions)

public:
  SettingsPageOptions(Ui::SettingsDialog* ui, QDialog* dialog);

  QWidget* widget() const {
    return widget_;
  }

  void addHeading(const QString& text);
  void addCheck(const taiga::opt::Bool& option, const QString& label);
  void addChoice(const taiga::opt::Int& option, const QString& label, const QStringList& choices);
  void addSpin(const taiga::opt::Int& option, const QString& label, int min, int max,
               const QString& suffix = {});
  void addText(const taiga::opt::Str& option, const QString& label, const QString& hint = {});
  void addNote(const QString& text);
  // Anything else; `load`/`apply` may be empty.
  void addRow(const QString& label, QWidget* field, std::function<void()> load = {},
              std::function<void()> apply = {});

  void load() override;
  void apply() const override;

private:
  QWidget* widget_ = nullptr;
  QFormLayout* layout_ = nullptr;
  std::vector<std::function<void()>> loaders_;
  std::vector<std::function<void()>> appliers_;
};

}  // namespace gui
