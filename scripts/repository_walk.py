"""Enumerate repository files without descending into generated directories."""
import os
from pathlib import Path


def repository_files(root, excluded):
    files = []
    for directory, subdirs, names in os.walk(root, followlinks=False):
        subdirs[:] = [name for name in subdirs if name not in excluded]
        files.extend(Path(directory) / name for name in names
                     if name not in excluded and (Path(directory) / name).is_file())
    return files
