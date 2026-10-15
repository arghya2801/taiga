#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QThreadPool>
#include <QTimer>

#include "media/manga.hpp"

namespace manga {

QJsonObject toJson(const Entry& entry);
Entry fromJson(const QJsonObject& object);
bool exportList(const QList<Entry>& entries, const QString& path, bool xml);

class Library final : public QObject {
  Q_OBJECT
public:
  Library();
  ~Library();
  void init();
  void flush();
  void refresh();
  const QHash<int, Entry>& entries() const {
    return entries_;
  }
  bool ready() const {
    return ready_;
  }
  int pendingCount() const {
    return pending_.size();
  }
  bool save(const Entry& entry);
  bool remove(int id);

signals:
  void reset();
  void changed(int id);
  void pendingChanged();
  void errorOccurred(const QString& error);

private:
  struct Pending {
    Entry desired, previous;
    bool deleting = false;
    quint64 version = 0;
  };
  void loadAccount();
  void saveCache();
  bool persistPending();
  void process();
  void complete(int id, const Entry* entry);
  QString path(const QString& suffix) const;
  QString account_;
  QHash<int, Entry> entries_;
  QHash<int, Pending> pending_;
  QThreadPool writer_;
  QTimer retry_;
  bool initialized_ = false, ready_ = false;
  bool refreshing_ = false;
  quint64 epoch_ = 0, version_ = 0, processingVersion_ = 0;
  quint64 refreshVersion_ = 0;
  int processing_ = 0;
};

extern Library library;

}  // namespace manga
