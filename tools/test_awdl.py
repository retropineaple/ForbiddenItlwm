#!/usr/bin/env python3
"""Run host AWDL protocol checks under memory/undefined-behavior sanitizers."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="forbiddenitlwm-tests-") as directory:
    for name in ("awdl_requested_configuration", "awdl_protocol", "awdl_frame_builder", "awdl_sync_prefix", "awdl_local_schedule", "awdl_probe_policy", "awdl_peer_cache", "awdl_data_reject", "awdl_channel_class", "awdl_mtu", "awdl_data_payload", "awdl_data_frame", "awdl_observation", "awdl_regulatory", "awdl_tx_burst", "awdl_peer_schedule", "awdl_build_guard", "btleadv_hci", "eapol_key_length", "wcl_transition", "counted_copy", "scan_cursor"):
        output = Path(directory) / name
        subprocess.run([
            "xcrun", "clang++", "-std=c++14",
            "-D__IO80211_TARGET=__MAC_26_0", "-I" + str(root / "include"), "-I" + str(root / "BTLEAdvFixup"), "-I" + str(root / "ForbiddenItlwm" / "WCL"),
            "-fsanitize=address,undefined", str(root / "tests" / (name + ".cpp")),
            "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True)
    # The build check again, as the Sonoma kext compiles it.
    output = Path(directory) / "awdl_build_guard_sonoma"
    subprocess.run(["xcrun", "clang++", "-std=c++14", "-D__MAC_14_4=140400", "-D__IO80211_TARGET=140400",
                    "-I" + str(root / "include"), "-fsanitize=address,undefined",
                    str(root / "tests" / "awdl_build_guard.cpp"), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
    # The build check again, as the Sequoia kext compiles it.
    output = Path(directory) / "awdl_build_guard_sequoia"
    subprocess.run(["xcrun", "clang++", "-std=c++14", "-D__MAC_15_2=150200", "-D__IO80211_TARGET=150200",
                    "-I" + str(root / "include"), "-fsanitize=address,undefined",
                    str(root / "tests" / "awdl_build_guard.cpp"), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
subprocess.run(["python3", str(root / "tests/monitor_tx_ownership.py")], check=True)
subprocess.run(["python3", str(root / "tests/monitor_channel.py")], check=True)
subprocess.run(["python3", str(root / "tests/monitor_probe.py")], check=True)
# AWDL recovery across Wi-Fi reassociation: extracted production methods under ASan/UBSan (PR #12).
subprocess.run(["python3", str(root / "tests/awdl_reconnect_recovery.py")], check=True)
# The pattern set for the bluetoothd installed here (PR #1); 26.7.1's set elsewhere.
# The default set is checked against this Mac's own bluetoothd, so it only means something on the macOS build the
# set is for. CI runners run another build; FORBIDDEN_SKIP_HOST_BLUETOOTHD=1 skips just this check there.
if not os.environ.get("FORBIDDEN_SKIP_HOST_BLUETOOTHD"):
    build = subprocess.check_output(["sw_vers", "-buildVersion"], text=True).strip()
    pattern_set = {"25G241": "2671", "25G332": "2672", "25G342": "2673"}.get(build, "2671")
    subprocess.run(["python3", str(root / "tests/btleadv_patterns.py"), "--set", pattern_set], check=True)
# The same-channel probe model and its source pins (PR #47); it builds its own test (audit T02).
subprocess.run(["python3", str(root / "tests/portable_rx/test-samech.py")], check=True)
# The Sonoma set needs a 14.8.9 bluetoothd. Point this at one to check it.
if os.environ.get("FORBIDDEN_SONOMA_BLUETOOTHD"):
    subprocess.run(["python3", str(root / "tests/btleadv_patterns.py"), "--target", "sonoma",
                    "--binary", os.environ["FORBIDDEN_SONOMA_BLUETOOTHD"]], check=True)
# The Tahoe 26.7.3 set needs a 26.7.3 bluetoothd. Point this at one to check it.
if os.environ.get("FORBIDDEN_2673_BLUETOOTHD"):
    subprocess.run(["python3", str(root / "tests/btleadv_patterns.py"), "--set", "2673",
                    "--binary", os.environ["FORBIDDEN_2673_BLUETOOTHD"]], check=True)
# The Sequoia table needs a 15.8.3 bluetoothd, which isn't on a Tahoe Mac. Point this at one to check it.
if os.environ.get("FORBIDDEN_SEQUOIA_BLUETOOTHD"):
    subprocess.run(["python3", str(root / "tests/btleadv_patterns.py"), "--target", "sequoia",
                    "--binary", os.environ["FORBIDDEN_SEQUOIA_BLUETOOTHD"]], check=True)
subprocess.run(["python3", str(root / "tests/awdl_snapshot.py")], check=True)
# The Continuity Camera run analyzer, against a recorded run and synthetic ones.
subprocess.run(["python3", "-I", str(root / "tests/cc_analyze_test.py")], check=True)
# The AirDrop checker: parsers, failure signatures and the verdict, on the 2026-11-18 failure.
subprocess.run(["python3", "-I", str(root / "tests/airdrop_check_test.py")], check=True)
subprocess.run(["python3", "-I", str(root / "tests/awdl_ctl_test.py")], check=True)
subprocess.run(["python3", str(root / "tests/awdl_bridge.py")], check=True)
subprocess.run(["python3", str(root / "tests/awdl_pool_lease.py")], check=True)
subprocess.run(["python3", str(root / "tests/awdl_rx_copy.py")], check=True)
with tempfile.TemporaryDirectory(prefix="forbidden-lqm-") as directory:
    output = str(Path(directory) / "wcl_lqm_merge")
    subprocess.run(["xcrun", "clang++", "-std=c++14", "-fsanitize=address,undefined",
                    str(root / "tests/wcl_lqm_merge.cpp"), "-o", output], check=True)
    subprocess.run([output], check=True)
