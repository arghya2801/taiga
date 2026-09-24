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
#include "settings_page_accounts.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <algorithm>
#include <string>

#include "sync/myanimelist/myanimelist.hpp"
#include "sync/myanimelist/myanimelist_utils.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/settings.hpp"
#include "ui_settings_dialog.h"

namespace gui {

namespace {

QString toQString(const std::string& s) {
  return QString::fromStdString(s);
}

std::string toStdString(const QLineEdit* lineEdit) {
  return lineEdit->text().trimmed().toStdString();
}

}  // namespace

SettingsPageAccounts::SettingsPageAccounts(Ui::SettingsDialog* ui, QDialog* dialog)
    : SettingsPage(ui, dialog) {
  using sync::ServiceId;

  for (const auto id : {ServiceId::AniList, ServiceId::Kitsu, ServiceId::MyAnimeList}) {
    ui_->serviceComboBox->addItem(sync::serviceName(id), sync::serviceSlug(id));
  }

  connect(ui_->serviceComboBox, &QComboBox::currentIndexChanged, this,
          [this]() { updateVisibleGroup(); });

  ui_->myanimelistUsernameLineEdit->setReadOnly(true);
  ui_->myanimelistAccessTokenLabel->hide();
  ui_->myanimelistAccessTokenLineEdit->hide();
  ui_->myanimelistRefreshTokenLabel->hide();
  ui_->myanimelistRefreshTokenLineEdit->hide();
  connect(ui_->myanimelistAuthorizeButton, &QPushButton::clicked, this,
          &SettingsPageAccounts::authorizeMyAnimeList);

  auto* service = sync::myanimelist::Service::instance();
  connect(service, &sync::Service::errorOccurred, this, [this](const QString& message) {
    if (authorizingMyAnimeList_) myanimelistAuthError_ = message;
  });
  connect(service, &sync::Service::authenticationCompleted, this, [this](bool authenticated) {
    if (!authorizingMyAnimeList_) return;
    authorizingMyAnimeList_ = false;
    ui_->myanimelistAuthorizeButton->setEnabled(true);
    if (authenticated) {
      ui_->myanimelistUsernameLineEdit->setText(
          toQString(taiga::accounts.myanimelistUsername()));
      ui_->myanimelistAuthorizeButton->setText(tr("Re-authorize..."));
      QMessageBox::information(dialog_, tr("MyAnimeList"), tr("Authorization succeeded."));
    } else {
      QMessageBox::warning(dialog_, tr("MyAnimeList"),
                           myanimelistAuthError_.isEmpty() ? tr("Authorization failed.")
                                                           : myanimelistAuthError_);
    }
  });
}

void SettingsPageAccounts::load() {
  const auto slug = QString::fromStdString(taiga::settings.service());
  ui_->serviceComboBox->setCurrentIndex(std::max(0, ui_->serviceComboBox->findData(slug)));
  updateVisibleGroup();

  ui_->syncEnabledCheckBox->setChecked(taiga::settings.syncEnabled());

  const auto& accounts = taiga::accounts;

  ui_->anilistUsernameLineEdit->setText(toQString(accounts.anilistUsername()));
  ui_->anilistTokenLineEdit->setText(toQString(accounts.anilistToken()));

  ui_->kitsuEmailLineEdit->setText(toQString(accounts.kitsuEmail()));
  ui_->kitsuUsernameLineEdit->setText(toQString(accounts.kitsuUsername()));
  ui_->kitsuPasswordLineEdit->setText(toQString(accounts.kitsuPassword()));
  ui_->kitsuAccessTokenLineEdit->setText(toQString(accounts.kitsuAccessToken()));
  ui_->kitsuRefreshTokenLineEdit->setText(toQString(accounts.kitsuRefreshToken()));

  ui_->myanimelistUsernameLineEdit->setText(toQString(accounts.myanimelistUsername()));
  ui_->myanimelistAuthorizeButton->setText(accounts.myanimelistUsername().empty()
                                               ? tr("Authorize...")
                                               : tr("Re-authorize..."));
}

void SettingsPageAccounts::apply() const {
  taiga::settings.setService(ui_->serviceComboBox->currentData().toString().toStdString());
  taiga::settings.setSyncEnabled(ui_->syncEnabledCheckBox->isChecked());

  auto& accounts = taiga::accounts;

  accounts.setAnilistUsername(toStdString(ui_->anilistUsernameLineEdit));
  accounts.setAnilistToken(toStdString(ui_->anilistTokenLineEdit));

  accounts.setKitsuEmail(toStdString(ui_->kitsuEmailLineEdit));
  accounts.setKitsuUsername(toStdString(ui_->kitsuUsernameLineEdit));
  accounts.setKitsuPassword(ui_->kitsuPasswordLineEdit->text().toStdString());
  accounts.setKitsuAccessToken(toStdString(ui_->kitsuAccessTokenLineEdit));
  accounts.setKitsuRefreshToken(toStdString(ui_->kitsuRefreshTokenLineEdit));
}

void SettingsPageAccounts::authorizeMyAnimeList() {
  std::string codeVerifier;
  const auto url = sync::myanimelist::authorizationCodeUrl(codeVerifier);
  if (!QDesktopServices::openUrl(QUrl{QString::fromStdString(url)})) {
    QMessageBox::warning(dialog_, tr("MyAnimeList"),
                         tr("Could not open the authorization page in your browser."));
    return;
  }

  bool accepted = false;
  const auto code = QInputDialog::getText(
      dialog_, tr("MyAnimeList authorization"),
      tr("After approving Taiga in your browser, paste the code shown on the page:"),
      QLineEdit::Normal, {}, &accepted).trimmed();
  if (!accepted || code.isEmpty()) return;

  authorizingMyAnimeList_ = true;
  myanimelistAuthError_.clear();
  ui_->myanimelistAuthorizeButton->setEnabled(false);
  sync::myanimelist::Service::instance()->requestAccessToken(
      code, QString::fromStdString(codeVerifier));
}

void SettingsPageAccounts::updateVisibleGroup() {
  using sync::ServiceId;

  const auto slug = ui_->serviceComboBox->currentData().toString();
  ui_->anilistGroupBox->setVisible(slug == sync::serviceSlug(ServiceId::AniList));
  ui_->kitsuGroupBox->setVisible(slug == sync::serviceSlug(ServiceId::Kitsu));
  ui_->myanimelistGroupBox->setVisible(slug == sync::serviceSlug(ServiceId::MyAnimeList));
}

}  // namespace gui
