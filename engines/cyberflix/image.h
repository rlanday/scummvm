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
 * encoding, reversed from the runtime blitters (TI.EXE FUN_0043b940 for
 * screen-space items, FUN_0043bd60 for scaled world items) and the
 * per-scanline RLE decoders (FUN_004194e0 forward / FUN_00419620 mirrored,
 * compose-buffer variants FUN_00419210/FUN_00419310).
 *
 * On disk a cel is:
 *   uint16 height, uint16 width   (packed into the resource @c info field for
 *                                  shapes; width = info >> 16, height = info & 0xffff)
 *   int16  originX, originY       (hotspot/placement offset; payload bytes 0..3)
 *   scanline[height]              (payload bytes 4..)
 *
 * Each scanline is @c {uint16 byteLength; controlStream}. The control stream is
 * a run of command bytes @c C with @c n = C >> 2 and @c op = C & 3:
 *   op 0  copy @c n pixels from the previous scanline (vertical delta; the
 *         blitter passes the just-written previous destination row as the
 *         reference, so where the cel itself is transparent above this run
 *         the copy resolves to the underlying background = transparency)
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
		return opaque[static_cast<uint>(y) * width + x] != 0;
	}
};

/**
 * CYBERFLIX MOV VIDEO FILE FORMAT (.MOV)
 * =====================================
 * Despite the extension these are NOT QuickTime; a .MOV is a CyberFlix
 * "Bicycle"-engine LPPALPPA container (see Archive) whose resources hold the
 * video frames, the soundtrack and a palette. Reversed and validated end-to-end
 * against MOVIES/LOGO.MOV (the "CyberFlix Incorporated presents" logo).
 *
 * Layout (all framing little-endian; see Archive for the container header and
 * the resource directory at file offset 0x400):
 *
 *   - VIDEO FRAMES are the resources whose @c info tag is @c kFrameInfoTag
 *     (0x02000108). The tag is not just a type id: its four little-endian bytes
 *     ARE the frame header @c {uint16 H; uint16 P} (0x02000108 -> H=0x0108=264,
 *     P=0x0200=512). The compressed stream consumed by the decoder therefore
 *     begins at the @c info field and runs through the payload, i.e. at
 *     @c (recordOffset + 8) == @c (dataOffset - 4) for @c (payloadLength + 4)
 *     bytes. All frames of a movie share one (H, P).
 *
 *   - AUDIO resources carry @c info tag 0x6 and are interleaved one per video
 *     frame (decoding is handled elsewhere; the video path ignores them).
 *
 *   - PALETTE is a single embedded Macintosh 'clut' resource shared by every
 *     frame; loadPalette() locates it by its sequential ColorSpec value run.
 *
 * Frames are INTER-CODED. Only the first frame of a run is a full keyframe;
 * later frames update just the regions that changed and rely on the retained
 * contents of the previous frame for the rest (row opcode K==10 and the per-row
 * skip mode leave pixels untouched). Playback therefore composites frames in
 * order into a persistent surface (FrameSequence); decoding a frame standalone
 * into a fresh buffer (decodeFrame) is correct only for keyframes such as SET
 * room backgrounds.
 */

/** Resource @c info tag identifying a MOV video frame; doubles as its H/P header. */
static const uint32 kFrameInfoTag = 0x02000108;

/**
 * High half of a full-screen frame's @c info word (the low half is the height).
 * 0x02000108 is a 512x264 video frame; 0x02000180 is a 512x384 menu frame. Both
 * decode with the same FrameSequence, so detect frames by this high half.
 */
static const uint32 kFrameInfoHigh = 0x0200;

/**
 * A decoded full-screen frame: an 8-bit palettised image, @c width * @c height
 * pixels packed at a stride equal to @c width.
 *
 * These are produced by the CyberFlix full-screen frame decompressor (TI.EXE
 * FUN_00423600), a hand-written assembly routine shared by MOV video frames and
 * SET room backgrounds. Unlike a cel it is always fully opaque (every pixel is
 * written), so there is no transparency mask.
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
 * A persistent full-screen framebuffer that decodes a CyberFlix movie one
 * frame at a time.
 *
 * MOV video is inter-coded: only the first frame of a run is a full keyframe;
 * every later frame updates just the regions that changed, relying on the
 * retained contents of the previous frame for everything else (the row opcode
 * K == 10 and the per-row skip mode deliberately leave pixels untouched).
 * Decoding such a frame into a fresh, zeroed buffer therefore leaves the
 * unchanged areas as palette index 0 instead of the prior image. This class
 * keeps the working surface alive across calls so successive frames composite
 * correctly.
 */
class FrameSequence {
public:
	/**
	 * Apply one frame's control stream on top of the retained framebuffer.
	 *
	 * The first applied frame establishes the dimensions and should be a
	 * keyframe; later frames must share those dimensions. The frame format is
	 * the one documented on @ref decodeFrame.
	 *
	 * @param src      Frame data; must begin at the @c H header word.
	 * @param srcSize  Bytes available at @p src.
	 * @return bytes consumed from @p src, or 0 on malformed input. On failure
	 *         the retained framebuffer is left unchanged.
	 */
	uint32 applyFrame(const byte *src, uint32 srcSize);

	uint16 width() const { return _width; }
	uint16 height() const { return _height; }
	bool empty() const { return _width == 0 || _height == 0; }
	void clear() {
		_work.clear();
		_pitch = 0;
		_width = _height = 0;
	}

	/** Tightly packed @c width*height palette indices of the current frame. */
	const byte *pixels() const;

	/** Copy the current frame out as a standalone @ref FrameImage. */
	void copyTo(FrameImage &out) const;

private:
	// Reference rows reach up to four lines past the frame, so the working
	// surface is padded by this many rows top and bottom.
	static const int kPad = 8;

	Common::Array<byte> _work; ///< Padded persistent surface (stride == width).
	int _pitch = 0;
	uint16 _width = 0;
	uint16 _height = 0;
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
 * @c {uint16 value; uint16 R, G, B}; the 8-bit channel is the high byte of each
 * 16-bit pair. MOV cluts replicate the value into both bytes (e.g. 0xf1f1) so
 * the byte order is irrelevant there, but SET cluts leave the low byte zero
 * (e.g. 0x1900), so we must read the high byte to avoid rendering them black.
 * The runtime apply routine (FUN_0041ba80) forces index 0 to black and 255 to
 * white, which we reproduce here.
 *
 * Caveat for MOV video: that index 0/255 forcing is a SET-background era
 * assumption. It is invisible for the LOGO.MOV intro only because no frame in
 * that movie references index 0 or 255 as a pixel value (the black background
 * is index 229; the clut itself even maps 0->white, 255->black). The forcing
 * should be revisited when MOV playback is wired into the engine proper, in
 * case other movies do use those indices.
 *
 * @return true if a clut was found (identified by its sequential value field).
 */
bool loadPalette(const byte *fileData, uint32 fileSize, byte *rgb);

} // End of namespace Cyberflix

#endif // CYBERFLIX_IMAGE_H
