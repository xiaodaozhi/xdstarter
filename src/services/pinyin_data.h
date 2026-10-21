// src/services/pinyin_data.h

#pragma once

#include <QStringList>

namespace xdstarter {

// Offline, tone-free character readings; an unknown scalar has no readings.
QStringList pinyinReadings(char32_t codepoint);

} // namespace xdstarter
