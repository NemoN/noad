# noad: preserve logo-return boundaries

This fork starts from madmartin/noad commit
`54f6b3d5c79e4c87884a92a3154a3c63ee8ed7bc` (noad 0.8.10).
The upstream GPL license remains in effect.

## Programme boundaries

The default preserves logo-return boundaries instead of moving them to a
black frame up to 45 seconds earlier. A transition between two adverts can
be black too, leaving the end of a commercial block unmarked. On the sports
regression recording this caused six ends to move backwards by 9.6–35.4 seconds.
The default avoids those six moves automatically.

`--legacy-blackframe-refinement` explicitly restores the historical behaviour
for comparisons. The guard covers normal and overlap paths. Other cleanup,
including aspect-ratio analysis, remains enabled. Logo recognition can still
misclassify programme footage; this is not a guarantee of perfect cuts.
The recommended Tvheadend profile does not enable `--overlap` and converts only
internal gaps between VDR keep marks into EDL commercial breaks, preserving
recording beginnings and endings.

## CPU decoding

The fork uses FFmpeg software decoding. GPU support, its command-line option,
and the libswscale dependency have been removed. The historical experiment is
archived under `docs/history/`.

The FFmpeg send/receive loop drains buffered frames before reading new input
and retains packets until accepted. Initialization failures stop analysis with
a nonzero exit status. Frame, packet and AVIO buffers are released explicitly.
The read-only AVIO callback and dictionary initialization fixes support FFmpeg 6.1.

## Build on Linux

Install autotools, a C++ compiler, pkg-config, and development libraries for
libavformat, libavcodec and libavutil. For a custom static FFmpeg installation:

```sh
./autogen.sh
export PKG_CONFIG_PATH=/path/to/ffmpeg/lib/pkgconfig
export LIBS="$(pkg-config --static --libs libavformat libavcodec libavutil)"
./configure --without-libmpeg2 --without-magick CXXFLAGS='-O2 -std=gnu++11'
make -j2
```

No NVIDIA headers or GPU-specific libraries are required. Unknown options,
including the removed `--hwaccel` option, are rejected before recording analysis.

## Validation

```sh
python3 -m unittest discover -s tests -v
python3 tools/check_marks.py tests/fixtures/sports-25fps.marks /path/to/marks.noad --fps 25
```

The fixture records timestamps only; the private video is not distributed.
A real regression run must analyze that recording and compare actual output
against the fixture. Parser tests alone do not test video recognition.
Recording-level validation is documented in `docs/validation.md`.
