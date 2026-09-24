#pragma once

#include <QString>

namespace manga {

struct Entry {
  int id = 0;
  QString title;
  QString type;
  QString status;
  int chapters = 0;
  int volumes = 0;
  int chaptersRead = 0;
  int volumesRead = 0;
  int score = 0;
  double mean = 0.0;
  bool onList = false;
};

}  // namespace manga
