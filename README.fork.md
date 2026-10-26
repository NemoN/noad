# noad: logo-return boundaries and optional NVDEC decoding

This fork starts from madmartin/noad commit
`54f6b3d5c79e4c87884a92a3154a3c63ee8ed7bc` (noad 0.8.10).
The upstream GPL license remains in effect.

## Programme boundaries

The default preserves the logo-return boundaries instead of moving them to a
black frame up to 45 seconds earlier. A transition between two adverts can be
black too, so that heuristic can leave the end of an advert block unmarked.
On the sports regression recording it moved six of eight ends backwards by
9.6–35.4 seconds. The default now avoids those six moves automatically.

`--legacy-blackframe-refinement` explicitly restores the historical behaviour
for comparisons. The guard covers the normal and overlap paths. Other cleanup,
including aspect-ratio analysis, remains enabled. Logo recognition itself can
still misclassify programme footage; this is not a guarantee of perfect cuts.
The recommended Tvheadend profile does not enable `--overlap` and converts only
internal gaps between VDR keep marks into EDL commercial breaks, preserving
recording beginnings and endings.

## Optional GPU decoding

`--hwaccel=none` is the default. The **experimental** option
`--hwaccel=cuda --decoder=ffmpeg` requests
CUDA/NVDEC via FFmpeg's hardware-context API. The FFmpeg libraries must have
NVDEC support compiled in and the NVIDIA driver must be usable by the caller.
The GPU path requires libavcodec 58 or newer. No CUDA device or missing codec
support is an error; it never silently turns into a CPU benchmark.

The existing noad logo, black-bar, and overlap algorithms still execute on the
CPU. GPU frames are downloaded and converted from formats such as NV12 to the
planar YUV420P required by noad's callbacks and GOP cache. Transfer and format
conversion time are included in end-to-end measurements. A decoder summary on
stderr records hardware-frame count, seeks, and transfer/conversion time.
Hardware and software decoder output can differ enough to change logo
recognition. Compare marks as well as timing before using CUDA on recordings.
The tested Quadro P600 is not recommended as the default for this workload;
see the recording-level results below.

The FFmpeg send/receive loop also drains buffered frames before reading new
input and retains packets until accepted. Decoder initialization failures now
stop analysis and produce a nonzero exit status. Frame/packet and AVIO buffers
are released explicitly. The read-only AVIO callback and dictionary
initialization fixes allow building against FFmpeg 6.1.

## Build on Linux

Install autotools, a C++ compiler, pkg-config, and development libraries for
libavformat, libavcodec, libavutil, and libswscale. For a custom static FFmpeg
installation, set `PKG_CONFIG_PATH` and include its private link dependencies:

```sh
./autogen.sh
export PKG_CONFIG_PATH=/path/to/ffmpeg/lib/pkgconfig
export LIBS="$(pkg-config --static --libs libavformat libavcodec libavutil libswscale)"
./configure --without-libmpeg2 --without-magick CXXFLAGS='-O2 -std=gnu++11'
make -j2
```

Hardware acceleration is selected at runtime; the same binary can run CPU and
GPU trials. A CPU-only FFmpeg build remains usable in `--hwaccel=none` mode.

## Validation

```sh
python3 -m unittest discover -s tests -v
python3 tools/check_marks.py tests/fixtures/sports-25fps.marks /path/to/marks.noad --fps 25
python3 tools/check_marks.py /path/to/cpu.marks /path/to/cuda.marks --fps 50
```

The fixture records timestamps only; the private broadcast video is not
distributed. A real regression run must analyze that recording and compare
the actual output against the fixture. Parser tests alone do not test video
recognition. Performance measurements and recording-level comparisons are
documented in `docs/validation.md`.

The hardware-context implementation follows the API described in FFmpeg's
[hardware decode example](https://ffmpeg.org/doxygen/6.1/hw_decode_8c-example.html).
