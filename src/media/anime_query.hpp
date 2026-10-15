#pragma once
#include <QList>
#include <QString>

#include "media/anime.hpp"
#include "media/anime_list.hpp"

namespace anime {
// Parsed once when the search text changes, rather than once per filtered row.
class Query {
public:
  explicit Query(const QString& text = {});
  bool matches(const Details& anime, const list::Entry* entry) const;

private:
  struct Term {
    QString field, op, value;
  };
  QList<Term> terms_;
};
}  // namespace anime
