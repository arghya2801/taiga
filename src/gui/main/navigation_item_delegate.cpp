/**
 * Taiga
 * Copyright (C) 2010-2024, Eren Okka
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

#include "navigation_item_delegate.hpp"

#include <QLocale>
#include <QPainter>
#include <QPainterPath>

#include "gui/utils/painter_state_saver.hpp"
#include "gui/utils/theme.hpp"

namespace gui {

NavigationItemDelegate::NavigationItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void NavigationItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
  // Separator
  if (index.data(static_cast<int>(NavigationItemDataRole::IsSeparator)).toBool()) {
    paintSeparator(painter, option.rect);
    return;
  }

  QStyledItemDelegate::paint(painter, option, index);

  // Branch
  if (index.data(static_cast<int>(NavigationItemDataRole::IsChild)).toBool()) {
    const bool isLastChild =
        index.data(static_cast<int>(NavigationItemDataRole::IsLastChild)).toBool();
    paintBranch(painter, option.rect, isLastChild);
  }

  // Counter
  if (const int count = index.data(static_cast<int>(NavigationItemDataRole::Counter)).toInt()) {
    paintCounter(painter, option.rect, count);
  }
}

void NavigationItemDelegate::paintBranch(QPainter* painter, QRect rect, bool isLastChild) const {
  const PainterStateSaver painterStateSaver(painter);

  rect.setLeft(8);
  rect.setWidth(16);

  const QPoint center = rect.center();

  painter->setPen([painter]() {
    auto pen = painter->pen();
    pen.setColor(theme.color(Theme::Color::Line));
    return pen;
  }());

  if (isLastChild) {
    painter->drawLine({center.x(), rect.top()}, center);
  } else {
    painter->drawLine(center.x(), rect.top(), center.x(), rect.bottom());
  }

  painter->drawLine(center.x() + painter->pen().width(), center.y(), rect.right(), center.y());
}

void NavigationItemDelegate::paintCounter(QPainter* painter, QRect rect, const int count) const {
  const PainterStateSaver painterStateSaver(painter);

  // Flat: a quiet number at the right edge, no badge.
  painter->setFont([painter]() {
    auto font = painter->font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    return font;
  }());
  painter->setPen(theme.color(Theme::Color::Faint));
  painter->drawText(rect.adjusted(0, 0, -10, 0), Qt::AlignRight | Qt::AlignVCenter,
                    QLocale().toString(count));
}

void NavigationItemDelegate::paintSeparator(QPainter* painter, const QRect& rect) const {
  const PainterStateSaver painterStateSaver(painter);

  const int y = rect.center().y();

  painter->setPen(theme.color(Theme::Color::Line));
  painter->drawLine(rect.left() + 8, y, rect.right() - 8, y);
};

}  // namespace gui
