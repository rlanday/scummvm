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

#include "common/endian.h"

#include "cyberflix/sound.h"

namespace Cyberflix {

// Decode one cbx block to @p outSize bytes of 8-bit PCM, appending to @p out.
// @p dup is 1 for the 22050 Hz path, 2 for the 11025 Hz path (each decoded
// sample is emitted @p dup times). Bounds are clamped so malformed input cannot
// overrun; a short block is padded with silence (0x80). Mirrors FUN_00430480 /
// FUN_00430590.
static void decodeCbxBlock(const byte *src, uint32 srcLen, uint32 outSize, int dup,
		Common::Array<byte> &out) {
	if (srcLen == 0) {
		for (uint32 k = 0; k < outSize; ++k)
			out.push_back(0x80);
		return;
	}
	uint32 i = 0, produced = 0;
	byte b = src[i++];
	for (int k = 0; k < dup && produced < outSize; ++k, ++produced)
		out.push_back((byte)(b * 2));
	int pred = b;
	while (produced < outSize && i < srcLen) {
		b = src[i++];
		if (!(b & 0x80)) { // literal
			for (int k = 0; k < dup && produced < outSize; ++k, ++produced)
				out.push_back((byte)(b * 2));
			pred = b;
		} else if (!(b & 0x40)) { // 4-bit DPCM delta-run
			int n = (b & 0x3f) + 1;
			for (int j = 0; j < n && i < srcLen && produced < outSize; ++j) {
				const byte d = src[i++];
				int hi = d >> 4;   if (hi >= 8) hi -= 16; // signed high nibble
				int lo = d & 0xf;  if (lo >= 8) lo -= 16; // signed low nibble
				int s1 = (pred + hi) & 0xff;
				for (int k = 0; k < dup && produced < outSize; ++k, ++produced)
					out.push_back((byte)(s1 * 2));
				pred = (s1 + lo) & 0xff;
				for (int k = 0; k < dup && produced < outSize; ++k, ++produced)
					out.push_back((byte)(pred * 2));
			}
		} else { // predictor repeat-run
			int n = ((b & 0x3f) + 1) * dup;
			for (int j = 0; j < n && produced < outSize; ++j, ++produced)
				out.push_back((byte)(pred * 2));
		}
	}
	while (produced < outSize) { // pad a short/truncated block with silence
		out.push_back(0x80);
		++produced;
	}
}

uint32 decodeCbxAudio(const byte *payload, uint32 payloadLen, Common::Array<byte> &out) {
	// Header fields are relative to the decoder base B == payload - 4, so e.g.
	// the rate at B+0x1c is payload+0x18. Read them directly off payload.
	if (payloadLen < 0x2c)
		return 0;
	const uint32 rate = READ_LE_UINT32(payload + 0x18);
	const uint32 blockBytes = READ_LE_UINT32(payload + 0x1c);
	const uint32 count = READ_LE_UINT32(payload + 0x24);
	if (blockBytes == 0 || count == 0 || count > 0x10000)
		return 0;
	const int dup = (rate == kAudioRate22050) ? 1 : 2;
	const uint32 outSize = blockBytes * dup;
	const uint32 startBytes = out.size();
	for (uint32 b = 0; b < count; ++b) {
		const uint32 tableOff = 0x28 + b * 4;
		if (tableOff + 4 > payloadLen)
			break;
		const uint32 off = READ_LE_UINT32(payload + tableOff);
		// Block offsets are relative to base B (payload-4); >= 4 keeps the
		// in-payload start non-negative.
		if (off < 4 || off - 4 >= payloadLen)
			break;
		const uint32 start = off - 4;
		decodeCbxBlock(payload + start, payloadLen - start, outSize, dup, out);
	}
	return out.size() - startBytes;
}

} // End of namespace Cyberflix
