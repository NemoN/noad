# CPU-only validation

GPU support was removed after the experiment at commit
`c7516cfdb253aa569857deca49f21aa89092aec6`. The CUDA decoder path, hardware-frame
transfer/conversion, GPU options and libswscale build dependency are gone.
Historical measurements are retained in
[`history/gpu-experiment-2026-10-26.md`](history/gpu-experiment-2026-10-26.md).

The CPU decoder's buffering, initialization error handling and memory cleanup
remain in place. The logo-return boundary correction is unchanged.

## Recognition regression

The sports reference has 18 VDR keep marks representing eight internal
commercial breaks. The old black-frame heuristic moved six ends backwards by
9.6–35.4 seconds, placing them inside adverts. The corrected default retains
the logo-return positions. All 16 internal boundaries were inspected using
96 decoded stills, bounding the transitions to about a second.

The earlier six-recording CPU comparison also confirmed one magazine endpoint
restored from 31:56.16 to 32:38.88. Images at 32:05 and 32:20 still show trailers;
the programme returns around 32:39. All other internal CPU boundaries on those
six recordings remained unchanged.

The CPU-only removal is checked by building against software-only FFmpeg 6.1.5,
analyzing the complete sports recording, and comparing all 18 actual marks
against `tests/fixtures/sports-25fps.marks`. Removed GPU options must return
exit 2 before processing a recording.

Four checker tests cover 50 fps frame fields, shifted endpoints inside comments,
missing boundaries and malformed mark files. These tests validate the checker;
the full recording run validates the compiled detector.

Logo-free intros can still be false positives and breaks can still be missed.
This validates the specific black-frame failure, not every advert in every
recording. Private broadcast videos and images are not distributed in the fork.
