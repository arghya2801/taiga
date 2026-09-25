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

#include "painters.hpp"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>

#include "base/string.hpp"
#include "gui/models/anime_list_model.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime.hpp"
#include "media/anime_list.hpp"
#include "media/anime_list_utils.hpp"
#include "media/anime_utils.hpp"
#include "track/scanner.hpp"

namespace gui {

void paintEmptyListText(QAbstractScrollArea* area, const QString& text) {
  QPainter painter(area->viewport());

  painter.setFont([&painter]() {
    auto font = painter.font();
    font.setItalic(true);
    return font;
  }());

  painter.drawText(area->viewport()->rect(), Qt::AlignCenter, text);
}

void paintProgressBar(QPainter* painter, const QStyleOption& option, const Anime* anime,
                      const ListEntry* entry) {
  if (!anime || !entry) return;
  const int total = anime->episode_count;
  // Like v1: aired but unwatched episodes are a faint extension of the bar, and episodes in the
  // library folders are ticks along the bottom. Both need a known total to place them.
  const int aired = total <= 0 ? 0
                    : anime::airingStatus(*anime) == anime::Status::FinishedAiring
                        ? total
                        : std::min(anime::estimateLastAiredEpisodeNumber(*anime), total);
  paintProgressBar(painter, option, entry->watched_episodes, total, aired, anime->id);
}

void paintProgressBar(QPainter* painter, const QStyleOption& option, int done, int total,
                      int aired, int animeId) {
  done = std::clamp(done, 0, total > 0 ? total : std::numeric_limits<int>::max());
  const auto text = u"%1/%2"_s.arg(done).arg(formatNumber(total, "?"));
  // Same ratio as `anime::list::getProgressRatio`: unknown totals show a mostly-full bar.
  const auto ratio = total > 0 ? std::min(done / static_cast<double>(total), 1.0) : 0.8;

  // Flat: a quiet track, one solid fill, no gradients or bevels.
  const QRectF bar = QRectF(option.rect).adjusted(0, 1, 0, -1);
  const QRectF fill{bar.left(), bar.top(), bar.width() * ratio, bar.height()};
  constexpr qreal radius = 3;

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setPen(Qt::NoPen);

  QPainterPath track;
  track.addRoundedRect(bar, radius, radius);
  painter->setClipPath(track);
  painter->fillRect(bar, theme.color(Theme::Color::Sunken));

  const auto progress = theme.color(Theme::Color::Progress);
  if (total > 0) {
    const qreal step = bar.width() / total;
    if (aired > done) {
      auto color = progress;
      color.setAlpha(theme.isDark() ? 26 : 34);
      painter->fillRect(QRectF{fill.right(), bar.top(), (aired - done) * step, bar.height()},
                        color);
    }
    painter->fillRect(fill, progress);
    if (animeId && track::availableEpisodes.last(animeId) > done) {
      const auto color = theme.color(Theme::Color::Available);
      for (int number = done + 1; number <= total; ++number) {
        if (!track::availableEpisodes.contains(animeId, number)) continue;
        painter->fillRect(QRectF{bar.left() + (number - 1) * step, bar.bottom() - 3,
                                 std::max(step - 1, 1.5), 3},
                          color);
      }
    }
  } else {
    painter->fillRect(fill, progress);
  }

  // The text changes color where the fill passes under it, so it reads on both.
  painter->setClipRect(QRectF{fill.right(), bar.top(), bar.right() - fill.right(), bar.height()});
  painter->setPen(theme.color(Theme::Color::Text));
  painter->drawText(bar, Qt::AlignCenter, text);
  painter->setClipRect(fill);
  painter->setPen(Qt::white);
  painter->drawText(bar, Qt::AlignCenter, text);

  painter->restore();
}

void paintSpinner(QPainter* painter, const QPixmap& pixmap, const QPointF& center, qreal angle) {
  const qreal dpr = pixmap.devicePixelRatio();
  const QPointF halfSize(pixmap.width() / dpr / 2.0, pixmap.height() / dpr / 2.0);

  painter->save();
  painter->setRenderHint(QPainter::SmoothPixmapTransform);
  painter->translate(center);
  painter->rotate(angle);
  painter->drawPixmap(-halfSize, pixmap);
  painter->restore();
}

}  // namespace gui
