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

static const int8 kSignedNibble[16] = {
	 0,  1,  2,  3,  4,  5,  6,  7,
	-8, -7, -6, -5, -4, -3, -2, -1
};

static inline void emitCbxSample(byte *dst, uint32 outSize, uint32 &produced,
		byte sample, bool duplicate) {
	if (produced >= outSize)
		return;
	dst[produced++] = sample;
	if (duplicate && produced < outSize)
		dst[produced++] = sample;
}

// Decode one cbx block to 8-bit PCM. The 22050 Hz path emits one sample; the
// 11025 Hz path duplicates each sample to the mixer rate. Write literals/DPCM
// samples directly and reserve memset() for actual repeat runs; the old generic
// per-sample loop made the compiler emit tiny memset() calls in the hot path.
static void decodeCbxBlock(const byte *src, uint32 srcLen, uint32 outSize,
		bool duplicate, byte *dst) {
	if (srcLen == 0) {
		memset(dst, 0x80, outSize);
		return;
	}

	uint32 i = 0, produced = 0;
	byte b = src[i++];
	emitCbxSample(dst, outSize, produced, (byte)(b * 2), duplicate);
	int pred = b;
	while (produced < outSize && i < srcLen) {
		b = src[i++];
		if (!(b & 0x80)) { // literal
			emitCbxSample(dst, outSize, produced, (byte)(b * 2), duplicate);
			pred = b;
		} else if (!(b & 0x40)) { // 4-bit DPCM delta-run
			uint32 n = (b & 0x3f) + 1;
			for (uint32 j = 0; j < n && i < srcLen && produced < outSize; ++j) {
				const byte d = src[i++];
				const int s1 = (pred + kSignedNibble[d >> 4]) & 0xff;
				emitCbxSample(dst, outSize, produced, (byte)(s1 * 2), duplicate);
				pred = (s1 + kSignedNibble[d & 0xf]) & 0xff;
				emitCbxSample(dst, outSize, produced, (byte)(pred * 2), duplicate);
			}
		} else { // predictor repeat-run
			uint32 n = (b & 0x3f) + 1;
			if (duplicate)
				n *= 2;
			if (n > outSize - produced)
				n = outSize - produced;
			memset(dst + produced, (byte)(pred * 2), n);
			produced += n;
		}
	}
	if (produced < outSize) // pad a short/truncated block with silence
		memset(dst + produced, 0x80, outSize - produced);
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
	const bool duplicate = rate != kAudioRate22050;
	if (duplicate && blockBytes > 0x7fffffffU)
		return 0;
	const uint32 outSize = blockBytes * (duplicate ? 2 : 1);
	const uint32 startBytes = out.size();

	uint32 validBlocks = 0;
	for (; validBlocks < count; ++validBlocks) {
		const uint32 tableOff = 0x28 + validBlocks * 4;
		if (tableOff + 4 > payloadLen)
			break;
		const uint32 off = READ_LE_UINT32(payload + tableOff);
		if (off < 4 || off - 4 >= payloadLen)
			break;
	}
	if (validBlocks == 0 || outSize > (0xffffffffU - startBytes) / validBlocks)
		return 0;

	out.resize(startBytes + outSize * validBlocks);
	for (uint32 b = 0; b < validBlocks; ++b) {
		const uint32 tableOff = 0x28 + b * 4;
		const uint32 off = READ_LE_UINT32(payload + tableOff);
		// Block offsets are relative to base B (payload-4); >= 4 keeps the
		// in-payload start non-negative.
		const uint32 start = off - 4;
		decodeCbxBlock(payload + start, payloadLen - start, outSize, duplicate,
				out.begin() + startBytes + b * outSize);
	}
	return out.size() - startBytes;
}

} // End of namespace Cyberflix
