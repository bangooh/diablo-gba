"""Integration tests with the actual user-supplied MPQ and encrypted release data."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROM_HASH = "5edcb0d022283672131427d415d310ffea4b82ec2e27e960334f746be7b58127"


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mpq", type=Path)
    parser.add_argument("package", type=Path)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    mpq, pack, binary, driver = map(Path.resolve, (args.mpq, args.package, args.binary, args.driver))
    checks = []
    with tempfile.TemporaryDirectory(prefix="builder095-") as work:
        root = Path(work)
        output = root / "검증한 ROM.gba"  # "Verified ROM": intentionally tests Korean filenames.

        def run(name, *, source=mpq, package=pack, target=output, expect=0, mode=None):
            command = [str(driver if mode else binary), str(source), str(package), str(target)]
            if mode:
                command.append(mode)
            start = time.monotonic()
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            assert result.returncode == expect, (name, result.returncode, result.stderr)
            checks.append({"check": name, "result": "PASS", "exit_code": expect,
                           "seconds": round(time.monotonic()-start, 3)})
            return result

        source_hash = digest(mpq)
        run("Actual MPQ restores V0.9.5; Unicode and spaces in output path")
        assert output.stat().st_size == 33554432 and digest(output) == ROM_HASH
        checks.append({"check": "Output byte count and SHA-256 equal the verified V0.9.5 ROM", "result": "PASS"})
        run("Existing output preserved", expect=9)
        assert digest(output) == ROM_HASH
        new = root / "new.gba"
        run("Wrong output extension rejected", target=root / "new.sav", expect=10)
        run("Input cannot be overwritten", target=mpq, expect=10)
        run("Missing MPQ rejected", source=root / "absent.mpq", target=new, expect=4)
        short = root / "short.mpq"
        short.write_bytes(b"MPQ\x1a")
        run("Truncated MPQ rejected", source=short, target=new, expect=4)
        wrong = root / "wrong.mpq"
        with wrong.open("wb") as file:
            file.write(b"MPQ\x1a")
            file.truncate(mpq.stat().st_size)
        run("Different MPQ with correct size and signature rejected", source=wrong, target=new, expect=5)
        run("Missing data package rejected", package=root / "absent.dgb", target=new, expect=1)
        broken = root / "broken.dgb"
        raw = pack.read_bytes()
        for label, offset, expect in [("magic", 0, 2), ("nonce", 8, 5), ("ROM hash", 20, 2),
                                     ("reserved header", 52, 2), ("ciphertext", 1024, 5), ("tag", -1, 5)]:
            changed = bytearray(raw)
            changed[offset] ^= 1
            broken.write_bytes(changed)
            run(f"Modified package {label} rejected", package=broken, target=new, expect=expect)
            assert not new.exists()
        for label, contents in [("truncated", raw[:1024]), ("appended", raw+b"extra")]:
            broken.write_bytes(contents)
            run(f"{label.title()} data rejected", package=broken, target=new, expect=2)
        run("Unwritable output location rejected", target=root / "absent" / "game.gba", expect=7)
        for mode in ("check", "mpq", "package", "decrypt", "verify", "save", "commit"):
            run(f"Cancellation during {mode}: no final or partial ROM", target=new, expect=8, mode=mode)
            assert not new.exists() and not list(root.glob("*.partial*"))
        run("Concurrent output creation preserved at commit", target=new, expect=9, mode="race")
        assert new.read_bytes() == b"concurrent writer"
        new.unlink()
        link = root / "link.gba"
        link.symlink_to(output)
        run("Symlink output preserved", target=link, expect=9)
        dangling = root / "dangling.gba"
        dangling.symlink_to(root / "missing-target")
        run("Dangling symlink output preserved", target=dangling, expect=9)
        unicode_mpq = root / "원본 디아블로.MPQ"  # "Original Diablo": Unicode MPQ path fixture.
        unicode_mpq.symlink_to(mpq)
        unicode_pack = root / "변환 자료.dgb"  # "Conversion data": Unicode package path fixture.
        unicode_pack.symlink_to(pack)
        run("Unicode and space characters in all input paths", source=unicode_mpq, package=unicode_pack, target=new)
        assert digest(new) == ROM_HASH
        assert digest(mpq) == source_hash
        checks.append({"check": "Original MPQ unchanged after all tests", "result": "PASS"})
        assert not list(root.glob("*.partial*"))

    report = {"builder": "0.9.5", "rom": "V0.9.5", "rom_sha256": ROM_HASH,
              "mpq_bytes": mpq.stat().st_size,
              "backend_tested": "Native Linux x86_64 / selected crypto backend", "windows_runtime_tested": False,
              "hardware_tested": False, "asset_reencoding": False, "checks": checks}
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"passed": len(checks), "report": str(args.report)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
