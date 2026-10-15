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

#include "atf.hpp"

#include <QStringList>

#include "base/string.hpp"
#include <optional>

namespace atf {

namespace {

const QString kTrue = QStringLiteral("true");

// Splits by commas that aren't escaped. Escapes are kept for the final unescape.
QStringList splitArgs(const QString& body) {
  QStringList args{QString{}};
  for (qsizetype i = 0; i < body.size(); ++i) {
    if (body[i] == u'\\' && i + 1 < body.size()) {
      args.last() += body[i];
      args.last() += body[++i];
    } else if (body[i] == u',') {
      args.append(QString{});
    } else {
      args.last() += body[i];
    }
  }
  return args;
}

std::optional<int> toNumber(const QString& str) {
  bool ok = false;
  const int value = str.toInt(&ok);
  return ok ? std::optional{value} : std::nullopt;
}

// Numbers compare as numbers, anything else as case-insensitive text.
int compare(const QString& a, const QString& b) {
  const auto x = toNumber(a);
  const auto y = toNumber(b);
  if (x && y) return *x < *y ? -1 : (*x > *y ? 1 : 0);
  return QString::compare(a, b, Qt::CaseInsensitive);
}

QString evaluate(const QString& name, QStringList args) {
  const auto arg = [&](qsizetype i) { return i < args.size() ? args[i] : QString{}; };
  const auto truth = [](bool value) { return value ? kTrue : QString{}; };

  if (name == u"and") {
    for (const auto& a : args) {
      if (a.isEmpty()) return {};
    }
    return kTrue;
  }
  if (name == u"or") {
    for (const auto& a : args) {
      if (!a.isEmpty()) return kTrue;
    }
    return {};
  }
  if (name == u"not") return truth(arg(0).isEmpty());
  if (args.size() > 1) {
    if (name == u"equal") return truth(compare(args[0], args[1]) == 0);
    if (name == u"gequal") return truth(compare(args[0], args[1]) >= 0);
    if (name == u"greater") return truth(compare(args[0], args[1]) > 0);
    if (name == u"lequal") return truth(compare(args[0], args[1]) <= 0);
    if (name == u"less") return truth(compare(args[0], args[1]) < 0);
  }

  if (name == u"if") {
    switch (args.size()) {
      case 1:
        return args[0];
      case 2:
        return !args[0].isEmpty() ? args[1] : QString{};
      default:
        return !args[0].isEmpty() ? args[1] : args[2];
    }
  }
  if (name == u"if2") return !arg(0).isEmpty() ? arg(0) : arg(1);
  if (name == u"ifequal") return arg(0) == arg(1) ? arg(2) : arg(3);

  if (name == u"cut") {
    const int length = arg(1).toInt();
    return length >= 0 ? arg(0).left(length) : arg(0);
  }
  if (name == u"len") return QString::number(arg(0).size());
  if (name == u"lower") return arg(0).toLower();
  if (name == u"upper") return arg(0).toUpper();
  if (name == u"num") return arg(0).rightJustified(arg(1).toInt(), u'0');
  if (name == u"pad") {
    const auto fill = arg(2).isEmpty() ? QStringLiteral(" ") : arg(2);
    QString padding;
    for (qsizetype i = 0; i < arg(1).toInt() - arg(0).size(); ++i) {
      padding += fill[i % fill.size()];
    }
    return padding + arg(0);
  }
  if (name == u"replace") return arg(1).isEmpty() ? arg(0) : QString{arg(0)}.replace(arg(1), arg(2));
  if (name == u"substr") return arg(0).mid(arg(1).toInt(), arg(2).toInt());
  if (name == u"triml") {
    const auto chars = args.size() > 1 ? arg(1) : QStringLiteral(" ");
    auto s = arg(0);
    while (!s.isEmpty() && chars.contains(s.front())) s.removeFirst();
    return s;
  }
  if (name == u"trimr") {
    const auto chars = args.size() > 1 ? arg(1) : QStringLiteral(" ");
    auto s = arg(0);
    while (!s.isEmpty() && chars.contains(s.back())) s.removeLast();
    return s;
  }

  return {};
}

bool isNameChar(QChar c) {
  return c.isLetterOrNumber();
}

// Evaluates `$name(...)` calls, innermost first, leaving escapes in place.
QString replaceFunctions(const QString& str) {
  QString result;
  for (qsizetype i = 0; i < str.size(); ++i) {
    if (str[i] == u'\\' && i + 1 < str.size()) {
      result += str[i];
      result += str[++i];
      continue;
    }
    if (str[i] == u'$') {
      qsizetype open = i + 1;
      while (open < str.size() && isNameChar(str[open])) ++open;
      if (open < str.size() && str[open] == u'(' && open > i + 1) {
        int depth = 0;
        qsizetype close = open;
        for (; close < str.size(); ++close) {
          if (str[close] == u'\\') {
            ++close;
          } else if (str[close] == u'(') {
            ++depth;
          } else if (str[close] == u')' && --depth == 0) {
            break;
          }
        }
        if (close < str.size()) {
          const auto name = str.mid(i + 1, open - i - 1);
          const auto body = replaceFunctions(str.mid(open + 1, close - open - 1));
          result += evaluate(name, splitArgs(body));
          i = close;
          continue;
        }
      }
    }
    result += str[i];
  }
  return result;
}

QString escape(const QString& str) {
  QString escaped;
  for (const auto c : str) {
    if (QStringView{u"$,()%\\"}.contains(c)) escaped += u'\\';
    escaped += c;
  }
  return escaped;
}

QString unescape(const QString& str) {
  QString result;
  for (qsizetype i = 0; i < str.size(); ++i) {
    if (str[i] == u'\\' && i + 1 < str.size()) ++i;
    result += str[i];
  }
  return result;
}

QString replaceVariables(const QString& str, const Fields& fields) {
  QString result;
  qsizetype pos = 0;
  while (pos < str.size()) {
    const auto start = str.indexOf(u'%', pos);
    const auto end = start < 0 ? -1 : str.indexOf(u'%', start + 1);
    if (end < 0) break;
    const auto name = str.mid(start + 1, end - start - 1);
    if (const auto it = fields.constFind(name); it != fields.cend()) {
      result += str.mid(pos, start - pos) + escape(*it);
      pos = end + 1;
    } else {
      result += str.mid(pos, end - pos);  // keep the first %, retry from the second
      pos = end;
    }
  }
  return result + str.mid(pos);
}

}  // namespace

QString replace(QString str, const Fields& fields) {
  str = replaceVariables(str, fields);
  str.replace(u"\\n"_s, u"\n"_s).replace(u"\\t"_s, u"\t"_s);
  str = unescape(replaceFunctions(str));
  while (str.contains(u"\n\n"_s)) str.replace(u"\n\n"_s, u"\n"_s);
  while (str.contains(u"  "_s)) str.replace(u"  "_s, u" "_s);
  return str.trimmed();
}

}  // namespace atf
