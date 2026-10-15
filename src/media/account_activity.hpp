#pragma once

#include <QObject>

#include "media/account_stats.hpp"

namespace stats {
enum class MediaKind { Anime, Manga };

class ActivityLog final : public QObject {
  Q_OBJECT
public:
  bool init();
  bool record(MediaKind kind, int id, qint64 previousProgress, qint64 progress,
              double previousScore, double score);
  const QList<ActivityChange>& records(MediaKind kind);
signals:
  void changed();

private:
  QString account(MediaKind kind) const;
  std::array<QString, 2> accounts_;
  std::array<QList<ActivityChange>, 2> records_;
};
inline ActivityLog activity;
}  // namespace stats
