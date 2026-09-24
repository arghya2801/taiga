#include "manga_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <utility>

#include "base/string.hpp"
#include "sync/myanimelist/myanimelist.hpp"

namespace gui {

namespace {

constexpr std::pair<const char*, const char*> kStatuses[] = {
    {"reading", "Reading"},       {"completed", "Completed"},
    {"on_hold", "On hold"},        {"dropped", "Dropped"},
    {"plan_to_read", "Wishlist"},
};

QString displayValue(int value) {
  return value > 0 ? QString::number(value) : u"?"_s;
}

}  // namespace

MangaDialog::MangaDialog(QWidget* parent, manga::Entry entry)
    : QDialog(parent), entry_(std::move(entry)) {
  setWindowTitle(entry_.title);
  resize(800, 610);

  auto* root = new QVBoxLayout(this);
  auto* body = new QHBoxLayout();
  root->addLayout(body, 1);

  coverLabel_ = new QLabel(tr("Loading cover..."), this);
  coverLabel_->setFixedSize(190, 285);
  coverLabel_->setAlignment(Qt::AlignCenter);
  coverLabel_->setWordWrap(true);
  body->addWidget(coverLabel_, 0, Qt::AlignTop);

  auto* right = new QVBoxLayout();
  body->addLayout(right, 1);
  titleLabel_ = new QLabel(entry_.title, this);
  auto titleFont = titleLabel_->font();
  titleFont.setPointSize(14);
  titleFont.setWeight(QFont::DemiBold);
  titleLabel_->setFont(titleFont);
  titleLabel_->setWordWrap(true);
  titleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  right->addWidget(titleLabel_);
  alternativeTitlesLabel_ = new QLabel(this);
  alternativeTitlesLabel_->setWordWrap(true);
  alternativeTitlesLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  alternativeTitlesLabel_->hide();
  right->addWidget(alternativeTitlesLabel_);

  auto* tabs = new QTabWidget(this);
  right->addWidget(tabs, 1);

  auto* detailsPage = new QWidget(tabs);
  auto* detailsLayout = new QVBoxLayout(detailsPage);
  detailsLoadLabel_ = new QLabel(tr("Loading details from MyAnimeList..."), detailsPage);
  detailsLayout->addWidget(detailsLoadLabel_);
  auto* detailsScroll = new QScrollArea(detailsPage);
  detailsScroll->setWidgetResizable(true);
  detailsScroll->setFrameShape(QFrame::NoFrame);
  auto* detailsContent = new QWidget(detailsScroll);
  auto* detailsContentLayout = new QVBoxLayout(detailsContent);
  infoLayout_ = new QFormLayout();
  detailsContentLayout->addLayout(infoLayout_);
  auto* synopsisHeader = new QLabel(tr("Synopsis"), detailsContent);
  auto synopsisFont = synopsisHeader->font();
  synopsisFont.setWeight(QFont::DemiBold);
  synopsisHeader->setFont(synopsisFont);
  detailsContentLayout->addWidget(synopsisHeader);
  synopsis_ = new QTextBrowser(detailsContent);
  synopsis_->setFrameShape(QFrame::NoFrame);
  synopsis_->setMinimumHeight(170);
  detailsContentLayout->addWidget(synopsis_, 1);
  detailsScroll->setWidget(detailsContent);
  detailsLayout->addWidget(detailsScroll, 1);
  tabs->addTab(detailsPage, tr("Details"));

  auto* listPage = new QWidget(tabs);
  auto* listLayout = new QVBoxLayout(listPage);
  auto* listScroll = new QScrollArea(listPage);
  listScroll->setWidgetResizable(true);
  listScroll->setFrameShape(QFrame::NoFrame);
  auto* listContent = new QWidget(listScroll);
  auto* form = new QFormLayout(listContent);
  form->setVerticalSpacing(10);

  statusBox_ = new QComboBox(listContent);
  for (const auto& [value, label] : kStatuses)
    statusBox_->addItem(QLatin1String(label), QLatin1String(value));
  const auto status = entry_.status.isEmpty() ? u"plan_to_read"_s : entry_.status;
  statusBox_->setCurrentIndex(statusBox_->findData(status));
  form->addRow(tr("Status"), statusBox_);

  chaptersSpin_ = new QSpinBox(listContent);
  chaptersSpin_->setRange(0, 1000000);
  chaptersSpin_->setValue(entry_.chaptersRead);
  form->addRow(tr("Chapters read"), chaptersSpin_);
  volumesSpin_ = new QSpinBox(listContent);
  volumesSpin_->setRange(0, 1000000);
  volumesSpin_->setValue(entry_.volumesRead);
  form->addRow(tr("Volumes read"), volumesSpin_);
  scoreSpin_ = new QSpinBox(listContent);
  scoreSpin_->setRange(0, 10);
  scoreSpin_->setValue(entry_.score);
  form->addRow(tr("Score"), scoreSpin_);

  rereadingCheck_ = new QCheckBox(tr("Currently rereading"), listContent);
  rereadingCheck_->setChecked(entry_.rereading);
  form->addRow({}, rereadingCheck_);
  timesRereadSpin_ = new QSpinBox(listContent);
  timesRereadSpin_->setRange(0, 1000000);
  timesRereadSpin_->setValue(entry_.timesReread);
  form->addRow(tr("Times reread"), timesRereadSpin_);
  rereadValueSpin_ = new QSpinBox(listContent);
  rereadValueSpin_->setRange(0, 5);
  rereadValueSpin_->setValue(entry_.rereadValue);
  form->addRow(tr("Reread value"), rereadValueSpin_);

  priorityBox_ = new QComboBox(listContent);
  priorityBox_->addItem(tr("Low"), 0);
  priorityBox_->addItem(tr("Medium"), 1);
  priorityBox_->addItem(tr("High"), 2);
  priorityBox_->setCurrentIndex(entry_.priority);
  form->addRow(tr("Priority"), priorityBox_);
  tagsEdit_ = new QLineEdit(entry_.tags.join(", "), listContent);
  tagsEdit_->setPlaceholderText(tr("Separate tags with commas"));
  form->addRow(tr("Tags"), tagsEdit_);
  commentsEdit_ = new QPlainTextEdit(entry_.comments, listContent);
  commentsEdit_->setMinimumHeight(110);
  form->addRow(tr("Comments"), commentsEdit_);

  listScroll->setWidget(listContent);
  listLayout->addWidget(listScroll);
  tabs->addTab(listPage, tr("My list"));

  connect(statusBox_, &QComboBox::currentIndexChanged, this, [this] {
    if (statusBox_->currentData().toString() != "completed") return;
    if (entry_.status == "completed") return;
    if (entry_.chapters > 0) chaptersSpin_->setValue(entry_.chapters);
    if (entry_.volumes > 0) volumesSpin_->setValue(entry_.volumes);
  });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)->setText(entry_.onList ? tr("Save") : tr("Add to list"));
  auto* openButton = buttons->addButton(tr("Open on MyAnimeList"), QDialogButtonBox::ActionRole);
  connect(openButton, &QPushButton::clicked, this, [id = entry_.id] {
    QDesktopServices::openUrl(QUrl{u"https://myanimelist.net/manga/%1"_s.arg(id)});
  });
  if (entry_.onList) {
    auto* removeButton = buttons->addButton(tr("Remove"), QDialogButtonBox::DestructiveRole);
    connect(removeButton, &QPushButton::clicked, this, [this] {
      if (QMessageBox::question(this, tr("Remove manga"),
                                tr("Remove %1 from your MyAnimeList manga list?")
                                    .arg(entry_.title)) != QMessageBox::Yes)
        return;
      removeRequested_ = true;
      accept();
    });
  }
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  showDetails(entry_);
  auto* service = sync::myanimelist::Service::instance();
  connect(service, &sync::myanimelist::Service::mangaDetailsFetched, this,
          [this](const manga::Entry& details) {
            if (details.id != entry_.id) return;
            detailsLoadLabel_->hide();
            showDetails(details);
          });
  connect(service, &sync::myanimelist::Service::mangaDetailsFailed, this, [this](int id) {
    if (id == entry_.id)
      detailsLoadLabel_->setText(tr("Could not load more details. List editing is still available."));
  });
  service->fetchMangaDetails(entry_.id);
}

manga::Entry MangaDialog::editedEntry() const {
  auto edited = entry_;
  edited.status = statusBox_->currentData().toString();
  edited.chaptersRead = chaptersSpin_->value();
  edited.volumesRead = volumesSpin_->value();
  edited.score = scoreSpin_->value();
  edited.rereading = rereadingCheck_->isChecked();
  edited.timesReread = timesRereadSpin_->value();
  edited.rereadValue = rereadValueSpin_->value();
  edited.priority = priorityBox_->currentData().toInt();
  edited.tags.clear();
  for (const auto& tag : tagsEdit_->text().split(',', Qt::SkipEmptyParts)) {
    const auto trimmed = tag.trimmed();
    if (!trimmed.isEmpty()) edited.tags.append(trimmed);
  }
  edited.comments = commentsEdit_->toPlainText();
  return edited;
}

bool MangaDialog::removeRequested() const {
  return removeRequested_;
}

void MangaDialog::showDetails(const manga::Entry& details) {
  titleLabel_->setText(details.title);
  alternativeTitlesLabel_->setText(details.alternativeTitles.join(", "));
  alternativeTitlesLabel_->setVisible(!details.alternativeTitles.isEmpty());
  while (infoLayout_->rowCount() > 0) infoLayout_->removeRow(0);
  const auto add = [this](const QString& title, const QString& value) {
    if (value.isEmpty()) return;
    auto* label = new QLabel(value, this);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout_->addRow(title, label);
  };
  add(tr("Type"), details.type);
  auto publicationStatus = details.publicationStatus;
  add(tr("Publishing"), publicationStatus.replace('_', ' '));
  add(tr("Started"), details.startDate);
  add(tr("Finished"), details.endDate);
  add(tr("Chapters"), displayValue(details.chapters));
  add(tr("Volumes"), displayValue(details.volumes));
  if (details.mean > 0) add(tr("Mean score"), QString::number(details.mean, 'f', 2));
  if (details.rank > 0) add(tr("Rank"), QString::number(details.rank));
  if (details.popularity > 0) add(tr("Popularity"), QString::number(details.popularity));
  add(tr("Genres"), details.genres.join(", "));
  add(tr("Authors"), details.authors.join(", "));
  synopsis_->setPlainText(details.synopsis.isEmpty() ? tr("No synopsis available.")
                                                      : details.synopsis);
  loadCover(details.coverUrl);
}

void MangaDialog::loadCover(const QString& url) {
  if (url.isEmpty() || url == loadedCoverUrl_) return;
  loadedCoverUrl_ = url;
  auto* manager = new QNetworkAccessManager(this);
  auto* reply = manager->get(QNetworkRequest(QUrl(url)));
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    const auto data = reply->readAll();
    reply->deleteLater();
    QPixmap cover;
    if (cover.loadFromData(data)) {
      coverLabel_->setPixmap(cover.scaled(coverLabel_->size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
      coverLabel_->setText({});
    } else {
      coverLabel_->setText(tr("Cover unavailable"));
    }
  });
}

}  // namespace gui
