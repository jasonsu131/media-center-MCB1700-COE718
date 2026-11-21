# wav2c.py: turn a .wav file into song.h for the media center (USE_AUDIO 2)
# makes 8 bit mono samples at 8000 Hz, stored in flash
# usage: python wav2c.py song.wav [seconds] ["Song name"]
# export the wav from audacity (any rate, mono or stereo, 16 bit)
# jason su 501158090

import sys, wave, struct

RATE = 8000                 # samples per second on the board
MAX_S = 25                  # 25 s = 200 KB of flash

def main():
    if len(sys.argv) < 2:
        print(__doc__ if __doc__ else "usage: python wav2c.py song.wav [seconds] [name]")
        return
    src  = sys.argv[1]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else MAX_S
    name = sys.argv[3] if len(sys.argv) > 3 else src.rsplit("/", 1)[-1].rsplit("\\", 1)[-1].rsplit(".", 1)[0]

    w = wave.open(src, "rb")
    ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    w.close()

    # read every frame as one mono value from -1 to 1
    mono = []
    for i in range(n):
        tot = 0.0
        for c in range(ch):
            off = (i * ch + c) * width
            if width == 1:
                v = (raw[off] - 128) / 128.0
            elif width == 2:
                v = struct.unpack_from("<h", raw, off)[0] / 32768.0
            elif width == 3:
                v = int.from_bytes(raw[off:off + 3], "little", signed=True) / 8388608.0
            else:
                v = struct.unpack_from("<i", raw, off)[0] / 2147483648.0
            tot += v
        mono.append(tot / ch)

    # resample to 8000 Hz (average the input samples in each output step)
    out_n = min(int(len(mono) * RATE / rate), int(secs * RATE))
    step = rate / RATE
    out = []
    for i in range(out_n):
        a = int(i * step)
        b = max(a + 1, int((i + 1) * step))
        seg = mono[a:b]
        out.append(sum(seg) / len(seg))

    # normalise so the loudest part uses the full range
    peak = max(max(abs(v) for v in out), 1e-6)
    data = [min(255, max(0, int(128 + 127 * v / peak))) for v in out]

    with open("song.h", "w") as f:
        f.write("// song.h: made by tools/wav2c.py from %s\n" % src.replace("\\", "/").split("/")[-1])
        f.write("// 8 bit unsigned mono, %d Hz, %.1f s\n\n" % (RATE, out_n / RATE))
        f.write("#define SONG_RATE  %d\n" % RATE)
        f.write("#define SONG_LEN   %du\n" % out_n)
        f.write("#define SONG_NAME  \"%s\"\n\n" % name[:38].replace('"', ""))
        f.write("static const unsigned char song_data[SONG_LEN] = {\n")
        for i in range(0, out_n, 16):
            f.write("    " + ",".join("%d" % d for d in data[i:i + 16]) + ",\n")
        f.write("};\n")
    print("song.h: %d samples, %.1f s, %d KB of flash" % (out_n, out_n / RATE, out_n // 1024))

if __name__ == "__main__":
    main()
