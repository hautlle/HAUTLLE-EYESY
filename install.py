#!/usr/bin/env python3
"""Prepare, activate, or undo a HAUTLLE EYESY source installation."""
# Copyright (c) 2026, HAUTLLE. BSD-3-Clause; see LICENSE.txt.
import sys
from pathlib import Path

sys.dont_write_bytecode = True
if sys.version_info < (3, 7):
    sys.exit('Python 3.7 or later is required; nothing has been installed.')
sys.path.insert(0, str(Path(__file__).resolve().parent / 'build-support'))
from installer import main

if __name__ == '__main__':
    sys.exit(main())
