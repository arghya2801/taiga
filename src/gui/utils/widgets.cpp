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

#include "widgets.hpp"

#include <QGuiApplication>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>

#include "taiga/session.hpp"

namespace gui {

void setupHeaderMenu(QHeaderView* header, const QString& sessionKey) {
  const auto defaults = header->saveState();
  if (const auto state = taiga::session.headerState(sessionKey); !state.isEmpty()) {
    header->restoreState(state);
  }

  // Resizing emits a signal per mouse move, so wait for it to settle before writing.
  auto* saveTimer = new QTimer(header);
  saveTimer->setSingleShot(true);
  saveTimer->setInterval(500);
  QObject::connect(saveTimer, &QTimer::timeout, header, [header, sessionKey] {
    taiga::session.setHeaderState(sessionKey, header->saveState());
  });
  const auto scheduleSave = [saveTimer] { saveTimer->start(); };
  QObject::connect(header, &QHeaderView::sectionResized, saveTimer, scheduleSave);
  QObject::connect(header, &QHeaderView::sectionMoved, saveTimer, scheduleSave);

  header->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(
      header, &QWidget::customContextMenuRequested, header, [header, defaults](const QPoint& pos) {
        QMenu menu(header);
        const auto* model = header->model();
        for (int i = 0; i < header->count(); ++i) {
          auto text = model->headerData(i, Qt::Horizontal).toString();
          if (text.isEmpty()) text = model->headerData(i, Qt::Horizontal, Qt::ToolTipRole).toString();
          auto* action = menu.addAction(text);
          action->setCheckable(true);
          action->setChecked(!header->isSectionHidden(i));
          action->setEnabled(i != 0);  // the first column (the title) always stays
          QObject::connect(action, &QAction::toggled, header, [header, i](bool checked) {
            header->setSectionHidden(i, !checked);
            if (checked && header->sectionSize(i) < header->minimumSectionSize()) {
              header->resizeSection(i, header->defaultSectionSize());
            }
          });
        }
        menu.addSeparator();
        menu.addAction(QObject::tr("Reset headers"), header,
                       [header, defaults] { header->restoreState(defaults); });
        menu.exec(header->mapToGlobal(pos));
      });
}

void centerWidgetToScreen(QWidget* widget) {
  if (!widget) return;
  const auto screen = QGuiApplication::screenAt(widget->frameGeometry().topLeft());
  if (!screen) return;
  widget->move(screen->availableGeometry().center() - QRect{{}, widget->frameSize()}.center());
};

bool confirm(QWidget* parent, const QString& text, const QString& informativeText,
             const QString& confirmButtonText) {
  QMessageBox msgBox(parent);
  msgBox.setIcon(QMessageBox::Icon::Question);
  msgBox.setText(text);
  msgBox.setInformativeText(informativeText);

  const auto confirmButton =
      msgBox.addButton(confirmButtonText, QMessageBox::ButtonRole::DestructiveRole);
  msgBox.addButton(QMessageBox::Cancel);
  msgBox.setDefaultButton(QMessageBox::Cancel);

  msgBox.exec();

  return msgBox.clickedButton() == reinterpret_cast<QAbstractButton*>(confirmButton);
}

}  // namespace gui
