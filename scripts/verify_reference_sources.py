"""Verify pinned reference source bytes using the public hash allowlist."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
rows=json.loads((root/'docs/reference-code-allowlist.json').read_text())
for name,digest in rows.items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,name
print(f'PASS: {len(rows)} exact reference hashes (offline)')
