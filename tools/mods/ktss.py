"""DOAX3 Scarlet's sound banks (romfs 00/sound/**/*.xhs): a G1L container of KTSS streams (Opus).

G1L ("_L1G0000"): u32 file size, u32 header size (0x18 + 4 * count), u32 0x10, u32 count, u32 offsets[count] (from
the file's start); the streams follow, back to back.

KTSS (one voice line; offsets from the stream's start; little-endian):
    0x00 "KTSS"   0x04 u32 stream size (header included)
    0x20 u8 codec (9: Opus)   0x22 u8 3, 0x23 u8 3   0x24 u32 data offset - 0x20 (0x50)
    0x28 u8 layers (1)   0x29 u8 channels   0x2c u32 sample rate (48000)   0x30 u32 samples (after the pre-skip)
    0x34 u32 loop start   0x38 u32 loop length (0: no loop)
    0x40 u32 data offset (0x70)   0x44 u32 data size   0x4c u32 frames
    0x50 u16 frame size in bytes (8 + Opus packet)   0x52 u16 samples per frame (960)   0x54 u32 sample rate
    0x58 u16 pre-skip (120: libopus's restricted low delay mode)   0x5a u16 1
    0x70 frames: u32 big-endian packet size, u32 big-endian encoder final range, the Opus packet (constant size: CBR)
The voice banks are mono, 48 kHz, 960-sample frames of 240 bytes (96 kbit/s). The game decodes them with the SDK's
own libopus (nn::codec::OpusMultiStreamDecoder, recompiled), so any valid Opus packets in this framing play.
"""
import ctypes
import os
import shutil
import struct
import subprocess
import wave

MSYS_BIN = r'C:\msys64\ucrt64\bin'
OPUS_APPLICATION_RESTRICTED_LOWDELAY = 2051
OPUS_SET_BITRATE = 4002
OPUS_SET_VBR = 4006
OPUS_GET_FINAL_RANGE = 4031
OPUS_GET_LOOKAHEAD = 4027


def _tool_dir():
    ff = shutil.which('ffmpeg')
    return os.path.dirname(ff) if ff else MSYS_BIN


_opus = None


def opus():
    """libopus (MSYS2's libopus-0.dll, or libopus on the PATH)."""
    global _opus
    if _opus is None:
        dirs = [_tool_dir(), MSYS_BIN]  # (MSYS2 too: the ffmpeg on the PATH can be a static build, without any DLLs)
        for d in dirs:
            if hasattr(os, 'add_dll_directory') and os.path.isdir(d):
                os.add_dll_directory(d)
        for name in [os.path.join(d, 'libopus-0.dll') for d in dirs] + ['libopus-0.dll', 'libopus.so.0', 'libopus.dylib']:
            try:
                _opus = ctypes.CDLL(name)
                break
            except OSError:
                continue
        else:
            raise SystemExit('libopus not found: install MSYS2 UCRT64 ffmpeg (pacman -S mingw-w64-ucrt-x86_64-ffmpeg)')
        _opus.opus_encoder_create.restype = ctypes.c_void_p
        _opus.opus_decoder_create.restype = ctypes.c_void_p
        _opus.opus_encode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int16), ctypes.c_int,
                                      ctypes.c_char_p, ctypes.c_int32]
        _opus.opus_decode.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32,
                                      ctypes.POINTER(ctypes.c_int16), ctypes.c_int, ctypes.c_int]
    return _opus


# ------------------------------------------------------------------ G1L
def g1l_read(data):
    """[KTSS bytes] of a sound bank."""
    magic, size, hsz, x10, n = struct.unpack_from('<8sIIII', data, 0)
    assert magic == b'_L1G0000', 'not a G1L sound bank'
    offs = list(struct.unpack_from(f'<{n}I', data, 0x18)) + [size]
    return [data[offs[i]:offs[i + 1]] for i in range(n)]


def g1l_write(streams, x10=0x10):
    hsz = 0x18 + 4 * len(streams)
    offs, o = [], hsz
    for s in streams:
        offs.append(o)
        o += len(s)
    head = struct.pack('<8sIIII', b'_L1G0000', o, hsz, x10, len(streams)) + struct.pack(f'<{len(streams)}I', *offs)
    return head + b''.join(streams)


# ------------------------------------------------------------------ KTSS
class Ktss:
    def __init__(self, data):
        assert data[:4] == b'KTSS', 'not a KTSS stream'
        self.head = bytearray(data[:0x70])
        self.codec = data[0x20]
        assert self.codec == 9, f'codec {self.codec}: only Opus (9) streams are handled'
        self.channels = data[0x29]
        self.rate, self.samples = struct.unpack_from('<II', data, 0x2c)
        self.data_off, self.data_size = struct.unpack_from('<II', data, 0x40)
        self.frames = struct.unpack_from('<I', data, 0x4c)[0]
        self.frame_bytes, self.frame_samples = struct.unpack_from('<HH', data, 0x50)
        self.skip = struct.unpack_from('<H', data, 0x58)[0]
        self.raw = data

    def packets(self):
        o = self.data_off
        for _ in range(self.frames):
            n = struct.unpack_from('>I', self.raw, o)[0]
            yield self.raw[o + 8:o + 8 + n]
            o += 8 + n

    def decode(self):
        """16-bit PCM (interleaved), the pre-skip removed, `samples` long."""
        lib = opus()
        err = ctypes.c_int()
        dec = lib.opus_decoder_create(self.rate, self.channels, ctypes.byref(err))
        assert not err.value, err.value
        out = bytearray()
        pcm = (ctypes.c_int16 * (self.frame_samples * self.channels))()
        for p in self.packets():
            n = lib.opus_decode(dec, p, len(p), pcm, self.frame_samples, 0)
            assert n > 0, f'opus_decode: {n}'
            out += bytes(pcm)[:n * 2 * self.channels]
        lib.opus_decoder_destroy(ctypes.c_void_p(dec))
        b = 2 * self.channels
        return bytes(out[self.skip * b:(self.skip + self.samples) * b])


def encode_like(template, pcm):
    """A KTSS stream of pcm (16-bit, template's rate and channels) with the template stream's settings: its header,
    frame size and packet size (constant bitrate), libopus in restricted low delay mode (the template's pre-skip)."""
    lib = opus()
    t = Ktss(template)
    ch, fs = t.channels, t.frame_samples
    packet = t.frame_bytes - 8
    err = ctypes.c_int()
    enc = lib.opus_encoder_create(t.rate, ch, OPUS_APPLICATION_RESTRICTED_LOWDELAY, ctypes.byref(err))
    assert not err.value, err.value
    lib.opus_encoder_ctl(ctypes.c_void_p(enc), OPUS_SET_BITRATE, ctypes.c_int32(packet * 8 * t.rate // fs))
    lib.opus_encoder_ctl(ctypes.c_void_p(enc), OPUS_SET_VBR, ctypes.c_int32(0))
    look = ctypes.c_int32()
    lib.opus_encoder_ctl(ctypes.c_void_p(enc), OPUS_GET_LOOKAHEAD, ctypes.byref(look))
    skip = look.value
    samples = len(pcm) // (2 * ch)
    total = samples + skip  # (the encoder's delay: decoded output starts skip samples late)
    frames = -(-total // fs)
    pcm = pcm + b'\0' * ((frames * fs - samples) * 2 * ch)
    body = bytearray()
    buf = ctypes.create_string_buffer(packet)
    rng = ctypes.c_uint32()
    for i in range(frames):
        chunk = pcm[i * fs * 2 * ch:(i + 1) * fs * 2 * ch]
        arr = (ctypes.c_int16 * (fs * ch)).from_buffer_copy(chunk)
        n = lib.opus_encode(ctypes.c_void_p(enc), arr, fs, buf, packet)
        assert n > 0, f'opus_encode: {n}'
        lib.opus_encoder_ctl(ctypes.c_void_p(enc), OPUS_GET_FINAL_RANGE, ctypes.byref(rng))
        if n < packet:  # (hard CBR gives exactly `packet` bytes; pad a short one with Opus padding just in case)
            n2 = lib.opus_packet_pad(buf, n, packet)
            assert n2 == 0, f'opus_packet_pad: {n2}'
            n = packet
        body += struct.pack('>II', n, rng.value) + buf.raw[:n]
    lib.opus_encoder_destroy(ctypes.c_void_p(enc))
    h = bytearray(t.head)
    data_off = struct.unpack_from('<I', h, 0x40)[0]
    struct.pack_into('<I', h, 0x04, data_off + len(body))
    struct.pack_into('<I', h, 0x30, samples)
    struct.pack_into('<II', h, 0x34, 0, 0)  # (no loop)
    struct.pack_into('<I', h, 0x44, len(body))
    struct.pack_into('<I', h, 0x4c, frames)
    struct.pack_into('<H', h, 0x58, skip)
    return bytes(h[:data_off]) + bytes(body)


# ------------------------------------------------------------------ audio files
def load_audio(path, rate, channels):
    """Any audio file ffmpeg reads, as 16-bit PCM at rate / channels (a plain WAV of that format needs no ffmpeg)."""
    try:
        with wave.open(path, 'rb') as w:
            if w.getframerate() == rate and w.getnchannels() == channels and w.getsampwidth() == 2:
                return w.readframes(w.getnframes())
    except (wave.Error, EOFError):
        pass
    ff = shutil.which('ffmpeg') or os.path.join(MSYS_BIN, 'ffmpeg.exe')
    r = subprocess.run([ff, '-v', 'error', '-i', path, '-f', 's16le', '-acodec', 'pcm_s16le', '-ac', str(channels),
                        '-ar', str(rate), '-'], capture_output=True)
    if r.returncode:
        raise SystemExit(f'{path}: ffmpeg could not read it: {r.stderr.decode(errors="replace").strip()}')
    return r.stdout


def save_wav(path, pcm, rate, channels):
    with wave.open(path, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
