"""The installer text view must retain agreement wording and list references."""
import io
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from runtime_license import agreement_text


class RuntimeLicenseTests(unittest.TestCase):
    def fixture(self, *, drawing=False, styled=False):
        document = '''<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>
        <w:p><w:r><w:t>Original title</w:t></w:r></w:p>
        <w:p><w:pPr><w:numPr><w:numId w:val="1"/><w:ilvl w:val="0"/></w:numPr></w:pPr><w:r><w:t>First </w:t></w:r><w:r><w:t>condition</w:t></w:r></w:p>
        <w:p><w:pPr><w:numPr><w:numId w:val="1"/><w:ilvl w:val="1"/></w:numPr></w:pPr><w:r><w:t>Subcondition</w:t></w:r></w:p>
        <w:p><w:pPr><w:numPr><w:numId w:val="1"/><w:ilvl w:val="0"/></w:numPr></w:pPr><w:r><w:t>Second condition</w:t></w:r></w:p>
        <w:p><w:pPr><w:numPr><w:numId w:val="2"/><w:ilvl w:val="0"/></w:numPr></w:pPr><w:r><w:t>Bullet condition</w:t></w:r></w:p>
        </w:body></w:document>'''
        if drawing:
            document = document.replace('</w:body>', '<w:p><w:r><w:drawing/></w:r></w:p></w:body>')
        if styled:
            document = document.replace('<w:numPr><w:numId w:val="1"/><w:ilvl w:val="0"/></w:numPr>',
                                        '<w:pStyle w:val="TermsList"/>', 1)
        numbering = '''<w:numbering xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
        <w:abstractNum w:abstractNumId="0">
        <w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="decimal"/><w:lvlText w:val="%1."/></w:lvl>
        <w:lvl w:ilvl="1"><w:start w:val="1"/><w:numFmt w:val="lowerLetter"/><w:lvlText w:val="(%2)"/></w:lvl>
        </w:abstractNum>
        <w:abstractNum w:abstractNumId="1"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="bullet"/><w:lvlText w:val="&#xf0b7;"/></w:lvl></w:abstractNum>
        <w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num>
        <w:num w:numId="2"><w:abstractNumId w:val="1"/></w:num>
        </w:numbering>'''
        (ROOT / 'build').mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='agreement-')
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / 'original.docx'
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr('word/document.xml', document)
            archive.writestr('word/numbering.xml', numbering)
            if styled:
                archive.writestr('word/styles.xml', '''<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
                <w:style w:styleId="BaseList"><w:pPr><w:numPr><w:numId w:val="1"/></w:numPr></w:pPr></w:style>
                <w:style w:styleId="TermsList"><w:basedOn w:val="BaseList"/></w:style></w:styles>''')
        return path

    def test_original_words_numbering_and_bullets_are_retained(self):
        path = self.fixture()
        original = path.read_bytes()
        text = agreement_text(path).decode('utf-8-sig')
        self.assertIn('1.\tFirst condition', text)
        self.assertIn('  (a)\tSubcondition', text)
        self.assertIn('2.\tSecond condition', text)
        self.assertIn('\u2022\tBullet condition', text)
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(agreement_text(path), agreement_text(path))

    def test_unsupported_content_fails_instead_of_silently_losing_terms(self):
        with self.assertRaisesRegex(ValueError, 'unsupported'):
            agreement_text(self.fixture(drawing=True))

    def test_inherited_list_style_retains_section_numbers(self):
        text = agreement_text(self.fixture(styled=True)).decode('utf-8-sig')
        self.assertIn('1.\tFirst condition', text)
        self.assertIn('2.\tSecond condition', text)


if __name__ == '__main__':
    unittest.main()
