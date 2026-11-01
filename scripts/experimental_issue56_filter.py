"""Filter a disposable runner's Procmon CSV to our four owned probe processes."""
import csv
import json
import re
import sys
from pathlib import Path


def text_encoding(path):
    prefix = path.open('rb').read(4)
    return 'utf-16' if prefix.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'


def run(csv_path, roots_path, probes_path, output_dir):
    roots = json.loads(roots_path.read_text(encoding='utf-8-sig'))
    probes = json.loads(probes_path.read_text(encoding='utf-8-sig'))
    labels = {str(probe['pid']): probe['label'] for probe in probes['queries']}
    if len(labels) != 4:
        raise ValueError('Expected four distinct owned probe processes')
    substitutions = sorted(((str(value), '$' + key.upper()) for key, value in roots.items()),
                           key=lambda pair: len(pair[0]), reverse=True)

    def clean(value):
        for old, new in substitutions:
            value = re.sub(re.escape(old), lambda _: new, value, flags=re.IGNORECASE)
        return re.sub(r'S-1-5-21-(?:\d+-){3}\d+', '<RUNNER_SID>', value, flags=re.IGNORECASE)

    fields = ['query', 'time', 'operation', 'path', 'result', 'detail']
    counts = {label: 0 for label in labels.values()}
    with csv_path.open(encoding=text_encoding(csv_path), newline='', errors='replace') as source, \
            (output_dir / 'filtered.csv').open('w', encoding='utf-8', newline='') as destination:
        reader = csv.DictReader(source)
        required = {'Time of Day', 'Process Name', 'PID', 'Operation', 'Path', 'Result', 'Detail'}
        if not required.issubset(set(reader.fieldnames or [])):
            raise ValueError('Procmon CSV does not have the expected columns')
        writer = csv.DictWriter(destination, fieldnames=fields)
        writer.writeheader()
        for row in reader:
            pid = row['PID'].strip()
            if pid not in labels or row['Process Name'].casefold() != 'shell-name-probe.exe':
                continue
            label = labels[pid]
            counts[label] += 1
            writer.writerow({'query': label, 'time': row['Time of Day'],
                             'operation': row['Operation'], 'path': clean(row['Path']),
                             'result': row['Result'], 'detail': clean(row['Detail'])})
    if any(value == 0 for value in counts.values()):
        raise ValueError('Capture is incomplete for at least one probe: ' + repr(counts))
    (output_dir / 'trace-summary.json').write_text(json.dumps({'counts': counts}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    run(*(Path(item) for item in sys.argv[1:]))
