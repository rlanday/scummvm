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
