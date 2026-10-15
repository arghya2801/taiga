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

#pragma once

#include <QHash>
#include <QString>

// v1's format strings: `%variable%` and `$function(args)`, e.g.
// `$if(%episode%,Episode %episode%$if(%total%,/%total%))`. Ported from `src/v1/base/atf.cpp`.
namespace atf {

// Variables with an empty value are removed; unknown `%names%` are left as they are.
using Fields = QHash<QString, QString>;

QString replace(QString str, const Fields& fields);

}  // namespace atf
