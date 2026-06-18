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

#ifndef CYBERFLIX_RUNTIME_GRAPHICS_HELPERS_H
#define CYBERFLIX_RUNTIME_GRAPHICS_HELPERS_H

#include "graphics/surface.h"

#include "cyberflix/image.h"

namespace Cyberflix {

inline void drawScaledCel(Graphics::Surface *screen, const CelImage &cel,
		const Common::Rect &dest, const Common::Rect &clip) {
	const int destW = dest.width();
	const int destH = dest.height();
	if (destW <= 0 || destH <= 0 || cel.width <= 0 || cel.height <= 0)
		return;
	Common::Rect paint = dest;
	paint.clip(clip);
	if (paint.isEmpty())
		return;
	for (int y = paint.top; y < paint.bottom; ++y) {
		int srcY = static_cast<int>(static_cast<int64>(y - dest.top) * cel.height / destH);
		for (int x = paint.left; x < paint.right; ++x) {
			int srcX = static_cast<int>(static_cast<int64>(x - dest.left) * cel.width / destW);
			if (cel.isOpaque(srcX, srcY))
				*(reinterpret_cast<byte *>(screen->getBasePtr(x, y))) =
						cel.pixels[static_cast<uint>(srcY) * cel.width + srcX];
		}
	}
}

inline void drawCel(Graphics::Surface *screen, const CelImage &cel,
		const Common::Rect &dest, const Common::Rect &clip) {
	Common::Rect paint = dest;
	paint.clip(clip);
	paint.clip(Common::Rect(dest.left, dest.top, dest.left + cel.width, dest.top + cel.height));
	if (paint.isEmpty() || cel.width == 0 || cel.height == 0)
		return;

	const int copyWidth = paint.width();
	for (int y = paint.top; y < paint.bottom; ++y) {
		const int srcY = y - dest.top;
		const int srcX = paint.left - dest.left;
		const byte *src = cel.pixels.begin() + static_cast<uint>(srcY) * cel.width + srcX;
		const byte *opaque = cel.opaque.begin() + static_cast<uint>(srcY) * cel.width + srcX;
		byte *dst = reinterpret_cast<byte *>(screen->getBasePtr(paint.left, y));
		for (int x = 0; x < copyWidth; ++x) {
			if (opaque[x])
				dst[x] = src[x];
		}
	}
}

inline void copyFramePixelsToScreen(Graphics::Surface &screen, const byte *pixels,
		int width, int height, int dstX, int dstY) {
	if (!pixels || width <= 0 || height <= 0)
		return;

	int srcX = 0;
	int srcY = 0;
	int copyWidth = width;
	int copyHeight = height;
	if (dstX < 0) {
		srcX = -dstX;
		copyWidth -= srcX;
		dstX = 0;
	}
	if (dstY < 0) {
		srcY = -dstY;
		copyHeight -= srcY;
		dstY = 0;
	}
	if (dstX + copyWidth > screen.w)
		copyWidth = screen.w - dstX;
	if (dstY + copyHeight > screen.h)
		copyHeight = screen.h - dstY;
	if (copyWidth <= 0 || copyHeight <= 0)
		return;

	// Frame backgrounds are fully opaque. Clip once, then copy whole rows; this
	// avoids the per-pixel bounds checks in the SET transition hot path.
	for (int y = 0; y < copyHeight; ++y) {
		memcpy(screen.getBasePtr(dstX, dstY + y),
				pixels + static_cast<uint>(srcY + y) * width + srcX, copyWidth);
	}
}

inline void copyFrameToScreen(Graphics::Surface &screen, const FrameImage &frame,
		int dstX, int dstY) {
	copyFramePixelsToScreen(screen, frame.pixels.begin(), frame.width,
			frame.height, dstX, dstY);
}

} // End of namespace Cyberflix

#endif
