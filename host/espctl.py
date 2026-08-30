"""Compatibility wrapper for the old espctl module name."""

from __future__ import annotations

import sys

from .otalink import main


if __name__ == "__main__":
    sys.exit(main())
