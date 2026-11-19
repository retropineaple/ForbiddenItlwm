#!/usr/bin/env python3
"""Why the driver didn't hand on the AWDL data frames the listener heard (ITLAWDLCoexRxRejectReasons).

  python3 -I tools/awdl_rx_rejects.py                 reads ioreg
  python3 -I tools/awdl_rx_rejects.py before.txt after.txt     the change between two ioreg dumps / registry_*.txt

Each frame counted in ITLAWDLCoexDataRxMalformed was a data frame on the listener's channel from a known peer that
ItlAWDL::parseDataFrame refused. The driver sorts them by the first check they failed, in the parser's own order
(include/AWDLDataFrame.hpp, classifyDataFrame). A frame that carries no payload (QoS Null) or is aggregated says
something very different from one whose IPv6 header is wrong, so the split decides what to look at next.
"""
import re
import struct
import subprocess
import sys

NAMES = ['None', 'Short', 'QosNull', 'NullData', 'OtherSubtype', 'NotAwdlBssid', 'Protected', 'HtControl',
         'Fragment', 'DsBits', 'Addresses', 'Amsdu', 'OuterLength', 'NotLlcSnap', 'NotIPv6', 'BadIPv6']
MEANING = {
    'Short': 'shorter than a header, or than the minimum payload',
    'QosNull': 'QoS Null (0xC8): no payload, a keepalive',
    'NullData': 'Null function (0x48): no payload',
    'OtherSubtype': 'another frame subtype (not plain data or QoS data)',
    'NotAwdlBssid': "BSSID isn't AWDL's",
    'Protected': 'encrypted (Protected bit)',
    'HtControl': 'has an HT Control field (Order bit)',
    'Fragment': 'fragmented',
    'DsBits': 'ToDS or FromDS set (four-address or infrastructure style)',
    'Addresses': 'a zero address, or a multicast source',
    'Amsdu': 'aggregated (A-MSDU)',
    'OuterLength': "outer data header length doesn't match: several packets in one frame",
    'NotLlcSnap': 'payload is not AWDL LLC/SNAP',
    'NotIPv6': 'AWDL data header present but not an IPv6 payload',
    'BadIPv6': 'IPv6 header or length invalid, or over 1500 bytes',
}


def read(text):
    m = re.search(r'"ITLAWDLCoexRxRejectReasons" = <([0-9a-fA-F]+)>', text)
    if not m:
        sys.exit('ITLAWDLCoexRxRejectReasons not found: is the driver new enough, and -itlawdlcoexdata on?')
    raw = bytes.fromhex(m.group(1))
    return list(struct.unpack('<%dI' % (len(raw) // 4), raw))


def load(arg):
    if arg:
        return read(open(arg).read())
    return read(subprocess.run(['ioreg', '-l', '-w0'], capture_output=True, text=True).stdout)


def main():
    args = sys.argv[1:]
    counts = load(args[0] if args else None)
    if len(args) > 1:
        after = load(args[1])
        counts = [b - a for a, b in zip(counts, after)]
    total = sum(counts[1:])
    print('rejected data frames: %d' % total)
    for i, c in sorted(enumerate(counts), key=lambda x: -x[1]):
        if i == 0 or not c:
            continue
        name = NAMES[i] if i < len(NAMES) else 'reason%d' % i
        print('  %-13s %9d  %5.1f%%  %s' % (name, c, 100.0 * c / total if total else 0, MEANING.get(name, '')))


if __name__ == '__main__':
    main()
