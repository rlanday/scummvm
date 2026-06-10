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

#ifndef CYBERFLIX_IMAGE_H
#define CYBERFLIX_IMAGE_H

#include "common/scummsys.h"
#include "common/array.h"

namespace Common {
class SeekableReadStream;
}

namespace Cyberflix {

/**
 * A decoded CyberFlix "cel": an 8-bit palettised image with per-pixel
 * transparency. SHP shapes and SET background frames share the same cel
 * encoding, reversed from the runtime blitter (TI.EXE FUN_0043bd60) and the
 * per-scanline RLE decoders (FUN_00419210 forward / FUN_00419310 mirrored).
 *
 * On disk a cel is:
 *   uint16 height, uint16 width   (packed into the resource @c info field for
 *                                  shapes; width = info >> 16, height = info & 0xffff)
 *   int16  originX, originY       (hotspot/placement offset; payload bytes 0..3)
 *   scanline[height]              (payload bytes 4..)
 *
 * Each scanline is @c {uint16 byteLength; controlStream}. The control stream is
 * a run of command bytes @c C with @c n = C >> 2 and @c op = C & 3:
 *   op 0  copy @c n pixels from the reference frame (transparent when none)
 *   op 1  skip  @c n pixels (transparent)
 *   op 2  fill  @c n pixels with the next stream byte (RLE)
 *   op 3  copy  @c n literal pixel bytes from the stream
 */
struct CelImage {
	uint16 width = 0;
	uint16 height = 0;
	int16 originX = 0;
	int16 originY = 0;
	Common::Array<byte> pixels;  ///< width*height palette indices (0 where transparent).
	Common::Array<byte> opaque;  ///< width*height; non-zero where a pixel was written.

	bool isOpaque(int x, int y) const {
		return opaque[(uint)y * width + x] != 0;
	}
};

/**
 * A decoded full-screen frame: an 8-bit palettised image, @c width * @c height
 * pixels packed at a stride equal to @c width.
 *
 * These are produced by the CyberFlix full-screen intra-frame decompressor
 * (TI.EXE FUN_00423600), a hand-written assembly routine shared by MOV video
 * keyframes and SET room backgrounds. Unlike a cel it is always fully opaque
 * (every pixel is written), so there is no transparency mask.
 */
struct FrameImage {
	uint16 width = 0;   ///< P: image bytes per row.
	uint16 height = 0;  ///< H: number of rows.
	Common::Array<byte> pixels; ///< width*height palette indices.
};

/**
 * Decode a full-screen frame produced by TI.EXE FUN_00423600.
 *
 * The source is @c {uint16 H; uint16 P; controlStream}. Each of the @c H rows
 * is introduced by a control byte @c C0 whose row opcode is @c K = C0 >> 2:
 *   - K == 1            a literal (uncompressed) row of @c P bytes;
 *   - K in  2..9        a row delta-coded against a reference row @c (di + dW),
 *                       d in {-4,-3,-2,-1,+1,+2,+3,+4}, via the per-row LZ;
 *   - K == 10           a skipped row (left unchanged);
 *   - K in 11..18       a verbatim copy of a neighbouring row @c (di + dW).
 * The per-row LZ selects one of eight inner modes from the low three bits of
 * each command byte (run length @c n = byte >> 3, or @c next + 0x20 when zero):
 * literal+DPCM, repeat-previous-byte, skip, RLE fill, pure DPCM, literal copy,
 * reference-row copy and an LZ back-reference. The DPCM mode applies signed
 * residuals from a big-endian bit stream, with magnitudes from a fixed table.
 *
 * @param src      Frame data; must begin at the @c H header word.
 * @param srcSize  Bytes available at @p src.
 * @param out      Receives the decoded @c H * @c P image.
 * @return bytes consumed from @p src, or 0 on malformed input.
 */
uint32 decodeFrame(const byte *src, uint32 srcSize, FrameImage &out);

/**
 * Decode a shape/cel resource stream into @p out.
 *
 * @param stream  Resource payload (positioned at originX). Fully consumed.
 * @param width   Cel width  (resource @c info >> 16 for shapes).
 * @param height  Cel height (resource @c info & 0xffff for shapes).
 * @return true on a clean decode (every scanline filled exactly @p width pixels).
 */
bool decodeCel(Common::SeekableReadStream &stream, uint16 width, uint16 height, CelImage &out);

/**
 * Locate and parse the embedded Macintosh 'clut' palette in a CyberFlix
 * container and expand it to @p rgb (256 * 3 bytes, R,G,B order).
 *
 * The palette is stored as an 8-byte header followed by 256 ColorSpec entries
 * @c {uint16 value; uint16 R, G, B}; the 8-bit channel is the high byte. The
 * runtime apply routine (FUN_0041ba80) forces index 0 to black and 255 to
 * white, which we reproduce here.
 *
 * @return true if a clut was found (identified by its sequential value field).
 */
bool loadPalette(const byte *fileData, uint32 fileSize, byte *rgb);

} // End of namespace Cyberflix

#endif // CYBERFLIX_IMAGE_H
