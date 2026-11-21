#!/usr/bin/env python3
"""Set one IO80211 AWDL control knob on awdl0 (apple80211 ioctl AWDL_CTL, type 162), for experiments that need no rebuild.

The first use: the family drops the iPhone from its peer table every 6-11 s during a Continuity Camera session
(IO80211AWDLPeer::shouldRemovePeerFromNDP, IO80211Family 24H232 0xffffff8002149bf5: no action frame for >2000 ms and no
unicast activity for 6000 ms), which ends the real-time group and stalls the video. `ndpRemovalTimeoutmSec` replaces both
limits (at most 100000, values under 2000 mean the default):

    sudo python3 -I tools/diag/awdl_ctl.py ndpRemovalTimeoutmSec 100000
    sudo python3 -I tools/diag/awdl_ctl.py ndpRemovalTimeoutmSec 0        # back to the default

Request layout, from the family's static setAWDL_CTL (0xffffff800221bd6f) and IO80211AWDLPeerManager::setAwdlCtl
(0xffffff800216c154):
    the request's req_len must be exactly 0x3B8A0 (anything else is EINVAL, 22)
    +0x004  name, NUL-terminated, at most 0x80 bytes (compared with each knob's name)
    +0x088  value (the knobs read it as a 32-bit or 64-bit integer)
    +0x090  length of the extra data at +0x094; 0 makes the handler return success WITHOUT calling setAwdlCtl, so the tool
            always sends at least 4
The knob is applied in the family's memory; it is gone after a reboot or when AWDL is torn down.

Known names in setAwdlCtl: ndpRemovalTimeoutmSec, serviceInactivityTimeout, ndpPostingGuardTimemSec, refuse_scans,
reset_stats, mdns_direct_path, print_all_election_id, adoptNANChannelSequence, verbose_master_indication,
airplaySoloModeEnabled, awdlElectionOnHostEnabled. Only ndpRemovalTimeoutmSec has been exercised.
"""
import ctypes
import ctypes.util
import socket
import struct
import sys

AWDL_CTL = 162
REQUEST_LEN = 0x3B8A0
NAME_OFFSET, NAME_MAX = 0x04, 0x80
VALUE_OFFSET = 0x88
DATA_LEN_OFFSET = 0x90
DATA_OFFSET = 0x94
SIOCSA80211 = 0x802869C8   # _IOW('i', 200, struct apple80211req), 0x28 bytes
SIOCGA80211 = 0xC02869C9


def build_payload(name, value, data_len=4):
    """The 0x3B8A0-byte AWDL_CTL request body."""
    raw = name.encode()
    if not raw or len(raw) >= NAME_MAX:
        raise ValueError("name must be 1..127 bytes")
    if not 0 < data_len <= 0x3B800 or data_len > REQUEST_LEN - DATA_OFFSET:
        raise ValueError("data_len must be between 1 and 0x3B800")
    body = bytearray(REQUEST_LEN)
    body[NAME_OFFSET:NAME_OFFSET + len(raw)] = raw
    struct.pack_into("<Q", body, VALUE_OFFSET, value & 0xFFFFFFFFFFFFFFFF)
    struct.pack_into("<I", body, DATA_LEN_OFFSET, data_len)
    return bytes(body)


def build_request_header(ifname, body_address, length=REQUEST_LEN, req_type=AWDL_CTL):
    """struct apple80211req: ifname[16], req_type, req_val, req_len, pad, req_data pointer."""
    return struct.pack("<16sIiIIQ", ifname.encode(), req_type, 0, length, 0, body_address)


def main(argv):
    if len(argv) < 3 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0 if len(argv) >= 2 and argv[1] in ("-h", "--help") else 2
    name, value = argv[1], int(argv[2], 0)
    ifname = argv[3] if len(argv) > 3 else "awdl0"
    body = ctypes.create_string_buffer(build_payload(name, value), REQUEST_LEN)
    header = ctypes.create_string_buffer(build_request_header(ifname, ctypes.addressof(body)), 0x28)
    libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
    fd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        if libc.ioctl(fd.fileno(), ctypes.c_ulong(SIOCSA80211), header) != 0:
            err = ctypes.get_errno()
            print(f"SIOCSA80211(AWDL_CTL {name}={value}) on {ifname} failed: errno {err}")
            return 1
    finally:
        fd.close()
    print(f"set {name} = {value} on {ifname}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
