#include "anime_query.hpp"

#include <QRegularExpression>
#include <algorithm>

namespace anime {
namespace {
bool contains(const std::string& a, const QString& b) {
  return QString::fromStdString(a).contains(b, Qt::CaseInsensitive);
}
bool contains(const std::vector<std::string>& a, const QString& b) {
  return std::any_of(a.begin(), a.end(), [&](const auto& v) { return contains(v, b); });
}
bool compare(double a, double b, const QString& op) {
  if (op == ">=") return a >= b;
  if (op == "<=") return a <= b;
  if (op == ">") return a > b;
  if (op == "<") return a < b;
  if (op == "!=" || op == "!") return a != b;
  return a == b;
}
QString typeName(Type type) {
  switch (type) {
    case Type::Tv:
      return "TV";
    case Type::Ova:
      return "OVA";
    case Type::Movie:
      return "Movie";
    case Type::Special:
      return "Special";
    case Type::Ona:
      return "ONA";
    case Type::Music:
      return "Music";
    default:
      return "Unknown";
  }
}
}  // namespace

Query::Query(const QString& text) {
  QStringList tokens;
  QString token;
  bool quoted = false;
  for (const auto c : text) {
    if (c == '"') {
      quoted = !quoted;
      continue;
    }
    if (c.isSpace() && !quoted) {
      if (!token.isEmpty()) tokens.append(token);
      token.clear();
    } else
      token += c;
  }
  if (!token.isEmpty()) tokens.append(token);
  static const QRegularExpression pattern(QStringLiteral("^([a-z]+):([!<=>]*)(.+)$"),
                                          QRegularExpression::CaseInsensitiveOption);
  const QStringList fields{"id",       "eps",   "title",   "genre",  "producer", "studio",
                           "tag",      "note",  "type",    "season", "year",     "rewatch",
                           "duration", "score", "average", "status"};
  for (const auto& word : tokens) {
    const auto m = pattern.match(word);
    if (m.hasMatch() && fields.contains(m.captured(1).toLower()))
      terms_.append({m.captured(1).toLower(), m.captured(2), m.captured(3)});
    else
      terms_.append({{}, {}, word});
  }
}

bool Query::matches(const Details& a, const list::Entry* e) const {
  const auto titles = [&](const QString& v) {
    return contains(a.titles.romaji, v) || contains(a.titles.english, v) ||
           contains(a.titles.japanese, v) || contains(a.titles.synonyms, v);
  };
  for (const auto& t : terms_) {
    bool found = false;
    if (t.field.isEmpty())
      found = titles(t.value) || contains(a.genres, t.value) || contains(a.tags, t.value) ||
              (e && contains(e->notes, t.value));
    else if (t.field == "title")
      found = titles(t.value);
    else if (t.field == "genre")
      found = contains(a.genres, t.value);
    else if (t.field == "studio")
      found = contains(a.studios, t.value);
    else if (t.field == "producer")
      found = contains(a.studios, t.value) || contains(a.producers, t.value);
    else if (t.field == "tag")
      found = contains(a.tags, t.value);
    else if (t.field == "note")
      found = e && contains(e->notes, t.value);
    else if (t.field == "type")
      found = typeName(a.type).compare(t.value, Qt::CaseInsensitive) == 0;
    else if (t.field == "season") {
      const QStringList names{"Winter", "Spring", "Summer", "Fall"};
      const auto month = a.date_started.month();
      found = month >= 1 && month <= 12 &&
              (QString::number(a.date_started.year()) + ' ' + names[(month - 1) / 3])
                  .contains(t.value, Qt::CaseInsensitive);
    } else if (t.field == "status") {
      const QStringList names{"unknown", "watching", "completed",
                              "on_hold", "dropped",  "plan_to_watch"};
      auto value = t.value.toLower();
      value.replace(' ', '_');
      value.replace('-', '_');
      found = e && names.value(int(e->status)) == value;
    } else {
      bool ok = false;
      const auto value = t.value.toDouble(&ok);
      if (!ok) return false;
      double actual = 0;
      if (t.field == "id")
        actual = a.id;
      else if (t.field == "eps")
        actual = a.episode_count;
      else if (t.field == "year")
        actual = a.date_started.year();
      else if (t.field == "duration")
        actual = a.episode_length;
      else if (t.field == "rewatch")
        actual = e ? e->rewatched_times : 0;
      else if (t.field == "score")
        actual = e ? e->score / 10.0 : 0;
      else if (t.field == "average")
        actual = a.score;
      if (!compare(actual, value, t.op)) return false;
      continue;
    }
    if (t.op == "!" || t.op == "!=") found = !found;
    if (!found) return false;
  }
  return true;
}
}  // namespace anime
