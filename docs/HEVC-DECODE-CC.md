# HEVC decode for the Continuity Camera on an AMD GPU

Findings from 2026-11-21 on a MacPro7,1 (Intel CPU, AMD Radeon RX 6900 XT, device 0x73bf, WhateverGreen 1.7.1, Lilu 1.7.2, boot-arg `unfairgva=7`,
macOS 15.8.2). Measured, not assumed; the last section says what is still unknown.

## What the Mac does today

- `VTIsHardwareDecodeSupported` says yes for H.264 (`avc1`), HEVC (`hvc1`) and AV1; no for `hev1`, VP9 and ProRes.
- The Continuity Camera stream is HEVC 1920x1080. `ContinuityCaptureAgent` logs, in order:
  `VCPDec ... (1920x1080, HEVC): Detected Intel-Mac with AMD-GPU capable of HW dGPU decoding`, `Creating HW or fallback SW decoder`, then
  `VTDecoderXPCService: (AppleGVA) [HEVC] GVA ERROR: Couldn't set the stream resolution`, the decoder pipeline is torn down, and
  `VCPDec ... (1920x1080, HEVC): Use default SW decoder`.
- In 40 minutes of the log, every one of 10 HEVC decoder creations ended at that error. H.264 playback sessions did not (5 `Playback Usage` lines and no error).
  So HEVC is decoded in software here, whatever VideoToolbox reports. (AV1 runs on the hardware through a `com.droga.vtav1vcn` VideoToolbox plug-in.)

## Where it fails (AppleGVA, from the dyld shared cache)

Extracted with `ipsw dyld extract` and read with REA. In `AppleGVA`, the HEVC decoder setup (`sub_7ffa0671bef2`) creates a media accelerator interface
(`_AVF_CreateMediaAcceleratorInterface`, type 0xe), and for each accelerator runs: create (slot 0x20), allocate a 0x1800000-byte bitstream buffer pool
(slot 7, error stage 3), then **set property 8 with the stream resolution (slot 0x16, error stage 4)**; a non-zero result from that call logs
"Couldn't set the stream resolution" and tears everything down. Slot 0x16 is a thunk into the accelerator object that `AVF_CreateMediaAcceleratorInterface`
wraps, which lives in the AMD VA driver (`AMDRadeonVADriver2`; its executable is only in the shared cache). The pool allocation before it succeeds,
so the GPU's decoder is reachable; the resolution call is the one refused.

## What is not known

- Why the AMD VA driver refuses the resolution call for this GPU (a size check, an ASIC or board-id check, or a missing VCN capability). `AMDRadeonVADriver2`
  has no useful strings; its dispatch has not been traced.
- Whether a Kernel patch or a Lilu/WhateverGreen change fixes it. The boot-args already enable WhateverGreen's AppleGVA patches (`unfairgva=7`); no further
  OpenCore setting is documented here, and none has been added to the configuration, because a patch without the failing check identified would be a guess.

## To go further

1. Trace `AMDRadeonVADriver2` from the property-8 handler (the object returned for type 0xe) to the check that returns non-zero.
2. Compare with an H.264 session, which passes the same call.
3. Test a HEVC file outside the Continuity Camera path (QuickTime or Safari) to separate a general HEVC failure from one specific to the camera stream.
