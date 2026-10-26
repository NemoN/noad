# Validation on 2026-10-26

Hardware: Intel Core i5-6600T, NVIDIA Quadro P600 (2 GiB), driver 580.178.04.
Libraries: FFmpeg 6.1.5, nv-codec-headers 12.1.14.0.

## Recognition regression

A full sports recording was inspected at all 16 internal commercial boundaries
using 96 decoded stills. The historical heuristic moved six ends backwards by
9.6–35.4 seconds, placing them inside adverts. Enabling
`--legacy-blackframe-refinement` reproduced all six wrong ends in the fork.
The new default preserves the eight logo-return positions. CPU and CUDA both
match all 18 keep marks in `tests/fixtures/sports-25fps.marks` exactly.
Visual transitions are bounded to about a second, not guaranteed frame-exact.

Comparing the new CPU output against the previous deployed build on six recordings:

- Six internal sports endpoints corrected; starts and the other two ends unchanged.
- One magazine endpoint restored from 31:56.16 to 32:38.88. Stills at 32:05
  and 32:20 show trailers; the programme returns around 32:39.
- One drama start mark restored from 01:55.20 to 00:00.00. The Tvheadend
  adapter ignores recording-edge trimming, so its EDL is unaffected.
- All other boundaries on these six recordings unchanged.

This validates the specific black-frame failure, not every advert in every
recording. Logo-free intros can still be false positives, and breaks can be missed.

## CPU versus CUDA/NVDEC

Six complete recordings were compared in 16 final trials. CPU/CUDA pairs used
the same binary, input bytes, VDR index, flags and one-core limit. Runs were
sequential, pinned to CPU 2 with 100% quota, nice 19 and 2 GiB memory limit.
Outputs and logos were fresh. SSD inputs were SHA-256 checked and fully
cache-warmed before every trial (mincore >=99%). The production worker remained
inactive. All recordings and production EDL files remained unchanged.

Times include hardware initialization, seeks, downloads and format conversion.
They exclude copying, indexing and cache warmup. Rows with two trials per mode
show their median; other rows are single trials. Different marks mean the modes
did not produce equivalent recognition results.

| Case | Video | Trials per mode | CPU seconds | CUDA seconds | Mark positions |
|---|---|---:|---:|---:|---|
| sports-sd | MPEG-2 576i25 | 2 | 19.79 | 22.13 | identical |
| magazine-sd-a | MPEG-2 576i25 | 2 | 14.30 | 12.73 | DIFFERENT |
| magazine-sd-b | MPEG-2 576i25 | 1 | 9.38 | 13.89 | DIFFERENT |
| documentary-sd | MPEG-2 576i25 | 1 | 8.37 | 12.88 | identical |
| drama-hd-a | H.264 720p50 | 1 | 57.21 | 104.14 | identical |
| drama-hd-b | H.264 720p50 | 1 | 94.93 | 124.99 | identical |

The reversed-order repeats reproduced the sports result and magazine mismatch.
In magazine-sd-a, CUDA moves one end 456 frames (18.24 s) later. A still at
32:00 already shows programme moderation that CUDA would skip. Magazine-sd-b
changes several boundaries by up to 576 frames (23.04 s). The cause of these
backend-dependent differences has not been fully isolated; CUDA is experimental.

**Recommendation on this hardware:** keep CPU decoding as the default. CUDA
was slower in five of six cases. The sole faster case produced unacceptable
mark changes and decoded fewer frames, so it does not establish an equivalent
recognition speedup. CUDA reduces process CPU time, but seeking, transfers and
synchronization can outweigh decoding savings. These are end-to-end noad
results, not conclusions about sequential decoding or every GPU.

Every CUDA trial logged positive hardware-frame counts and independent NVIDIA
decoder activity. There was no silent CPU fallback. Detailed metrics and frame
deltas are in `benchmark-results.json`.

## Build and failure checks

- Builds successfully against CUDA-enabled and CPU-only FFmpeg 6.1.5 libraries.
- The CPU-only build matches all 18 sports reference marks exactly.
- Hiding CUDA devices produces exit 1 and no marks, with no CPU fallback.
- Requesting CUDA from CPU-only FFmpeg also produces exit 1 and no marks.
- Invalid accelerators and CUDA with libmpeg2 are rejected with exit 2.
- Four regression-checker tests cover 50 fps frame fields, shifted endpoints
  inside comments, missing boundaries and malformed mark files.

Private broadcast recordings and images are not distributed with this fork.
