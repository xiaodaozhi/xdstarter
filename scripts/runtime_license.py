"""Deterministic text view of the pinned Microsoft DOCX agreement for Inno.

The unmodified original is always delivered alongside the display text. This
converter accepts simple text/list documents only; unsupported content fails.
"""
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile

NS = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
VAL = '{' + NS['w'] + '}val'


def number(value, kind):
    if kind == 'decimal':
        return str(value)
    if kind in ('lowerLetter', 'upperLetter'):
        result = ''
        while value:
            value, digit = divmod(value - 1, 26)
            result = chr(97 + digit) + result
        return result.upper() if kind == 'upperLetter' else result
    if kind in ('lowerRoman', 'upperRoman'):
        if not 0 < value < 4000:
            raise ValueError('Unsupported Roman list value')
        result = ''
        for integer, symbol in ((1000, 'M'), (900, 'CM'), (500, 'D'), (400, 'CD'),
                                (100, 'C'), (90, 'XC'), (50, 'L'), (40, 'XL'),
                                (10, 'X'), (9, 'IX'), (5, 'V'), (4, 'IV'), (1, 'I')):
            while value >= integer:
                result += symbol
                value -= integer
        return result.lower() if kind == 'lowerRoman' else result
    raise ValueError('Unsupported agreement numbering format: ' + kind)


def agreement_text(path):
    with zipfile.ZipFile(Path(path)) as archive:
        document = ET.fromstring(archive.read('word/document.xml'))
        numbering = ET.fromstring(archive.read('word/numbering.xml'))
        styles = {}
        if 'word/styles.xml' in archive.namelist():
            styles = {style.get('{'+NS['w']+'}styleId'): style for style in
                      ET.fromstring(archive.read('word/styles.xml')).findall('w:style', NS)}
        if any(document.findall('.//w:' + tag, NS) for tag in
               ('tbl', 'drawing', 'footnoteReference', 'endnoteReference', 'txbxContent')):
            raise ValueError('Agreement contains unsupported non-paragraph content')
        abstract = {item.get('{'+NS['w']+'}abstractNumId'): {
            int(level.get('{'+NS['w']+'}ilvl')): {
                'start': int(level.find('w:start', NS).get(VAL)),
                'format': level.find('w:numFmt', NS).get(VAL),
                'text': level.find('w:lvlText', NS).get(VAL)}
            for level in item.findall('w:lvl', NS)}
            for item in numbering.findall('w:abstractNum', NS)}
        lists = {}
        for item in numbering.findall('w:num', NS):
            identifier = item.get('{'+NS['w']+'}numId')
            levels = {key: dict(value) for key, value in
                      abstract[item.find('w:abstractNumId', NS).get(VAL)].items()}
            for override in item.findall('w:lvlOverride', NS):
                level = int(override.get('{'+NS['w']+'}ilvl'))
                start = override.find('w:startOverride', NS)
                if override.find('w:lvl', NS) is not None:
                    raise ValueError('Unsupported agreement level override')
                if start is not None:
                    levels[level]['start'] = int(start.get(VAL))
            lists[identifier] = levels
        counters, output = {}, []
        for paragraph in document.findall('w:body/w:p', NS):
            text = ''.join(node.text or '' if node.tag == '{'+NS['w']+'}t' else
                           '\t' if node.tag == '{'+NS['w']+'}tab' else '\n'
                           for node in paragraph.iter() if node.tag in
                           ('{'+NS['w']+'}t', '{'+NS['w']+'}tab', '{'+NS['w']+'}br', '{'+NS['w']+'}cr'))
            properties, chain, visited = {}, [], set()
            style_node = paragraph.find('w:pPr/w:pStyle', NS)
            style_id = style_node.get(VAL) if style_node is not None else None
            while style_id in styles:
                if style_id in visited:
                    raise ValueError('Agreement has cyclic paragraph style inheritance')
                visited.add(style_id)
                style = styles[style_id]
                chain.append(style)
                parent = style.find('w:basedOn', NS)
                style_id = parent.get(VAL) if parent is not None else None
            for owner in [*reversed(chain), paragraph]:
                values = owner.find('w:pPr/w:numPr', NS)
                if values is not None:
                    properties.update({key: values.find('w:'+key, NS).get(VAL)
                                       for key in ('numId', 'ilvl') if values.find('w:'+key, NS) is not None})
            if 'numId' in properties:
                identifier = properties['numId']
                level = int(properties.get('ilvl', '0'))
                if identifier != '0':
                    definition = lists[identifier][level]
                    for key in list(counters):
                        if key[0] == identifier and key[1] > level:
                            del counters[key]
                    key = (identifier, level)
                    counters[key] = counters.get(key, definition['start'] - 1) + 1
                    label = definition['text']
                    if definition['format'] == 'bullet':
                        label = label.replace('\uf0b7', '\u2022').replace('\uf0a7', '\u25aa')
                    elif definition['format'] != 'none':
                        def replace(match):
                            index = int(match[1]) - 1
                            value = counters.get((identifier, index), lists[identifier][index]['start'])
                            return number(value, lists[identifier][index]['format'])
                        label = re.sub(r'%([1-9])', replace, label)
                    text = '  ' * level + label + '\t' + text
            output.append(text)
        if not any(output):
            raise ValueError('Empty Microsoft agreement')
        return b'\xef\xbb\xbf' + ('\n\n'.join(output).rstrip() + '\n').encode('utf-8')
