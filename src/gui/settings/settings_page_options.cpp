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

#include "settings_page_options.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "ui_settings_dialog.h"

namespace gui {

SettingsPageOptions::SettingsPageOptions(Ui::SettingsDialog* ui, QDialog* dialog)
    : SettingsPage(ui, dialog) {
  auto* scroll = new QScrollArea(ui_->stackedWidget);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto* content = new QWidget(scroll);
  layout_ = new QFormLayout(content);
  layout_->setRowWrapPolicy(QFormLayout::DontWrapRows);
  layout_->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  scroll->setWidget(content);
  ui_->stackedWidget->addWidget(scroll);
  widget_ = scroll;
}

void SettingsPageOptions::addHeading(const QString& text) {
  auto* label = new QLabel(text, widget_);
  auto font = label->font();
  font.setWeight(QFont::DemiBold);
  label->setFont(font);
  if (layout_->rowCount() > 0) label->setContentsMargins(0, 12, 0, 0);
  layout_->addRow(label);
}

void SettingsPageOptions::addCheck(const taiga::opt::Bool& option, const QString& label) {
  auto* check = new QCheckBox(label, widget_);
  loaders_.push_back([check, option] { check->setChecked(option.get()); });
  appliers_.push_back([check, option] { option.set(check->isChecked()); });
  layout_->addRow(check);
}

void SettingsPageOptions::addChoice(const taiga::opt::Int& option, const QString& label,
                                    const QStringList& choices) {
  auto* combo = new QComboBox(widget_);
  combo->addItems(choices);
  loaders_.push_back([combo, option] { combo->setCurrentIndex(option.get()); });
  appliers_.push_back([combo, option] { option.set(combo->currentIndex()); });
  layout_->addRow(label, combo);
}

void SettingsPageOptions::addSpin(const taiga::opt::Int& option, const QString& label, int min,
                                  int max, const QString& suffix) {
  auto* spin = new QSpinBox(widget_);
  spin->setRange(min, max);
  spin->setSuffix(suffix);
  loaders_.push_back([spin, option] { spin->setValue(option.get()); });
  appliers_.push_back([spin, option] { option.set(spin->value()); });
  layout_->addRow(label, spin);
}

void SettingsPageOptions::addText(const taiga::opt::Str& option, const QString& label,
                                  const QString& hint) {
  auto* edit = new QLineEdit(widget_);
  edit->setPlaceholderText(hint);
  loaders_.push_back([edit, option] { edit->setText(option.get()); });
  appliers_.push_back([edit, option] { option.set(edit->text().trimmed()); });
  layout_->addRow(label, edit);
}

void SettingsPageOptions::addNote(const QString& text) {
  auto* label = new QLabel(text, widget_);
  label->setWordWrap(true);
  label->setEnabled(false);
  layout_->addRow(label);
}

void SettingsPageOptions::addRow(const QString& label, QWidget* field, std::function<void()> load,
                                 std::function<void()> apply) {
  field->setParent(widget_);
  if (load) loaders_.push_back(std::move(load));
  if (apply) appliers_.push_back(std::move(apply));
  if (label.isEmpty()) {
    layout_->addRow(field);
  } else {
    layout_->addRow(label, field);
  }
}

void SettingsPageOptions::load() {
  for (const auto& load : loaders_) load();
}

void SettingsPageOptions::apply() const {
  for (const auto& apply : appliers_) apply();
}

}  // namespace gui
