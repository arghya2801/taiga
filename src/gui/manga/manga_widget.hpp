#pragma once

#include <QHash>
#include <QWidget>

#include "media/manga.hpp"

class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace gui {

class MangaWidget final : public QWidget {
public:
  explicit MangaWidget(QWidget* parent = nullptr);

private:
  void refresh();
  void loadCache();
  void saveCache() const;
  void search();
  void populate(QTableWidget* table, const QList<manga::Entry>& entries);
  void filterList();
  void editSelected(QTableWidget* table);
  void editEntry(manga::Entry entry);
  void setBusy(bool busy, const QString& message = {});
  int selectedId(QTableWidget* table) const;

  QHash<int, manga::Entry> library_;
  QHash<int, manga::Entry> results_;
  QTabWidget* tabs_ = nullptr;
  QTableWidget* listTable_ = nullptr;
  QTableWidget* searchTable_ = nullptr;
  QLineEdit* filterBox_ = nullptr;
  QLineEdit* searchBox_ = nullptr;
  QComboBox* statusFilter_ = nullptr;
  QPushButton* refreshButton_ = nullptr;
  QPushButton* searchButton_ = nullptr;
  QPushButton* editButton_ = nullptr;
  QPushButton* addButton_ = nullptr;
  QPushButton* chapterButton_ = nullptr;
  QLabel* messageLabel_ = nullptr;
  bool busy_ = false;
};

}  // namespace gui
