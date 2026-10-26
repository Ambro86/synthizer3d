import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from retry_vcpkg import NETWORK_ERROR, run


class NetworkRetryTests(unittest.TestCase):
    def test_transient_errors(self):
        for message in (
            'curl: (6) Could not resolve host: github.com',
            'curl: (28) Operation timed out',
            'fatal: unable to access URL: The requested URL returned error: 503',
            'HTTP 429', 'status code 502', 'Connection reset by peer',
            'fatal: early EOF',
            "error: curl operation failed with error code 7 (Couldn't connect to server).",
        ):
            with self.subTest(message=message):
                self.assertRegex(message, NETWORK_ERROR)

    def test_permanent_errors(self):
        for message in ('error: compiler failed', 'status code 404',
                        'SHA512 mismatch', 'HTTP 403', 'SyntaxError: invalid syntax',
                        'curl operation failed with error code 60 (certificate error)'):
            with self.subTest(message=message):
                self.assertNotRegex(message, NETWORK_ERROR)

    def test_more_than_five_failures_then_success(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'simulate.py'
            script.write_text(
                "from pathlib import Path\n"
                "p = Path('attempts')\n"
                "n = int(p.read_text()) if p.exists() else 0\n"
                "p.write_text(str(n + 1))\n"
                "if n < 8:\n"
                "    print('curl: (6) Could not resolve host: github.com')\n"
                "    raise SystemExit(1)\n"
            )
            with patch('retry_vcpkg.time.sleep') as sleep, contextlib.redirect_stdout(io.StringIO()):
                run([sys.executable, str(script)], directory)
            self.assertEqual(sleep.call_count, 8)
            self.assertEqual((Path(directory) / 'attempts').read_text(), '9')

    def test_compilation_error_fails_without_retry(self):
        with patch('retry_vcpkg.time.sleep') as sleep, contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(subprocess.CalledProcessError):
                run([sys.executable, '-c', "print('error: compiler failed'); exit(1)"], '.')
            sleep.assert_not_called()

    def test_cancellation_does_not_retry(self):
        with patch('retry_vcpkg.time.sleep', side_effect=KeyboardInterrupt), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(KeyboardInterrupt):
                run([sys.executable, '-c', "print('curl: (6) Could not resolve host'); exit(1)"], '.')


if __name__ == '__main__':
    unittest.main()
