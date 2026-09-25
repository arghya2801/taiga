#pragma once

#include <QHash>

#include "gui/common/page_widget.hpp"
#include "media/manga.hpp"

class QAction;
class QSortFilterProxyModel;
class QStandardItemModel;

namespace gui {

class MangaListView;

class MangaWidget final : public PageWidget {
public:
  explicit MangaWidget(QWidget* parent = nullptr);

private:
  void refresh();
  void loadCache();
  void saveCache() const;
  void search();
  void populate();
  void updateCounts();
  void addChapter();
  void editSelected();
  void editEntry(manga::Entry entry);
  void showContextMenu(const QPoint& pos);
  void setBusy(bool busy, const QString& message = {});
  void selectId(int id);
  int selectedId() const;
  const manga::Entry* selectedEntry() const;

  QHash<int, manga::Entry> library_;
  QHash<int, manga::Entry> results_;
  bool showingResults_ = false;
  QString currentStatus_;
  bool busy_ = false;

  QStandardItemModel* model_ = nullptr;
  QSortFilterProxyModel* proxyModel_ = nullptr;
  MangaListView* view_ = nullptr;
  QAction* refreshAction_ = nullptr;
  QAction* chapterAction_ = nullptr;
};

}  // namespace gui
