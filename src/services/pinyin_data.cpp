// src/services/pinyin_data.cpp

#include "services/pinyin_data.h"

#include <algorithm>
#include <iterator>

namespace xdstarter {
namespace {

struct PinyinRecord {
    char32_t codepoint;
    const char* readings;
};

constexpr PinyinRecord kPinyinRecords[] = {
#include "../../third_party/unicode/unihan_pinyin.inc"
};

} // namespace

QStringList pinyinReadings(char32_t codepoint) {
    const auto end = std::end(kPinyinRecords);
    const auto found = std::lower_bound(
        std::begin(kPinyinRecords), end, codepoint,
        [](const PinyinRecord& record, char32_t value) { return record.codepoint < value; });
    if (found == end || found->codepoint != codepoint) {
        return {};
    }
    return QString::fromLatin1(found->readings).split(u',');
}

} // namespace xdstarter
