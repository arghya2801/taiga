#pragma once

#include <QDialog>

#include "media/manga.hpp"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTextBrowser;

namespace gui {

class MangaDialog final : public QDialog {
public:
  explicit MangaDialog(QWidget* parent, manga::Entry entry);

  manga::Entry editedEntry() const;
  bool removeRequested() const;

private:
  void showDetails(const manga::Entry& details);
  void loadCover(const QString& url);

  manga::Entry entry_;
  bool removeRequested_ = false;
  QString loadedCoverUrl_;
  QLabel* coverLabel_ = nullptr;
  QLabel* titleLabel_ = nullptr;
  QLabel* alternativeTitlesLabel_ = nullptr;
  QLabel* detailsLoadLabel_ = nullptr;
  QFormLayout* infoLayout_ = nullptr;
  QTextBrowser* synopsis_ = nullptr;
  QComboBox* statusBox_ = nullptr;
  QSpinBox* chaptersSpin_ = nullptr;
  QSpinBox* volumesSpin_ = nullptr;
  QSpinBox* scoreSpin_ = nullptr;
  QCheckBox* rereadingCheck_ = nullptr;
  QSpinBox* timesRereadSpin_ = nullptr;
  QSpinBox* rereadValueSpin_ = nullptr;
  QComboBox* priorityBox_ = nullptr;
  QLineEdit* tagsEdit_ = nullptr;
  QPlainTextEdit* commentsEdit_ = nullptr;
};

}  // namespace gui
