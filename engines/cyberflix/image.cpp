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
