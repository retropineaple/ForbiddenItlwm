"""Compile the real listener, TX teardown and state task against host gate/firmware stubs.

Tests bookkeeping and ordering, not firmware or radio behavior.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

root = Path(__file__).resolve().parents[1]


class AWDLReconnectRecoveryTests(unittest.TestCase):
    def test_compiled_driver(self):
        source = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
        bodies = []
        for signature in ("int ItlIwx::\nawdlCoexListener(",
                          "int ItlIwx::\nawdlCoexTxSetup(",
                          "int ItlIwx::\nawdlCoexTxTeardown(",
                          "void ItlIwx::\niwx_newstate_task("):
            start = source.index(signature)
            end = source.index("\n}\n", start) + 3
            bodies.append(source[start:end])
        harness = (root / "tests/awdl_reconnect_recovery_host.cpp").read_text()
        program = harness.replace("// DRIVER_IMPLEMENTATIONS", "\n".join(bodies))
        with tempfile.TemporaryDirectory(prefix="forbiddenitlwm-reconnect-") as directory:
            src = Path(directory) / "test.cpp"
            src.write_text(program)
            output = Path(directory) / "test"
            subprocess.run(["xcrun", "clang++", "-std=c++14", "-pthread",
                            "-fsanitize=address,undefined", str(src), "-o", str(output)], check=True)
            for case in ("tx-only", "add-wait", "add-rollback-failure", "add-timeout", "add-restart",
                         "remove-wait", "timeout", "restart", "remove-restart",
                         "rollback-failure", "normal", "equal-run", "no-owner", "tx-failure",
                         "setup-sta", "setup-sta-failure", "setup-sta-restart", "setup-sta-timeout",
                         "setup-sta-shutdown", "setup-queue", "setup-queue-failure",
                         "setup-queue-restart", "setup-queue-timeout", "setup-queue-shutdown",
                         "setup-queue-negative", "transition-run-stop", "transition-deauth",
                         "transition-auth", "transition-commit",
                         "transition-run", "transition-run-stop-failure", "transition-deauth-failure",
                         "transition-run-stop-restart", "transition-deauth-restart",
                         "transition-run-stop-owner",
                         "transition-run-stop-shutdown", "transition-deauth-shutdown"):
                with self.subTest(case=case):
                    result = subprocess.run([str(output), case], capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
