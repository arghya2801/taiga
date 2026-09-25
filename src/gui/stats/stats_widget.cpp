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

#include "stats_widget.hpp"

#include <QDateTime>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

#include "base/string.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/rating.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "media/anime_stats.hpp"
#include "taiga/settings.hpp"
#include "track/scanner.hpp"

namespace gui {

namespace {

const QDateTime kStartTime = QDateTime::currentDateTime();

QString formatLongDuration(qint64 seconds) {
  if (seconds <= 0) return StatsWidget::tr("None");
  const auto days = seconds / 86400;
  const auto hours = (seconds % 86400) / 3600;
  const auto minutes = (seconds % 3600) / 60;
  QStringList parts;
  if (days) parts << StatsWidget::tr("%n day(s)", nullptr, days);
  if (hours) parts << StatsWidget::tr("%n hour(s)", nullptr, hours);
  if (!days && minutes) parts << StatsWidget::tr("%n minute(s)", nullptr, minutes);
  return parts.isEmpty() ? StatsWidget::tr("Less than a minute") : parts.join(", ");
}

QLabel* makeHeading(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  auto font = label->font();
  font.setPointSizeF(font.pointSizeF() * 1.2);
  font.setWeight(QFont::DemiBold);
  label->setFont(font);
  label->setContentsMargins(0, 16, 0, 4);
  return label;
}

}  // namespace

// Horizontal bars, one per score, highest score on top. This is the page's one chart, so it
// gets the progress bar's green rather than anything new.
class ScoreChart final : public QWidget {
public:
  using QWidget::QWidget;

  void setCounts(const std::array<int, 10>& counts) {
    m_counts = counts;
    setMinimumHeight(kRowHeight * static_cast<int>(counts.size()));
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);

    const int max = std::max(1, *std::ranges::max_element(m_counts));
    const QFontMetrics metrics{font()};
    // Rating labels vary by service, e.g. "(9) Great" on MyAnimeList.
    int labelWidth = 0;
    for (int bucket = 1; bucket <= static_cast<int>(m_counts.size()); ++bucket) {
      labelWidth = std::max(labelWidth, metrics.horizontalAdvance(formatRating(bucket * 10)));
    }
    labelWidth += 12;
    const int countWidth = metrics.horizontalAdvance(u"0000"_s) + 8;
    const int barSpace = std::max(0, width() - labelWidth - countWidth);

    const QColor barColor = theme.color(Theme::Color::Progress);
    const QColor emptyColor = theme.color(Theme::Color::Faint);

    for (int row = 0; row < static_cast<int>(m_counts.size()); ++row) {
      const int bucket = static_cast<int>(m_counts.size()) - row;  // 10 at the top
      const int count = m_counts[bucket - 1];
      const QRect rowRect{0, row * kRowHeight, width(), kRowHeight};

      painter.setPen(theme.color(Theme::Color::Muted));
      painter.drawText(rowRect.adjusted(0, 0, -(width() - labelWidth + 12), 0),
                       Qt::AlignRight | Qt::AlignVCenter, formatRating(bucket * 10));

      const int barWidth = count * barSpace / max;
      const QRectF bar{static_cast<qreal>(labelWidth), rowRect.top() + 4.0,
                       static_cast<qreal>(barWidth), kRowHeight - 8.0};
      if (barWidth > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(barColor);
        painter.drawRoundedRect(bar, 2, 2);
      }

      painter.setPen(count ? palette().color(QPalette::Text) : emptyColor);
      painter.drawText(QRect{labelWidth + barWidth + 6, rowRect.top(), countWidth, kRowHeight},
                       Qt::AlignLeft | Qt::AlignVCenter, QString::number(count));
    }
  }

private:
  static constexpr int kRowHeight = 22;
  std::array<int, 10> m_counts{};
};

StatsWidget::StatsWidget(QWidget* parent) : PageWidget(parent) {
  m_toolbar->addAction(theme.getIcon("sync"), tr("Refresh"), this, &StatsWidget::refresh);

  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  layout()->addWidget(scroll);

  auto* content = new QWidget(scroll);
  auto* column = new QVBoxLayout(content);
  column->setContentsMargins(24, 0, 24, 24);
  column->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  scroll->setWidget(content);

  const auto addSection = [&](const QString& title, QLabel*& label) {
    column->addWidget(makeHeading(title, content));
    label = new QLabel(content);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(label);
  };

  addSection(tr("Anime list"), m_listLabel);
  addSection(tr("Scores"), m_scoreLabel);

  m_chart = new ScoreChart(content);
  m_chart->setMaximumWidth(560);
  m_chart->setMinimumWidth(280);
  column->addWidget(m_chart);

  addSection(tr("Local data"), m_dataLabel);
}

void StatsWidget::showEvent(QShowEvent* event) {
  PageWidget::showEvent(event);
  refresh();
}

void StatsWidget::refresh() {
  const auto stats = anime::calculateListStats(
      anime::db.entries().values(), [](int id) { return anime::db.item(id); });

  const QLocale locale;

  m_listLabel->setText(
      tr("%1 anime, %2 episodes watched\n"
         "Time spent watching: %3\n"
         "Time left to watch: %4")
          .arg(locale.toString(stats.animeCount))
          .arg(locale.toString(stats.episodesWatched))
          .arg(formatLongDuration(stats.secondsWatched))
          .arg(formatLongDuration(stats.secondsPlanned)));

  const int scored = std::ranges::fold_left(stats.scoreCounts, 0, std::plus{});
  m_scoreLabel->setText(
      scored ? tr("%n scored anime. Mean score: %1, standard deviation: %2", nullptr, scored)
                   .arg(QString::number(stats.meanScore / 10.0, 'f', 2))
                   .arg(QString::number(stats.scoreDeviation / 10.0, 'f', 2))
             : tr("Scores you give anime will show up here."));
  m_chart->setCounts(stats.scoreCounts);
  m_chart->setVisible(scored > 0);

  int availableCount = 0;
  for (const auto& entry : anime::db.entries()) {
    availableCount += track::availableEpisodes.count(entry.anime_id);
  }

  m_dataLabel->setText(
      tr("Anime in database: %1\n"
         "Episodes in library folders: %2\n"
         "Cached images: %3\n"
         "Uptime: %4")
          .arg(locale.toString(anime::db.items().size()))
          .arg(taiga::settings.libraryFolders().empty() ? tr("no library folders")
                                                        : locale.toString(availableCount))
          .arg(locale.formattedDataSize(imageProvider.cacheSize()))
          .arg(formatLongDuration(kStartTime.secsTo(QDateTime::currentDateTime()))));
}

}  // namespace gui
