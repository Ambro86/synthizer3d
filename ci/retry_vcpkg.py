"""Recover vcpkg setup/downloads after transient network failures."""
import os
from pathlib import Path
import random
import re
import subprocess
import time


VCPKG_COMMIT = "817892b0ecbc6af994dd6df999c73faf795adbf2"
NETWORK_ERROR = re.compile(
    r"could not resolve (?:host|proxy)|temporary failure in name resolution|"
    r"failed to connect|connection (?:reset|refused|timed out|closed)|"
    r"operation timed out|network is unreachable|remote end hung up|"
    r"unexpected disconnect|early EOF|TLS connection was non-properly terminated|"
    r"SSL_ERROR_SYSCALL|HTTP/[\d.]+ stream .*not closed cleanly|"
    r"(?:HTTP(?: error)?|status(?: code)?|returned error:)\s*[:=]?\s*(?:408|429|5\d\d)\b|"
    r"curl:\s*\((?:5|6|7|18|28|35|52|55|56|92)\)|"
    r"curl operation failed with error code (?:5|6|7|18|28|35|52|55|56|92)\b",
    re.IGNORECASE,
)


def run(command, cwd):
    """Retry only transient network errors, without an attempt limit."""
    attempt = 0
    while True:
        attempt += 1
        with subprocess.Popen(
            command, cwd=str(cwd), stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace",
        ) as process:
            network_error = False
            for line in process.stdout:
                print(line, end="", flush=True)
                network_error = network_error or bool(NETWORK_ERROR.search(line))
            code = process.wait()
        if code == 0:
            return
        if not network_error:
            raise subprocess.CalledProcessError(code, command)
        delay = min(30 * attempt, 120) + random.randint(0, 15)
        print("Transient network failure; retrying in {} seconds (attempt {}).".format(
            delay, attempt + 1), flush=True)
        time.sleep(delay)


def main():
    workspace = Path(os.environ["GITHUB_WORKSPACE"])
    root = workspace / "vcpkg"
    root.mkdir(exist_ok=True)
    # The action may fail before cloning, bootstrapping or exporting VCPKG_ROOT.
    if not (root / ".git").exists():
        run(["git", "init"], root)
    run(["git", "fetch", "--depth=1", "https://github.com/microsoft/vcpkg.git",
         VCPKG_COMMIT], root)
    run(["git", "checkout", VCPKG_COMMIT], root)
    if os.name == "nt":
        run(["cmd.exe", "/c", str(root / "bootstrap-vcpkg.bat"), "-disableMetrics"], root)
        executable = root / "vcpkg.exe"
    else:
        run(["sh", str(root / "bootstrap-vcpkg.sh"), "-disableMetrics"], root)
        executable = root / "vcpkg"
    os.environ["VCPKG_ROOT"] = str(root)
    run([str(executable), "install", "--triplet", os.environ["VCPKG_DEFAULT_TRIPLET"]], workspace)
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as env_file:
        env_file.write("VCPKG_ROOT={}\n".format(root))


if __name__ == "__main__":
    main()
