"""Audit actual PE resource bytes using in-memory fixtures; no native APIs."""
from pathlib import Path
import runpy
import struct
import unittest


ROOT = Path(__file__).resolve().parents[2]
AUDIT = runpy.run_path(str(ROOT / 'scripts/check_windows_icon.py'))
ICON = (ROOT / 'docs/design/assets/app/xdstarter.ico').read_bytes()


def pe_fixture(group='IDI_XDSTARTER', languages=(1033,)):
    frames = AUDIT['read_ico'](ICON)
    group_data = struct.pack('<HHH', 0, 1, len(frames)) + b''.join(
        struct.pack('<BBBBHHIH', *metadata, index + 1)
        for index, (metadata, _) in enumerate(frames))
    tree = {
        3: {index + 1: {lang: payload for lang in languages}
            for index, (_, payload) in enumerate(frames)},
        14: {group: {lang: group_data for lang in languages}},
    }
    resource, leaves, directories, names = bytearray(), {}, {}, {}

    def allocate(length, alignment=4):
        resource.extend(b'\0' * (-len(resource) % alignment))
        offset = len(resource)
        resource.extend(b'\0' * length)
        return offset

    def directory(node, path=()):
        keys = sorted(node, key=lambda key: (0, key) if isinstance(key, str) else (1, key))
        offset = allocate(16 + len(keys) * 8)
        directories[path] = offset
        named = sum(isinstance(key, str) for key in keys)
        struct.pack_into('<HH', resource, offset + 12, named, len(keys) - named)
        for index, key in enumerate(keys):
            identifier = key
            if isinstance(key, str):
                text = key.encode('utf-16-le')
                name_offset = allocate(2 + len(text), 2)
                struct.pack_into('<H', resource, name_offset, len(text) // 2)
                resource[name_offset + 2:name_offset + 2 + len(text)] = text
                names[path + (key,)] = name_offset
                identifier = name_offset | 0x80000000
            value = node[key]
            if isinstance(value, dict):
                target = directory(value, path + (key,)) | 0x80000000
            else:
                target = allocate(16)
                payload_offset = allocate(len(value))
                resource[payload_offset:payload_offset + len(value)] = value
                struct.pack_into('<IIII', resource, target, 0x1000 + payload_offset, len(value), 0, 0)
                leaves[path + (key,)] = (target, payload_offset)
            struct.pack_into('<II', resource, offset + 16 + index * 8, identifier, target)
        return offset

    directory(tree)
    headers = bytearray(0x200)
    headers[:2] = b'MZ'
    struct.pack_into('<I', headers, 0x3c, 0x80)
    headers[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', headers, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x22)
    optional = 0x98
    struct.pack_into('<H', headers, optional, 0x20b)
    struct.pack_into('<I', headers, optional + 60, 0x200)
    struct.pack_into('<I', headers, optional + 108, 16)
    struct.pack_into('<II', headers, optional + 112 + 16, 0x1000, len(resource))
    raw_size = (len(resource) + 511) // 512 * 512
    section = optional + 240
    struct.pack_into('<8sIIIIIIHHI', headers, section, b'.rsrc\0\0\0', len(resource),
                     0x1000, raw_size, 0x200, 0, 0, 0, 0, 0x40000040)
    executable = headers + resource + bytes(raw_size - len(resource))
    return executable, leaves, directories, names, section


class WindowsIconTests(unittest.TestCase):
    def test_named_group_and_each_language_are_verified(self):
        executable, *_ = pe_fixture(languages=(1033, 2052))
        self.assertEqual(AUDIT['check_icon'](executable, ICON),
                         [('IDI_XDSTARTER', 1033), ('IDI_XDSTARTER', 2052)])
        self.assertEqual(AUDIT['check_icon'](executable, ICON, 'IDI_XDSTARTER', 2052),
                         [('IDI_XDSTARTER', 2052)])

    def test_integer_group_and_explicit_name_selection(self):
        executable, *_ = pe_fixture(group=7)
        self.assertEqual(AUDIT['check_icon'](executable, ICON, 7, 1033), [(7, 1033)])
        self.assertEqual(AUDIT['resource_identifier']('name:7'), '7')
        self.assertEqual(AUDIT['resource_identifier']('0x409'), 1033)
        with self.assertRaisesRegex(AUDIT['IconCheckError'], 'No icon group'):
            AUDIT['check_icon'](executable, ICON, 7, 2052)

    def test_pe32_optional_header_uses_its_own_directory_offsets(self):
        executable, *_ = pe_fixture()
        resource = struct.unpack_from('<II', executable, 0x98 + 112 + 16)
        struct.pack_into('<H', executable, 0x98, 0x10b)
        struct.pack_into('<I', executable, 0x98 + 92, 16)
        struct.pack_into('<II', executable, 0x98 + 96 + 16, *resource)
        self.assertEqual(AUDIT['check_icon'](executable, ICON), [('IDI_XDSTARTER', 1033)])

    def test_frame_count_metadata_payload_and_icon_reference_must_match(self):
        original, leaves, *_ = pe_fixture()
        group = 0x200 + leaves[(14, 'IDI_XDSTARTER', 1033)][1]
        icon = 0x200 + leaves[(3, 1, 1033)][1]
        for offset, pattern, value, message in (
            (group + 4, '<H', 9, 'exactly ten frames'),
            (group + 12, '<H', 24, 'metadata differs'),
            (group + 18, '<H', 0xffff, 'no same-language RT_ICON'),
            (icon + 24, '<B', original[icon + 24] ^ 1, 'PNG payload differs'),
        ):
            with self.subTest(message=message):
                executable = bytearray(original)
                struct.pack_into(pattern, executable, offset, value)
                with self.assertRaisesRegex(AUDIT['IconCheckError'], message):
                    AUDIT['check_icon'](executable, ICON)

    def test_resource_relative_offset_cannot_escape_directory(self):
        executable, *_ = pe_fixture()
        struct.pack_into('<I', executable, 0x200 + 20, 0xffffffff)
        with self.assertRaisesRegex(AUDIT['IconCheckError'], 'relative offset'):
            AUDIT['check_icon'](executable, ICON)

    def test_rva_must_have_file_backed_raw_bytes(self):
        executable, leaves, _, _, section = pe_fixture()
        raw_size, = struct.unpack_from('<I', executable, section + 16)
        entry = 0x200 + leaves[(3, 1, 1033)][0]
        struct.pack_into('<I', executable, entry, 0x1000 + raw_size - 1)
        with self.assertRaisesRegex(AUDIT['IconCheckError'], 'outside section raw data'):
            AUDIT['check_icon'](executable, ICON)

    def test_overlapping_section_rvas_cannot_choose_an_arbitrary_mapping(self):
        executable, _, _, _, section = pe_fixture()
        struct.pack_into('<H', executable, 0x84 + 2, 2)
        executable[section + 40:section + 80] = executable[section:section + 40]
        with self.assertRaisesRegex(AUDIT['IconCheckError'], 'no unique'):
            AUDIT['check_icon'](executable, ICON)

    def test_truncated_sections_and_names_fail_with_clear_errors(self):
        original, _, _, names, section = pe_fixture()
        for executable, message in ((original[:100], 'PE signature'),
                                    (original[:-10], 'raw span')):
            with self.subTest(message=message):
                with self.assertRaisesRegex(AUDIT['IconCheckError'], message):
                    AUDIT['check_icon'](executable, ICON)
        executable = bytearray(original)
        struct.pack_into('<H', executable, 0x200 + names[(14, 'IDI_XDSTARTER')], 0xffff)
        with self.assertRaisesRegex(AUDIT['IconCheckError'], 'name length'):
            AUDIT['check_icon'](executable, ICON)

    def test_reference_ico_crc_is_checked_before_pe_comparison(self):
        executable, *_ = pe_fixture()
        icon = bytearray(ICON)
        payload_offset, = struct.unpack_from('<I', icon, 6 + 12)
        icon[payload_offset + 24] ^= 1
        with self.assertRaisesRegex(AUDIT['IconCheckError'], 'PNG CRC'):
            AUDIT['check_icon'](executable, icon)


if __name__ == '__main__':
    unittest.main()
