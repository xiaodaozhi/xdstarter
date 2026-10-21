# scripts/check_pinyin_data.py

"""Check the offline Unihan subset, generated C++ table and required polyphones."""

import sys

from generate_pinyin_data import DATA_DIR, parse_table, run


def main() -> int:
    try:
        stats = run(check=True)
        records = dict(parse_table((DATA_DIR / "unihan_pinyin.tsv").read_bytes()))
        required = {
            "乐": {"le", "yue"},
            "长": {"chang", "zhang"},
            "重": {"chong", "zhong"},
            "行": {"hang", "xing"},
            "厦": {"sha", "xia"},
            "绿": {"lv", "lu"},
        }
        for character, readings in required.items():
            if not readings.issubset(records.get(ord(character), [])):
                raise ValueError(f"Missing expected readings at U+{ord(character):04X}")
        for scalar in (0, 0x41, 0xD800, 0x10FFFF):
            if scalar in records:
                raise ValueError(f"Unexpected Mandarin mapping at U+{scalar:04X}")
    except (ValueError, OSError, KeyError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(f"PASS: Unicode license, pinned hashes, {stats['codepoints']} offline records and common polyphonic readings.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
