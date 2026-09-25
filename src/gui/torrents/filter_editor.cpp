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

#include "filter_editor.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "base/string.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"

namespace gui {

using namespace track::feed;

namespace {

constexpr int kElementCount = static_cast<int>(FilterElement::FileSize) + 1;
constexpr int kOperatorCount = static_cast<int>(FilterOperator::NotContains) + 1;

QString describe(const Filter& filter) {
  const QStringList actions{FilterListEditor::tr("Discard"), FilterListEditor::tr("Select"),
                            FilterListEditor::tr("Prefer")};
  auto text = u"%1: %2"_s.arg(actions.value(static_cast<int>(filter.action)), filter.name);
  if (!filter.animeIds.isEmpty()) {
    text += FilterListEditor::tr(" (%n anime)", nullptr, filter.animeIds.size());
  }
  return text;
}

// Edits one filter. Conditions are rows of element, operator and value.
bool editFilter(QWidget* parent, Filter& filter) {
  QDialog dialog{parent};
  dialog.setWindowTitle(FilterListEditor::tr("Torrent filter"));
  dialog.resize(640, 420);
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout;
  layout->addLayout(form);

  auto* name = new QLineEdit(filter.name);
  form->addRow(FilterListEditor::tr("Name:"), name);

  auto* action = new QComboBox;
  action->addItems({FilterListEditor::tr("Discard matching torrents"),
                    FilterListEditor::tr("Select matching torrents"),
                    FilterListEditor::tr("Prefer matching torrents over other releases")});
  action->setCurrentIndex(static_cast<int>(filter.action));
  form->addRow(FilterListEditor::tr("Action:"), action);

  auto* match = new QComboBox;
  match->addItems({FilterListEditor::tr("All conditions"), FilterListEditor::tr("Any condition")});
  match->setCurrentIndex(static_cast<int>(filter.match));
  form->addRow(FilterListEditor::tr("Match:"), match);

  auto* option = new QComboBox;
  option->addItems({FilterListEditor::tr("Show discarded torrents greyed out"),
                    FilterListEditor::tr("Show discarded torrents greyed out, unchecked"),
                    FilterListEditor::tr("Hide discarded torrents")});
  option->setCurrentIndex(static_cast<int>(filter.option));
  form->addRow(FilterListEditor::tr("Discarded:"), option);

  QStringList animeTitles;
  for (const int id : filter.animeIds) {
    const auto item = anime::db.item(id);
    animeTitles.append(item ? QString::fromStdString(anime::preferredTitle(*item))
                            : QString::number(id));
  }
  auto* limit = new QLabel(animeTitles.isEmpty() ? FilterListEditor::tr("All anime")
                                                 : animeTitles.join(u", "_s));
  limit->setWordWrap(true);
  auto* clearLimit = new QPushButton(FilterListEditor::tr("Apply to all anime"));
  clearLimit->setEnabled(!filter.animeIds.isEmpty());
  QList<int> animeIds = filter.animeIds;
  QObject::connect(clearLimit, &QPushButton::clicked, &dialog, [&animeIds, limit, clearLimit] {
    animeIds.clear();
    limit->setText(FilterListEditor::tr("All anime"));
    clearLimit->setEnabled(false);
  });
  auto* limitRow = new QHBoxLayout;
  limitRow->addWidget(limit, 1);
  limitRow->addWidget(clearLimit);
  form->addRow(FilterListEditor::tr("Applies to:"), limitRow);

  auto* table = new QTableWidget(0, 3);
  table->setHorizontalHeaderLabels({FilterListEditor::tr("Element"),
                                    FilterListEditor::tr("Operator"),
                                    FilterListEditor::tr("Value")});
  table->horizontalHeader()->setStretchLastSection(true);
  table->verticalHeader()->hide();
  const auto addCondition = [table](const Condition& condition) {
    const int row = table->rowCount();
    table->insertRow(row);
    auto* element = new QComboBox;
    for (int i = 0; i < kElementCount; ++i) {
      element->addItem(elementName(static_cast<FilterElement>(i)));
    }
    element->setCurrentIndex(static_cast<int>(condition.element));
    auto* op = new QComboBox;
    for (int i = 0; i < kOperatorCount; ++i) {
      op->addItem(operatorName(static_cast<FilterOperator>(i)));
    }
    op->setCurrentIndex(static_cast<int>(condition.op));
    table->setCellWidget(row, 0, element);
    table->setCellWidget(row, 1, op);
    table->setItem(row, 2, new QTableWidgetItem(condition.value));
  };
  for (const auto& condition : filter.conditions) addCondition(condition);
  table->resizeColumnsToContents();
  layout->addWidget(new QLabel(FilterListEditor::tr(
      "Conditions. Values can use %watched% and %total%. Statuses use numbers: list status 1 "
      "is watching, 2 completed, 3 on hold, 4 dropped, 5 plan to watch.")));
  layout->addWidget(table, 1);

  auto* conditionButtons = new QHBoxLayout;
  auto* add = new QPushButton(FilterListEditor::tr("Add condition"));
  auto* remove = new QPushButton(FilterListEditor::tr("Remove condition"));
  QObject::connect(add, &QPushButton::clicked, &dialog, [addCondition] { addCondition({}); });
  QObject::connect(remove, &QPushButton::clicked, &dialog, [table] {
    if (table->currentRow() >= 0) table->removeRow(table->currentRow());
  });
  conditionButtons->addWidget(add);
  conditionButtons->addWidget(remove);
  conditionButtons->addStretch();
  layout->addLayout(conditionButtons);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) return false;

  filter.name = name->text().trimmed().isEmpty() ? FilterListEditor::tr("Untitled filter")
                                                 : name->text().trimmed();
  filter.action = static_cast<FilterAction>(action->currentIndex());
  filter.match = static_cast<FilterMatch>(match->currentIndex());
  filter.option = static_cast<FilterOption>(option->currentIndex());
  filter.animeIds = animeIds;
  filter.conditions.clear();
  for (int row = 0; row < table->rowCount(); ++row) {
    filter.conditions.append({
        static_cast<FilterElement>(
            static_cast<QComboBox*>(table->cellWidget(row, 0))->currentIndex()),
        static_cast<FilterOperator>(
            static_cast<QComboBox*>(table->cellWidget(row, 1))->currentIndex()),
        table->item(row, 2) ? table->item(row, 2)->text() : QString{},
    });
  }
  return true;
}

}  // namespace

FilterListEditor::FilterListEditor(QWidget* parent) : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_list = new QListWidget(this);
  m_list->setMinimumHeight(200);
  layout->addWidget(m_list, 1);
  connect(m_list, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem* item) { edit(m_list->row(item)); });
  connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    const int row = m_list->row(item);
    if (row >= 0 && row < m_filters.size()) {
      m_filters[row].enabled = item->checkState() == Qt::Checked;
    }
  });

  auto* buttons = new QVBoxLayout;
  layout->addLayout(buttons);
  const auto addButton = [&](const QString& text, auto&& slot) {
    auto* button = new QPushButton(text, this);
    connect(button, &QPushButton::clicked, this, slot);
    buttons->addWidget(button);
  };
  addButton(tr("Add..."), [this] {
    Filter filter{.name = tr("New filter"),
                  .conditions = {{FilterElement::FileTitle, FilterOperator::Contains, {}}}};
    if (editFilter(this, filter)) {
      m_filters.append(filter);
      refresh();
    }
  });
  addButton(tr("Edit..."), [this] { edit(m_list->currentRow()); });
  addButton(tr("Remove"), [this] {
    if (const int row = m_list->currentRow(); row >= 0) {
      m_filters.removeAt(row);
      refresh();
    }
  });
  addButton(tr("Move up"), [this] {
    if (const int row = m_list->currentRow(); row > 0) {
      m_filters.swapItemsAt(row, row - 1);
      refresh();
      m_list->setCurrentRow(row - 1);
    }
  });
  addButton(tr("Move down"), [this] {
    if (const int row = m_list->currentRow(); row >= 0 && row < m_filters.size() - 1) {
      m_filters.swapItemsAt(row, row + 1);
      refresh();
      m_list->setCurrentRow(row + 1);
    }
  });
  addButton(tr("Reset to defaults"), [this] {
    m_filters = defaultFilters();
    refresh();
  });
  buttons->addStretch();
}

void FilterListEditor::setFilters(const QList<Filter>& filters) {
  m_filters = filters;
  refresh();
}

QList<Filter> FilterListEditor::filters() const {
  return m_filters;
}

void FilterListEditor::refresh() {
  const QSignalBlocker blocker{m_list};
  m_list->clear();
  for (const auto& filter : m_filters) {
    auto* item = new QListWidgetItem(describe(filter), m_list);
    item->setCheckState(filter.enabled ? Qt::Checked : Qt::Unchecked);
  }
}

void FilterListEditor::edit(int row) {
  if (row < 0 || row >= m_filters.size()) return;
  if (editFilter(this, m_filters[row])) {
    refresh();
    m_list->setCurrentRow(row);
  }
}

}  // namespace gui
