/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/stream.h"

#include "cyberflix/image.h"

namespace Cyberflix {

// Per-scanline RLE control opcodes (low two bits of each control byte).
enum {
	kCelCopyRef = 0,    ///< Copy from the reference frame; transparent when absent.
	kCelTransparent = 1, ///< Skip (leave transparent).
	kCelFill = 2,       ///< Run-length fill with the next stream byte.
	kCelLiteral = 3     ///< Copy literal pixel bytes from the stream.
};

// Decode one scanline of @p width pixels into @p row / @p rowOpaque starting at
// stream offset @p p, consuming @p byteLen control bytes. Returns false on a
// malformed line (over/underrun). Mirrors TI.EXE FUN_00419210.
static bool decodeScanline(const byte *data, uint32 p, uint32 byteLen, uint16 width,
		byte *row, byte *rowOpaque) {
	const uint32 end = p + byteLen;
	uint16 x = 0;
	while (p < end && x < width) {
		const byte c = data[p++];
		uint16 n = (uint16)(c >> 2);
		if (n > (uint16)(width - x))
			n = (uint16)(width - x);
		switch (c & 3) {
		case kCelFill: {
			if (p >= end)
				return false;
			const byte v = data[p++];
			for (uint16 i = 0; i < n; ++i, ++x) {
				row[x] = v;
				rowOpaque[x] = 1;
			}
			break;
		}
		case kCelLiteral:
			if (p + n > end)
				return false;
			for (uint16 i = 0; i < n; ++i, ++x) {
				row[x] = data[p++];
				rowOpaque[x] = 1;
			}
			break;
		case kCelCopyRef:
		case kCelTransparent:
		default:
			// No reference frame for a standalone cel: advance, leaving the
			// pixels transparent (the runtime would copy the previous frame).
			x = (uint16)(x + n);
			break;
		}
	}
	// A well-formed scanline consumes exactly its byte length and fills the row.
	return p == end && x == width;
}

bool decodeCel(Common::SeekableReadStream &stream, uint16 width, uint16 height, CelImage &out) {
	if (width == 0 || height == 0 || width > 0x400)
		return false;

	const int16 originX = stream.readSint16LE();
	const int16 originY = stream.readSint16LE();

	// Slurp the remaining scanline data for random access.
	const uint32 remain = (uint32)(stream.size() - stream.pos());
	Common::Array<byte> data;
	data.resize(remain);
	if (remain && stream.read(data.begin(), remain) != remain)
		return false;

	out.width = width;
	out.height = height;
	out.originX = originX;
	out.originY = originY;
	out.pixels.resize((uint)width * height);
	out.opaque.resize((uint)width * height);
	for (uint i = 0; i < out.pixels.size(); ++i) {
		out.pixels[i] = 0;
		out.opaque[i] = 0;
	}

	uint32 p = 0;
	for (uint16 y = 0; y < height; ++y) {
		if (p + 2 > remain)
			return false;
		const uint16 byteLen = (uint16)(data[p] | (data[p + 1] << 8));
		p += 2;
		if (p + byteLen > remain)
			return false;
		byte *row = &out.pixels[(uint)y * width];
		byte *rowOpaque = &out.opaque[(uint)y * width];
		if (!decodeScanline(data.begin(), p, byteLen, width, row, rowOpaque))
			return false;
		p += byteLen;
	}
	return true;
}

// === Full-screen frame decompressor (TI.EXE FUN_00423600) ===
//
// A clean-room reimplementation of CyberFlix's hand-written assembly
// decompressor for full-screen frames (MOV video keyframes and SET room
// backgrounds). Validated byte-exact against the original routine on every
// keyframe of the boot movie. The codec decodes into a destination of pitch W;
// inter-row predictors reference rows up to four lines above or below the
// current one, so the working buffer carries padding rows of zero on each side.

// Magnitude table for the DPCM bit-mode, indexed by the high-bit position of
// the 16-bit code window (TI.EXE 0x00457b00). Equivalently the run of leading
// zeros that prefixes each residual.
static const byte kFrameMagTable[16] = { 8, 8, 8, 8, 8, 8, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0 };

// Index of the most significant set bit of @p v, or -1 if @p v is zero.
static int highestBit16(uint16 v) {
	for (int i = 15; i >= 0; --i)
		if (v & (1u << i))
			return i;
	return -1;
}

// Decoder state for a single frame. All pointers are byte offsets into @c _dst
// (the padded working surface) or @c _src (the compressed stream).
class FrameDecoder {
public:
	FrameDecoder(const byte *src, uint32 srcSize, byte *dst, int dstSize, int pitch) :
		_src(src), _srcSize(srcSize), _s(4), _dst(dst), _dstSize(dstSize),
		_w(pitch), _ok(true) {}

	bool ok() const { return _ok; }
	uint32 consumed() const { return _s; }

	// Decode @p height rows, each of @p contentWidth bytes, starting at the
	// destination offset @p base. The frame header has already been parsed, so
	// the stream cursor (@c _s) sits at the first row's control byte.
	//
	// Each row opens with a control byte whose opcode @c k = byte >> 2 selects
	// how the row is produced relative to its neighbours; after a row is written
	// the cursor advances by one full destination pitch (@c _w).
	void run(int height, int contentWidth, int base) {
		int di = base;
		for (int row = 0; row < height && _ok; ++row) {
			const int rowStart = di;
			const byte c0 = readByte();
			const int k = c0 >> 2;
			if (k == 1) {
				// Uncompressed row: the pixels follow verbatim in the stream.
				copyFromSrc(di, contentWidth);
				di = rowStart + _w;
			} else if (k >= 2 && k <= 9) {
				// Delta-coded against a reference row 1..4 lines above or below.
				static const int kDeltaRef[10] = { 0, 0, -4, -3, -2, -1, 1, 2, 3, 4 };
				decodeDeltaRow(di, rowStart + kDeltaRef[k] * _w, contentWidth);
				di = rowStart + _w;
			} else if (k == 10) {
				// Unchanged row: leave the destination as it stands.
				di = rowStart + _w;
			} else if (k >= 11 && k <= 18) {
				// Exact copy of a neighbouring row 1..4 lines above or below.
				static const int kCopyRef[19] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
						-4, -3, -2, -1, 1, 2, 3, 4 };
				dcopy(di, di + kCopyRef[k] * _w, contentWidth);
				di = rowStart + _w;
			} else {
				_ok = false;
			}
		}
	}

private:
	byte readByte() {
		if (_s >= _srcSize) { _ok = false; return 0; }
		return _src[_s++];
	}
	uint16 readWordLE() {
		if (_s + 2 > _srcSize) { _ok = false; return 0; }
		const uint16 v = (uint16)(_src[_s] | (_src[_s + 1] << 8));
		_s += 2;
		return v;
	}
	// A big-endian 16-bit word for the DPCM bit window.
	uint16 readWordBE() {
		const uint16 v = readWordLE();
		return (uint16)((v << 8) | (v >> 8));
	}
	// Run length: high five bits of @p c, or an extended byte + 0x20 when zero.
	int readRunLength(byte c) {
		int n = c >> 3;
		if (n == 0)
			n = readByte() + 0x20;
		return n;
	}

	bool destRange(int off, int len) const {
		return off >= 0 && len >= 0 && off + len <= _dstSize;
	}

	void copyFromSrc(int di, int n) {
		if (!destRange(di, n) || _s + (uint32)n > _srcSize) { _ok = false; return; }
		memcpy(_dst + di, _src + _s, n);
		_s += n;
	}

	// Copy @p n bytes within the destination, reproducing the original's
	// byte/word/dword granularity so self-overlapping runs match exactly.
	void dcopy(int d, int s, int n) {
		if (!destRange(d, n) || !destRange(s, n)) { _ok = false; return; }
		if (n & 1) { _dst[d] = _dst[s]; ++d; ++s; }
		if (n & 2) { _dst[d] = _dst[s]; _dst[d + 1] = _dst[s + 1]; d += 2; s += 2; }
		for (int k = n >> 2; k > 0; --k) {
			_dst[d] = _dst[s]; _dst[d + 1] = _dst[s + 1];
			_dst[d + 2] = _dst[s + 2]; _dst[d + 3] = _dst[s + 3];
			d += 4; s += 4;
		}
	}

	// DPCM bit-mode: emit @p budget pixels, each predicted from the byte at @p pe
	// (the pixel to the left, or the one directly above in the reference row),
	// then optionally adjusted by a signed residual decoded from a variable-length
	// big-endian bit code. This is the path that reproduces smooth gradients.
	//
	// The bit reader keeps a 16-bit code window @c ax that is consumed from the
	// top down; spent bits are refilled from a one-word look-ahead buffer @c dxw
	// (and from the stream when that empties). @c bits counts how many of @c ax's
	// bits are still valid. Each code is classified by the position @c cx of its
	// highest set bit (i.e. its run of leading zeros):
	//   - ax == 0 or cx < 8   escape: the residual is the window's low 8 bits and
	//                          the whole window (16 bits) is consumed;
	//   - cx == 15            the predictor is copied unchanged (1 bit consumed);
	//   - 8 <= cx <= 14       a residual of fixed magnitude kFrameMagTable[cx-1]
	//                          with the sign taken from the next code bit; the
	//                          code is (mag + 2) bits long.
	// Returns the advanced destination offset.
	int dpcm(int pe, int di, int budget) {
		uint16 ax = readWordBE();   // current code window (MSB first)
		uint16 dxw = readWordBE();  // look-ahead bits feeding into ax
		int bits = 16;              // valid bits remaining in ax
		while (_ok) {
			const int cx = highestBit16(ax);
			int consume;            // number of code bits this symbol occupies
			if (ax == 0 || cx < 8) {
				// Escape: predicted byte plus the low 8 bits of the window.
				if (!destRange(di, 1) || pe < 0 || pe >= _dstSize) { _ok = false; break; }
				_dst[di] = (byte)((ax + _dst[pe]) & 0xff);
				++di; ++pe;
				consume = 16;
			} else if (cx == 15) {
				// Copy the predictor verbatim; this symbol is a single set bit.
				if (!destRange(di, 1) || pe < 0 || pe >= _dstSize) { _ok = false; break; }
				_dst[di] = _dst[pe];
				++di; ++pe;
				// Shift that one bit out of the window and refill, decrementing the
				// pixel budget (this branch resolves the symbol on its own).
				ax = (uint16)((ax << 1) | (dxw >> 15));
				--bits; --budget;
				if (budget == 0) break;
				if (bits != 0)
					dxw = (uint16)(dxw << 1);
				else { dxw = readWordBE(); bits = 16; }
				continue;
			} else {
				// Signed residual: magnitude by code length, sign from the bit just
				// below the leading one.
				const int c = cx - 1;
				const int sign = (ax >> c) & 1;
				const int mag = kFrameMagTable[c];
				if (!destRange(di, 1) || pe < 0 || pe >= _dstSize) { _ok = false; break; }
				_dst[di] = _dst[pe];
				++di; ++pe;
				if (sign)
					_dst[di - 1] = (byte)((_dst[di - 1] + mag) & 0xff);
				else
					_dst[di - 1] = (byte)((_dst[di - 1] - mag) & 0xff);
				consume = mag + 2;
			}

			--budget;
			// Advance the window by @c consume bits, pulling replacements from the
			// look-ahead word and reloading it from the stream as it runs dry.
			if (bits >= consume) {
				if (consume > 0)
					ax = (uint16)((ax << consume) | (dxw >> (16 - consume)));
				bits -= consume;
				if (bits == 0) { dxw = readWordBE(); bits = 16; }
				else dxw = (uint16)(dxw << consume);
			} else {
				// The symbol straddles the look-ahead boundary: take the rest of
				// the current word, reload, then take the remaining @c rem bits.
				const int rem = consume - bits;
				if (bits > 0)
					ax = (uint16)((ax << bits) | (dxw >> (16 - bits)));
				dxw = readWordBE();
				if (rem > 0)
					ax = (uint16)((ax << rem) | (dxw >> (16 - rem)));
				dxw = (uint16)(dxw << rem);
				bits = 16 - rem;
			}
			if (budget == 0) break;
		}
		// The bit reader always fetches a whole look-ahead word; once the run ends,
		// hand back the source bytes whose bits were never consumed so the next
		// row's control byte is read from the right place.
		_s -= 2;
		if (bits >= 8) _s -= 1;
		if (bits >= 16) _s -= 1;
		return di;
	}

	// Reconstruct one row as a sequence of runs, each introduced by a command
	// byte. The run length @p n covers @p contentWidth bytes in total; @c edx
	// tracks how many row bytes remain, and @c ref walks the reference row in
	// lock-step with the output so vertical predictors stay column-aligned. The
	// low three bits of the command byte (b0,b1,b2) choose one of eight run modes.
	void decodeDeltaRow(int di, int ref, int contentWidth) {
		int edx = contentWidth;
		while (edx > 0 && _ok) {
			const byte c = readByte();
			const int b0 = c & 1, b1 = (c >> 1) & 1, b2 = (c >> 2) & 1;
			const int n = readRunLength(c);
			if (!_ok)
				return;
			if (!b0 && !b1 && !b2) {            // one literal byte, then DPCM (predict from left)
				edx -= n; ref += n;
				if (!destRange(di, 1) || _s >= _srcSize) { _ok = false; return; }
				_dst[di] = readByte(); ++di;
				di = dpcm(di - 1, di, n - 1);
			} else if (!b0 && !b1 && b2) {      // repeat the previous byte
				edx -= n; ref += n;
				if (!destRange(di, n) || di < 1) { _ok = false; return; }
				const byte v = _dst[di - 1];
				for (int i = 0; i < n; ++i)
					_dst[di++] = v;
			} else if (!b0 && b1 && !b2) {      // skip (leave as-is)
				edx -= n; ref += n; di += n;
			} else if (!b0 && b1 && b2) {       // RLE fill from one stream byte
				edx -= n; ref += n;
				if (!destRange(di, n)) { _ok = false; return; }
				const byte v = readByte();
				for (int i = 0; i < n; ++i)
					_dst[di++] = v;
			} else if (b0 && !b1 && !b2) {      // pure DPCM (predict from reference row)
				edx -= n;
				di = dpcm(ref, di, n);
				ref += n;
			} else if (b0 && !b1 && b2) {       // literal copy from the stream
				edx -= n; ref += n;
				copyFromSrc(di, n); di += n;
			} else if (b0 && b1 && !b2) {       // copy from the reference row
				edx -= n;
				dcopy(di, ref, n); di += n; ref += n;
			} else {                            // LZ back-reference within the row
				edx -= n; ref += n;
				const int dist = readWordLE();
				dcopy(di, di - dist, n); di += n;
			}
		}
	}

	const byte *_src;
	uint32 _srcSize;
	uint32 _s;
	byte *_dst;
	int _dstSize;
	int _w;
	bool _ok;
};

uint32 decodeFrame(const byte *src, uint32 srcSize, FrameImage &out) {
	if (srcSize < 4)
		return 0;
	const int height = src[0] | (src[1] << 8);
	const int width = src[2] | (src[3] << 8);
	if (height <= 0 || width <= 0 || width > 0x400 || height > 0x400)
		return 0;

	// Decode at a stride equal to the content width (full-screen frames fill the
	// surface), into a buffer padded by reference-row reach on each side.
	const int kPad = 8;
	const int pitch = width;
	const int rows = height + 2 * kPad;
	const int dstSize = rows * pitch;
	Common::Array<byte> work;
	work.resize(dstSize);
	memset(work.begin(), 0, dstSize);

	FrameDecoder dec(src, srcSize, work.begin(), dstSize, pitch);
	dec.run(height, width, kPad * pitch);
	if (!dec.ok())
		return 0;

	out.width = (uint16)width;
	out.height = (uint16)height;
	out.pixels.resize((uint)width * height);
	memcpy(out.pixels.begin(), work.begin() + kPad * pitch, (uint)width * height);
	return dec.consumed();
}

bool loadPalette(const byte *fileData, uint32 fileSize, byte *rgb) {
	if (fileSize < 8 + 256 * 8)
		return false;

	// The clut is embedded (not a top-level resource). Identify it by its
	// ColorSpec array: 256 eight-byte entries whose leading uint16 value field
	// counts 0, 1, 2, ... A 64-entry run is a reliable, cheap signature.
	const uint32 limit = fileSize - 256 * 8;
	for (uint32 o = 0; o + 64 * 8 <= fileSize; o += 2) {
		bool match = true;
		for (uint32 k = 0; k < 64; ++k) {
			const uint32 b = o + k * 8;
			const uint16 value = (uint16)(fileData[b] | (fileData[b + 1] << 8));
			if (value != k) {
				match = false;
				break;
			}
		}
		if (!match || o > limit)
			continue;

		for (uint32 k = 0; k < 256; ++k) {
			const uint32 b = o + k * 8;
			rgb[k * 3 + 0] = fileData[b + 2]; // high byte of the R channel
			rgb[k * 3 + 1] = fileData[b + 4]; // high byte of the G channel
			rgb[k * 3 + 2] = fileData[b + 6]; // high byte of the B channel
		}
		// The runtime forces the palette's extreme indices (FUN_0041ba80).
		rgb[0] = rgb[1] = rgb[2] = 0;
		rgb[255 * 3 + 0] = rgb[255 * 3 + 1] = rgb[255 * 3 + 2] = 0xff;
		return true;
	}
	return false;
}

} // End of namespace Cyberflix
