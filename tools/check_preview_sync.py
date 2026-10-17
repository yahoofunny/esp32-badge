#!/usr/bin/env python3
"""Validate real firmware sources; the pet preview uses these sources directly."""
from native_checks import run_checks
from make_clawd_album import OUTPUT,generate

if __name__ == '__main__':
    source,files=generate()
    if OUTPUT.read_text(encoding='utf-8')!=source:
        raise SystemExit('Built-in Clawd GIF bytes are stale; run tools/make_clawd_album.py')
    print(f'PASS: {len(files)} unique built-in Clawd GIFs match original assets',flush=True)
    run_checks()
