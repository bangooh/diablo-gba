"""Asset-free regression coverage for generation diagnostics and failure handling."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import check_build_environment as env
import regenerate_complete as complete
import regenerate_foundation as foundation


class BuildEnvironmentTests(unittest.TestCase):
    def test_streamed_fingerprint_and_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.mpq'
            data = b'fixture' * 200000
            path.write_bytes(data)
            self.assertEqual(env.fingerprint(path), dict(bytes=len(data),
                md5=hashlib.md5(data).hexdigest(), sha256=hashlib.sha256(data).hexdigest()))
            with patch.object(env, 'check_environment', side_effect=lambda *args: []), contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(SystemExit, 'does not match'):
                    env.preflight(path, expected_sha256='0' * 64)
                self.assertEqual(env.preflight(path, expected_sha256=hashlib.sha256(data).hexdigest().upper())['bytes'], len(data))

    def test_windows_explains_wsl_without_running_tools(self):
        with patch.object(sys, 'platform', 'win32'), patch.object(env.subprocess, 'run') as run:
            self.assertIn('WSL2', env.check_environment()[0])
            run.assert_not_called()

    def test_missing_dependencies_are_aggregated(self):
        with patch.object(sys, 'platform', 'linux'), patch.object(env.importlib, 'import_module', side_effect=ImportError('missing')), patch.object(env.shutil, 'which', return_value=None):
            errors = '\n'.join(env.check_environment())
            for name in ('PIL', 'mpyq', 'dclimplode', 'numpy', 'gcc', 'g++', 'ffmpeg'):
                self.assertIn(name, errors)

    def test_first_failed_stage_stops_and_preserves_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'repo'
            for name in ('tools', 'src', 'source_assets'):
                (root / 'game-source' / name).mkdir(parents=True)
            mpq = Path(directory) / 'fixture.mpq'
            mpq.write_bytes(b'fixture')
            output = Path(directory) / 'output'
            def fail(*args, **kwargs):
                kwargs['stdout'].write('compiler missing\n')
                return type('Result', (), {'returncode': 1})()
            stderr = io.StringIO()
            with patch.object(foundation, 'ROOT', root), patch.object(sys, 'argv', ['foundation', '--mpq', str(mpq), '--output', str(output)]), patch.object(foundation.subprocess, 'run', side_effect=fail) as run, contextlib.redirect_stderr(stderr), contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit):
                    foundation.main()
            self.assertEqual(run.call_count, 1)
            report = json.loads((output / 'regeneration-report.json').read_text())
            self.assertEqual(report['steps'][0]['name'], 'ui')
            self.assertEqual(report['steps'][0]['returncode'], 1)
            self.assertIn('compiler missing', stderr.getvalue())
            self.assertIn(str(output / 'logs/ui.log'), stderr.getvalue())

    def test_failed_foundation_cannot_resume(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / 'regeneration-report.json').write_text(json.dumps({'steps': [{'returncode': 1}]}))
            with patch.object(sys, 'argv', ['complete', '--mpq', 'unused.mpq', '--output', str(output), '--resume']), patch.object(complete, 'preflight', return_value={}), patch.object(complete.subprocess, 'run') as run, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    complete.main()
                self.assertEqual(raised.exception.code, 2)
                run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
