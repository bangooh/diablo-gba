"""Exercise the embedded-package core and inspect the exact Windows EXE payload."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROM_HASH = "b9378d07833a21496f1aee3d0d0f9c1f8110f093ac40a58f0708d0412e3d6134"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("mpq", type=Path)
parser.add_argument("package", type=Path)
parser.add_argument("exe", type=Path)
parser.add_argument("driver", type=Path)
parser.add_argument("report", type=Path)
args = parser.parse_args()
mpq = args.mpq.resolve()
data, payload = args.exe.read_bytes(), args.package.read_bytes()
checks = []

def passed(name):
    checks.append({"check": name, "result": "PASS"})

pe = struct.unpack_from("<I", data, 0x3c)[0]
assert data[pe:pe+4] == b"PE\0\0"
machine, sections, _, sym_pointer, symbols, optional_size = struct.unpack_from("<HHIIIH", data, pe+4)
optional = pe+24
assert machine == 0x8664 and struct.unpack_from("<H", data, optional)[0] == 0x20b
assert struct.unpack_from("<H", data, optional+68)[0] == 2
assert sym_pointer == symbols == 0
assert struct.unpack_from("<II", data, optional+112+48) == (0, 0)
passed("Windows x64 GUI binary; normal symbols and debug directory removed")
offset = data.find(payload)
assert offset >= 0 and data.find(payload, offset+1) < 0
passed("EXE contains one byte-identical verified V0.9.5 encrypted package")
container = None
for i in range(sections):
    start = optional+optional_size+i*40
    name = data[start:start+8].rstrip(b"\0").decode()
    raw_size, raw_offset = struct.unpack_from("<II", data, start+16)
    flags = struct.unpack_from("<I", data, start+36)[0]
    if raw_offset <= offset and offset+len(payload) <= raw_offset+raw_size:
        assert flags & 0x40000000 and not flags & 0xa0000000
        container = name
assert container is not None
passed("Embedded package is mapped read-only and non-executable")
with mpq.open("rb") as file:
    mpq_digest = hashlib.file_digest(file, "sha256").digest()
key = hashlib.sha256(b"DiabloGBA/0.9.4/MPQ-ROM/v2"+mpq_digest).digest()
assert key not in data and mpq_digest not in data and mpq_digest.hex().encode() not in data
passed("EXE does not contain the reference MPQ hash or derived AES key")

with tempfile.TemporaryDirectory(prefix="single-exe-test-") as temporary:
    work = Path(temporary)
    driver = work/"standalone-builder"
    shutil.copy2(args.driver, driver)
    output = work/"검증 ROM.gba"  # "Verification ROM": intentionally tests Korean filenames.

    def run(name, mode="normal", expect=0, source=mpq, target=output):
        result = subprocess.run([str(driver), str(source), str(target), mode], cwd=work,
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == expect, (name, result.returncode, result.stderr)
        passed(name)

    run("Standalone native binary restores from actual MPQ without an external data file")
    assert output.stat().st_size == 33554432 and hashlib.sha256(output.read_bytes()).hexdigest() == ROM_HASH
    passed("Restored ROM exactly matches the V0.9.5 SHA-256 and 32 MiB size")
    run("Existing output remains unchanged", expect=9)
    assert hashlib.sha256(output.read_bytes()).hexdigest() == ROM_HASH
    output.unlink()
    for mode, code in [("null",2),("short",2),("long",2),("header",2),("cipher",5),("tag",5)]:
        run("Invalid embedded data rejected: "+mode, mode=mode, expect=code)
        assert not output.exists()
    run("Cancellation removes incomplete output", mode="cancel", expect=8)
    assert not output.exists() and not list(work.glob("*.partial*"))
    run("Output created concurrently is preserved", mode="race", expect=9)
    assert output.read_bytes() == b"concurrent writer"
    output.unlink()
    wrong = work/"unsupported.MPQ"
    with wrong.open("wb") as file:
        file.write(b"MPQ\x1a")
        file.truncate(mpq.stat().st_size)
    run("Unsupported MPQ with matching size and signature rejected", source=wrong, expect=5)
    assert not output.exists()
    wrong.unlink()
    run("Missing MPQ rejected", source=wrong, expect=4)
    run("Invalid output extension rejected", target=work/"game.sav", expect=10)
    assert {p.name for p in work.iterdir()} == {driver.name}
    passed("No data-package extraction or leftover partial files")

report = {"builder": "0.9.5", "exe_bytes": len(data),
          "mpq_bytes": mpq.stat().st_size,
          "exe_sha256": hashlib.sha256(data).hexdigest(), "rom_sha256": ROM_HASH,
          "payload_sha256": hashlib.sha256(payload).hexdigest(), "payload_section": container,
          "windows_runtime_tested": False, "native_runtime_tested": "Linux x64 / OpenSSL",
          "checks": checks}
args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
print(json.dumps({"checks_passed":len(checks), "exe_bytes":len(data), "report":str(args.report)}))
