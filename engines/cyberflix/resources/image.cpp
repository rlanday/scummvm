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

#include "common/intrinsics.h"
#include "common/stream.h"

#include "cyberflix/image.h"

namespace Cyberflix {

// Per-scanline RLE control opcodes (low two bits of each control byte).
enum {
	kCelCopyPrevRow = 0, ///< Copy from the previous scanline (vertical delta).
	kCelTransparent = 1, ///< Skip (leave transparent).
	kCelFill = 2,        ///< Run-length fill with the next stream byte.
	kCelLiteral = 3      ///< Copy literal pixel bytes from the stream.
};

// Decode one scanline of @p width pixels into @p row / @p rowOpaque starting at
// stream offset @p p, consuming @p byteLen control bytes. @p prevRow /
// @p prevOpaque are the previously decoded scanline (null on the first row).
// Returns false on a malformed line (over/underrun). Mirrors TI.EXE
// FUN_004194e0 as driven by the screen-space cel blitter FUN_0043b940, which
// feeds each scanline the just-written previous destination row as the op-0
// reference.
static bool decodeScanline(const byte *data, uint32 p, uint32 byteLen, uint16 width,
		byte *row, byte *rowOpaque, const byte *prevRow, const byte *prevOpaque) {
	const uint32 end = p + byteLen;
	uint16 x = 0;
	while (p < end && x < width) {
		const byte c = data[p++];
		uint16 n = static_cast<uint16>(c >> 2);
		if (n > static_cast<uint16>(width - x))
			n = static_cast<uint16>(width - x);
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
		case kCelCopyPrevRow:
			// The runtime copies these pixels from the previous destination
			// scanline. Where the previous cel row was opaque this duplicates
			// it; where it was transparent the original would copy whatever
			// background lay beneath, which we model as transparency.
			for (uint16 i = 0; i < n; ++i, ++x) {
				if (prevRow && prevOpaque[x]) {
					row[x] = prevRow[x];
					rowOpaque[x] = 1;
				}
			}
			break;
		case kCelTransparent:
		default:
			// Skip: leave the pixels transparent.
			x = static_cast<uint16>(x + n);
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
	const uint32 remain = static_cast<uint32>((stream.size() - stream.pos()));
	Common::Array<byte> data;
	data.resize(remain);
	if (remain && stream.read(data.begin(), remain) != remain)
		return false;

	out.width = width;
	out.height = height;
	out.originX = originX;
	out.originY = originY;
	out.pixels.resize(static_cast<uint>(width) * height);
	out.opaque.resize(static_cast<uint>(width) * height);
	for (uint i = 0; i < out.pixels.size(); ++i) {
		out.pixels[i] = 0;
		out.opaque[i] = 0;
	}

	uint32 p = 0;
	for (uint16 y = 0; y < height; ++y) {
		if (p + 2 > remain)
			return false;
		const uint16 byteLen = static_cast<uint16>((data[p] | (data[p + 1] << 8)));
		p += 2;
		if (p + byteLen > remain)
			return false;
		byte *row = &out.pixels[static_cast<uint>(y) * width];
		byte *rowOpaque = &out.opaque[static_cast<uint>(y) * width];
		const byte *prevRow = y ? row - width : nullptr;
		const byte *prevOpaque = y ? rowOpaque - width : nullptr;
		if (!decodeScanline(data.begin(), p, byteLen, width, row, rowOpaque, prevRow, prevOpaque))
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
						0, -4, -3, -2, -1, 1, 2, 3, 4 };
				copyFromRefRow(di, di + kCopyRef[k] * _w, contentWidth);
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
		const uint16 v = static_cast<uint16>((_src[_s] | (_src[_s + 1] << 8)));
		_s += 2;
		return v;
	}
	// A big-endian 16-bit word for the DPCM bit window.
	uint16 readWordBE() {
		const uint16 v = readWordLE();
		return static_cast<uint16>(((v << 8) | (v >> 8)));
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
		if (!destRange(di, n) || _s + static_cast<uint32>(n) > _srcSize) { _ok = false; return; }
		memcpy(_dst + di, _src + _s, n);
		_s += n;
	}

	void copyFromRefRow(int di, int ref, int n) {
		// Reference-row copies do not overlap for valid row-sized runs, so bypass
		// dcopy()'s overlap-preserving loop here; that path showed up as a SET
		// transition hotspot when decodeDeltaRow() copied unchanged spans.
		if (n > _w) {
			dcopy(di, ref, n);
			return;
		}
		if (!destRange(di, n) || !destRange(ref, n)) { _ok = false; return; }
		memcpy(_dst + di, _dst + ref, n);
	}

	// Copy @p n bytes within the destination, reproducing the original's
	// byte/word/dword granularity so self-overlapping runs match exactly.
	void dcopy(int d, int s, int n) {
		if (!destRange(d, n) || !destRange(s, n)) { _ok = false; return; }
		if (d + n <= s || s + n <= d) {
			memcpy(_dst + d, _dst + s, n);
			return;
		}
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
	//   - 8 <= cx <= 14       a signed residual of magnitude 15 - cx
	//                          with the sign taken from the next code bit; the
	//                          code is (mag + 2) bits long.
	// Returns the advanced destination offset.
	int dpcm(int pe, int di, int budget) {
		if (budget <= 0)
			return di;
		if (!destRange(di, budget) || !destRange(pe, budget)) {
			_ok = false;
			return di;
		}

		uint16 ax = readWordBE();   // current code window (MSB first)
		uint16 dxw = readWordBE();  // look-ahead bits feeding into ax
		int bits = 16;              // valid bits remaining in ax
		byte *dst = _dst;
		while (_ok) {
			int consume;            // number of code bits this symbol occupies
			if (ax & 0x8000) {
				// Most smooth gradients encode "same as predictor" as consecutive
				// one-bit symbols. Batch across bit-window reloads so long
				// predictor-copy spans become one libc copy/fill instead of many
				// tiny calls, while preserving the native reader's final backtrack.
				int run = 0;
				do {
					const uint16 inv = static_cast<uint16>(~ax);
					int chunk = inv ? 15 - Common::intLog2(inv) : 16;
					if (chunk > bits)
						chunk = bits;
					if (chunk > budget - run)
						chunk = budget - run;

					if (chunk == 16)
						ax = dxw;
					else
						ax = static_cast<uint16>(((ax << chunk) | (dxw >> (16 - chunk))));
					bits -= chunk;
					run += chunk;

					if (run == budget)
						break;
					if (bits != 0)
						dxw = static_cast<uint16>(dxw << chunk);
					else { dxw = readWordBE(); bits = 16; }
				} while (_ok && (ax & 0x8000));
				if (!_ok)
					break;

				if (pe + 1 == di)
					memset(dst + di, dst[pe], run);
				else
					memcpy(dst + di, dst + pe, run);
				di += run; pe += run; budget -= run;
				if (budget == 0) break;
				continue;
			}
			if (ax < 0x0100) {
				// Escape: predicted byte plus the low 8 bits of the window.
				dst[di] = static_cast<byte>(((ax + dst[pe]) & 0xff));
				++di; ++pe;
				consume = 16;
			} else {
				// Signed residual: magnitude by code length, sign from the bit just
				// below the leading one.
				const int cx = Common::intLog2(ax);
				const int mag = 15 - cx;
				const int sign = (ax >> (cx - 1)) & 1;
				dst[di] = static_cast<byte>(((dst[pe] + (sign ? mag : -mag)) & 0xff));
				++di; ++pe;
				consume = mag + 2;
			}

			--budget;
			// Advance the window by @c consume bits, pulling replacements from the
			// look-ahead word and reloading it from the stream as it runs dry.
			if (bits >= consume) {
				if (consume > 0)
					ax = static_cast<uint16>(((ax << consume) | (dxw >> (16 - consume))));
				bits -= consume;
				if (bits == 0) { dxw = readWordBE(); bits = 16; }
				else dxw = static_cast<uint16>(dxw << consume);
			} else {
				// The symbol straddles the look-ahead boundary: take the rest of
				// the current word, reload, then take the remaining @c rem bits.
				const int rem = consume - bits;
				if (bits > 0)
					ax = static_cast<uint16>(((ax << bits) | (dxw >> (16 - bits))));
				dxw = readWordBE();
				if (rem > 0)
					ax = static_cast<uint16>(((ax << rem) | (dxw >> (16 - rem))));
				dxw = static_cast<uint16>(dxw << rem);
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
			const int n = readRunLength(c);
			if (!_ok)
				return;
			switch (c & 7) {
			case 0: {                           // one literal byte, then DPCM (predict from left)
				edx -= n; ref += n;
				if (!destRange(di, 1) || _s >= _srcSize) { _ok = false; return; }
				_dst[di] = readByte(); ++di;
				di = dpcm(di - 1, di, n - 1);
				break;
			}
			case 4: {                           // repeat the previous byte
				edx -= n; ref += n;
				if (!destRange(di, n) || di < 1) { _ok = false; return; }
				const byte v = _dst[di - 1];
				memset(_dst + di, v, n);
				di += n;
				break;
			}
			case 2:                             // skip (leave as-is)
				edx -= n; ref += n; di += n;
				break;
			case 6: {                           // RLE fill from one stream byte
				edx -= n; ref += n;
				if (!destRange(di, n)) { _ok = false; return; }
				const byte v = readByte();
				memset(_dst + di, v, n);
				di += n;
				break;
			}
			case 1:                             // pure DPCM (predict from reference row)
				edx -= n;
				di = dpcm(ref, di, n);
				ref += n;
				break;
			case 5:                             // literal copy from the stream
				edx -= n; ref += n;
				copyFromSrc(di, n); di += n;
				break;
			case 3:                             // copy from the reference row
				edx -= n;
				copyFromRefRow(di, ref, n); di += n; ref += n;
				break;
			default: {                          // LZ back-reference within the row
				edx -= n; ref += n;
				const int dist = readWordLE();
				dcopy(di, di - dist, n); di += n;
				break;
			}
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

uint32 FrameSequence::applyFrame(const byte *src, uint32 srcSize) {
	if (srcSize < 4)
		return 0;
	const int height = src[0] | (src[1] << 8);
	const int width = src[2] | (src[3] << 8);
	if (height <= 0 || width <= 0 || width > 0x400 || height > 0x400)
		return 0;

	// The first frame fixes the dimensions and allocates the retained surface;
	// later frames must match and decode on top of the existing contents.
	if (empty()) {
		_width = static_cast<uint16>(width);
		_height = static_cast<uint16>(height);
		_pitch = width;
		// Value-initialize the retained surface and predictor padding rows once;
		// an extra memset here showed up as bzero during SET-transition profiles.
		_work.resize((height + 2 * kPad) * _pitch, 0);
	} else if (width != _width || height != _height) {
		return 0;
	}

	FrameDecoder dec(src, srcSize, _work.begin(), static_cast<int>(_work.size()), _pitch);
	dec.run(height, width, kPad * _pitch);
	if (!dec.ok())
		return 0;
	const uint32 consumed = dec.consumed();
	updateDepthMap(src, srcSize, consumed);
	return consumed;
}

const byte *FrameSequence::pixels() const {
	if (empty())
		return nullptr;
	return _work.begin() + kPad * _pitch;
}

bool FrameSequence::updateDepthMap(const byte *src, uint32 srcSize, uint32 consumed) {
	_depthMap.clear();
	if (empty() || consumed >= srcSize)
		return false;

	const byte *tail = src + consumed;
	const uint32 tailSize = srcSize - consumed;
	const uint32 tableSize = static_cast<uint32>(_height) * 2;
	if (tailSize < tableSize)
		return false;

	Common::Array<byte> depthMap;
	depthMap.resize(static_cast<uint>(_width) * _height);
	for (uint16 y = 0; y < _height; ++y) {
		const uint32 rowOffset = static_cast<uint32>(tail[y * 2] | (tail[y * 2 + 1] << 8));
		if (rowOffset < tableSize || rowOffset >= tailSize)
			return false;
		const uint32 count = tail[rowOffset];
		const uint32 rowEnd = rowOffset + 1 + count * 2;
		if (rowEnd > tailSize)
			return false;

		uint32 x = 0;
		const byte *run = tail + rowOffset + 1;
		for (uint32 i = 0; i < count; ++i, run += 2) {
			const uint32 length = run[0];
			const byte depth = run[1];
			if (length == 0 || x + length > _width)
				return false;
			memset(depthMap.begin() + static_cast<uint>(y) * _width + x, depth, length);
			x += length;
		}
		if (x != _width)
			return false;
	}

	_depthMap = depthMap;
	return true;
}

bool FrameSequence::depthVisibleAt(int x, int y, int depth) const {
	if (_depthMap.empty())
		return true;
	if (x < 0 || y < 0 || x >= _width || y >= _height)
		return false;
	return depth <= _depthMap[static_cast<uint>(y) * _width + x];
}

void FrameSequence::copyTo(FrameImage &out) const {
	out.width = _width;
	out.height = _height;
	out.pixels.resize(static_cast<uint>(_width) * _height);
	if (!empty())
		memcpy(out.pixels.begin(), pixels(), static_cast<uint>(_width) * _height);
}

uint32 decodeFrame(const byte *src, uint32 srcSize, FrameImage &out) {
	// A standalone frame is just a one-frame sequence (keyframe).
	FrameSequence seq;
	const uint32 consumed = seq.applyFrame(src, srcSize);
	if (consumed == 0)
		return 0;
	seq.copyTo(out);
	return consumed;
}

bool loadPalette(const byte *fileData, uint32 fileSize, Palette &rgb) {
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
			const uint16 value = static_cast<uint16>((fileData[b] | (fileData[b + 1] << 8)));
			if (value != k) {
				match = false;
				break;
			}
		}
		if (!match || o > limit)
			continue;

		for (uint32 k = 0; k < 256; ++k) {
			// Each ColorSpec entry is {uint16 value; uint16 R, G, B} little-
			// endian. The channels are 16-bit, but only the high byte carries
			// the 8-bit value we want, so we read the odd byte of each pair
			// (b+3/b+5/b+7). MOV cluts happen to replicate the value into both
			// bytes (e.g. 0xf1f1), but SET cluts leave the low byte zero
			// (e.g. 0x1900), so reading the low byte would render them black.
			const uint32 b = o + k * 8;
			rgb[k * 3 + 0] = fileData[b + 3]; // high byte of the R channel
			rgb[k * 3 + 1] = fileData[b + 5]; // high byte of the G channel
			rgb[k * 3 + 2] = fileData[b + 7]; // high byte of the B channel
		}
		// The runtime forces the palette's extreme indices (FUN_0041ba80).
		rgb[0] = rgb[1] = rgb[2] = 0;
		rgb[255 * 3 + 0] = rgb[255 * 3 + 1] = rgb[255 * 3 + 2] = 0xff;
		return true;
	}
	return false;
}

} // End of namespace Cyberflix
