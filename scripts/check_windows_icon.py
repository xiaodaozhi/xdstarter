"""Read-only audit of the Windows icon embedded in a built XDStarter PE file.

Uses only the Python standard library. It never loads the executable, invokes a
Windows API or starts another process. Resource identifiers may be integers or
UTF-16 names; every selected group/language must match the reference ICO.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
import zlib


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'


class IconCheckError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise IconCheckError(message)


def unpack(data, offset, pattern, description):
    size = struct.calcsize(pattern)
    require(0 <= offset <= len(data) and size <= len(data) - offset,
            f'Truncated {description}')
    return struct.unpack_from(pattern, data, offset)


def png_size(payload):
    require(payload.startswith(PNG_SIGNATURE), 'Icon frame is not a PNG payload')
    position, dimensions, ended, image_data = 8, None, False, False
    while position < len(payload):
        length, = unpack(payload, position, '>I', 'PNG chunk length')
        require(length <= len(payload) - position - 12, 'Truncated PNG chunk')
        tag = payload[position + 4:position + 8]
        data = payload[position + 8:position + 8 + length]
        checksum, = unpack(payload, position + 8 + length, '>I', 'PNG CRC')
        require(zlib.crc32(tag + data) & 0xffffffff == checksum, 'PNG CRC mismatch')
        if dimensions is None:
            require(tag == b'IHDR' and length == 13, 'PNG must begin with a 13-byte IHDR')
            width, height, depth, kind, compression, filtering, interlace = struct.unpack('>IIBBBBB', data)
            require(depth == 8 and kind == 6 and compression == filtering == interlace == 0,
                    'Expected a non-interlaced 32-bit RGBA PNG frame')
            dimensions = (width, height)
        else:
            require(tag != b'IHDR', 'Duplicate PNG IHDR')
        if tag == b'IDAT' and length:
            image_data = True
        position += 12 + length
        if tag == b'IEND':
            require(length == 0 and position == len(payload), 'Invalid PNG IEND or trailing bytes')
            ended = True
            break
    require(dimensions is not None and ended and image_data, 'Incomplete PNG frame')
    return dimensions


def read_ico(data):
    reserved, kind, count = unpack(data, 0, '<HHH', 'ICO header')
    require(reserved == 0 and kind == 1 and count == len(EXPECTED_SIZES),
            'Reference ICO must contain exactly ten icon frames')
    frames, ranges = [], []
    table_end = 6 + count * 16
    for index, expected_size in enumerate(EXPECTED_SIZES):
        entry = unpack(data, 6 + index * 16, '<BBBBHHII', 'ICO directory entry')
        width, height, colors, unused, planes, bits, length, offset = entry
        require((width or 256, height or 256) == (expected_size, expected_size),
                f'ICO frame {index} dimensions do not match the ten-size baseline')
        require(colors == unused == 0 and planes == 1 and bits == 32,
                f'ICO frame {index} is not declared as 32-bit RGBA')
        require(length > 0 and offset >= table_end and offset <= len(data) and
                length <= len(data) - offset, f'ICO frame {index} payload is out of bounds')
        require(all(offset + length <= start or offset >= end for start, end in ranges),
                f'ICO frame {index} overlaps another payload')
        ranges.append((offset, offset + length))
        payload = data[offset:offset + length]
        require(png_size(payload) == (expected_size, expected_size),
                f'ICO frame {index} PNG dimensions disagree with its directory')
        frames.append((entry[:7], payload))
    return frames


class PeResources:
    def __init__(self, data):
        self.data = data
        require(data[:2] == b'MZ', 'Executable is missing the DOS signature')
        pe_offset, = unpack(data, 0x3c, '<I', 'DOS PE offset')
        require(pe_offset >= 0x40 and data[pe_offset:pe_offset + 4] == b'PE\0\0',
                'Executable is missing a valid PE signature')
        _, count, _, _, _, optional_size, _ = unpack(data, pe_offset + 4, '<HHIIIHH', 'COFF header')
        require(1 <= count <= 96, 'Invalid PE section count')
        optional = pe_offset + 24
        require(optional_size <= len(data) - optional, 'Truncated PE optional header')
        magic, = unpack(data, optional, '<H', 'PE optional magic')
        require(magic in (0x10b, 0x20b), 'Unsupported PE optional-header format')
        directory_offset = 112 if magic == 0x20b else 96
        require(optional_size >= directory_offset + 24, 'Optional header has no resource directory')
        directories, = unpack(data, optional + directory_offset - 4, '<I', 'data-directory count')
        require(directories >= 3, 'PE has no resource data-directory entry')
        self.header_size, = unpack(data, optional + 60, '<I', 'PE header size')
        require(optional + optional_size + count * 40 <= self.header_size <= len(data),
                'PE header span does not cover its section table or exceeds file length')
        self.resource_rva, self.resource_size = unpack(data, optional + directory_offset + 16,
                                                      '<II', 'resource data directory')
        require(self.resource_rva != 0 and self.resource_size >= 16,
                'PE does not contain a resource directory')
        self.sections = []
        section_table = optional + optional_size
        for index in range(count):
            _, virtual_size, rva, raw_size, raw_offset, _, _, _, _, _ = unpack(
                data, section_table + index * 40, '<8sIIIIIIHHI', 'PE section header')
            require(raw_offset <= len(data) and raw_size <= len(data) - raw_offset,
                    f'Section {index} raw span exceeds file length')
            require(rva + max(virtual_size, raw_size) <= 0x100000000,
                    f'Section {index} RVA span overflows 32 bits')
            self.sections.append((rva, max(virtual_size, raw_size), raw_offset, raw_size))
        require(self.resource_rva + self.resource_size <= 0x100000000,
                'Resource RVA span overflows 32 bits')

    def rva_bytes(self, rva, size):
        require(0 <= rva <= 0xffffffff and size > 0 and rva + size <= 0x100000000,
                'Invalid resource RVA/size')
        candidates = []
        if rva < self.header_size and size <= self.header_size - rva:
            candidates.append(rva)
        for start, span, raw_offset, raw_size in self.sections:
            if start <= rva < start + span:
                relative = rva - start
                require(relative <= raw_size and size <= raw_size - relative,
                        'Resource RVA maps outside section raw data')
                candidates.append(raw_offset + relative)
        require(len(candidates) == 1, 'Resource RVA has no unique file-backed section mapping')
        offset = candidates[0]
        require(offset <= len(self.data) and size <= len(self.data) - offset,
                'Resource payload exceeds file length')
        return self.data[offset:offset + size]

    def relative_bytes(self, offset, size):
        require(0 <= offset <= self.resource_size and 0 < size <= self.resource_size - offset,
                'Resource-directory relative offset is out of bounds')
        return self.rva_bytes(self.resource_rva + offset, size)

    def identifier(self, value):
        if not value & 0x80000000:
            require(value <= 0xffff, 'Integer resource identifier exceeds 16 bits')
            return value
        offset = value & 0x7fffffff
        length, = struct.unpack('<H', self.relative_bytes(offset, 2))
        require(0 < length <= 1024, 'Invalid resource name length')
        try:
            return self.relative_bytes(offset + 2, length * 2).decode('utf-16-le')
        except UnicodeError as error:
            raise IconCheckError('Invalid UTF-16 resource name') from error

    def directory(self, offset):
        header = self.relative_bytes(offset, 16)
        named, numbered = struct.unpack_from('<HH', header, 12)
        count = named + numbered
        require(count <= 4096, 'Resource directory has too many entries')
        entries = self.relative_bytes(offset + 16, count * 8) if count else b''
        result = {}
        for index in range(count):
            key, target = struct.unpack_from('<II', entries, index * 8)
            identifier = self.identifier(key)
            require(identifier not in result, 'Duplicate resource identifier')
            require(bool(key & 0x80000000) == (index < named), 'Invalid named-resource ordering')
            result[identifier] = target
        return result

    def icons(self):
        result = {}
        root = self.directory(0)
        for kind in (3, 14):
            require(kind in root and root[kind] & 0x80000000,
                    f'Missing RT_{"ICON" if kind == 3 else "GROUP_ICON"} directory')
            for name, directory in self.directory(root[kind] & 0x7fffffff).items():
                require(directory & 0x80000000, 'Resource name must point to a language directory')
                for language, leaf in self.directory(directory & 0x7fffffff).items():
                    require(not leaf & 0x80000000, 'Resource language must point to a data entry')
                    rva, size, _, reserved = struct.unpack('<IIII', self.relative_bytes(leaf, 16))
                    require(reserved == 0, 'Resource data entry has a nonzero reserved field')
                    result[kind, name, language] = self.rva_bytes(rva, size)
        return result


def check_icon(executable, ico, group=None, language=None):
    frames = read_ico(ico)
    resources = PeResources(executable).icons()
    groups = [(name, lang, payload) for (kind, name, lang), payload in resources.items()
              if kind == 14 and (group is None or name == group) and
              (language is None or lang == language)]
    require(groups, 'No icon group matches the requested group/language')
    for name, lang, payload in groups:
        reserved, kind, count = unpack(payload, 0, '<HHH', 'RT_GROUP_ICON header')
        require(reserved == 0 and kind == 1 and count == len(frames) and
                len(payload) == 6 + count * 14,
                f'Icon group {name!r}/{lang!r} must contain exactly ten frames')
        seen = set()
        for index, (metadata, reference) in enumerate(frames):
            entry = unpack(payload, 6 + index * 14, '<BBBBHHIH', 'RT_GROUP_ICON entry')
            icon_id = entry[-1]
            require(icon_id not in seen, f'Icon group {name!r}/{lang!r} repeats an RT_ICON ID')
            seen.add(icon_id)
            require(entry[:-1] == metadata,
                    f'Icon group {name!r}/{lang!r} frame {index} metadata differs from ICO')
            actual = resources.get((3, icon_id, lang))
            require(actual is not None,
                    f'Icon group {name!r}/{lang!r} frame {index} has no same-language RT_ICON')
            require(actual == reference,
                    f'Icon group {name!r}/{lang!r} frame {index} PNG payload differs from ICO')
    return [(name, lang) for name, lang, _ in groups]


def resource_identifier(value):
    if value.startswith('name:'):
        return value[5:]
    raw = value[3:] if value.startswith('id:') else value
    try:
        result = int(raw, 16 if raw.lower().startswith('0x') else 10)
    except ValueError:
        if value.startswith('id:'):
            raise argparse.ArgumentTypeError('Resource id must be an integer') from None
        return value
    if not 0 <= result <= 0xffff:
        raise argparse.ArgumentTypeError('Resource id must fit 16 bits')
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--icon', type=Path, default=ROOT / 'docs/design/assets/app/xdstarter.ico')
    parser.add_argument('--group', type=resource_identifier, help='integer id or name (name:1 forces a numeric name)')
    parser.add_argument('--language', type=resource_identifier, help='language id, e.g. 1033 or 0x409')
    arguments = parser.parse_args(argv)
    try:
        require(0 < arguments.executable.stat().st_size <= 512 * 1024 * 1024,
                'Executable size must be between 1 byte and 512 MiB')
        require(0 < arguments.icon.stat().st_size <= 8 * 1024 * 1024,
                'ICO size must be between 1 byte and 8 MiB')
        executable, icon = arguments.executable.read_bytes(), arguments.icon.read_bytes()
        groups = check_icon(executable, icon, arguments.group, arguments.language)
        print(f'PASS: {len(groups)} embedded icon group/language variant(s); ten 32-bit PNG frames match ICO exactly.')
        for name, language in groups:
            print(f'  group={name!r}, language={language!r}, sizes={EXPECTED_SIZES}')
        print(f'Executable SHA-256: {hashlib.sha256(executable).hexdigest()}')
        print(f'ICO SHA-256: {hashlib.sha256(icon).hexdigest()}')
        return 0
    except (OSError, IconCheckError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
