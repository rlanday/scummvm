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

#ifndef DREAMFACTORY_RUNTIME_GRAPHICS_HELPERS_H
#define DREAMFACTORY_RUNTIME_GRAPHICS_HELPERS_H

#include "common/rect.h"

#include "graphics/surface.h"

#include "dreamfactory/image.h"

namespace DreamFactory {

inline void drawScaledCel(Graphics::Surface &screen, const CelImage &cel,
		const Common::Rect &dest, const Common::Rect &clip,
		const FrameSequence *depthFrame = nullptr, int depthBucket = 0) {
	const int destW = dest.width();
	const int destH = dest.height();
	if (destW <= 0 || destH <= 0 || cel.width == 0 || cel.height == 0)
		return;
	Common::Rect paint = dest;
	paint.clip(clip);
	// The clip rect can come from file-sourced camera data (the SET viewport),
	// so never trust it as a write bound on its own.
	paint.clip(Common::Rect(0, 0, screen.w, screen.h));
	if (paint.isEmpty())
		return;
	for (int y = paint.top; y < paint.bottom; ++y) {
		int srcY = static_cast<int>(static_cast<int64>(y - dest.top) * cel.height / destH);
		for (int x = paint.left; x < paint.right; ++x) {
			int srcX = static_cast<int>(static_cast<int64>(x - dest.left) * cel.width / destW);
			if (cel.isOpaque(srcX, srcY) &&
					(!depthFrame || depthFrame->depthVisibleAt(x, y, depthBucket)))
				*(reinterpret_cast<byte *>(screen.getBasePtr(x, y))) =
						cel.pixels[static_cast<uint>(srcY) * static_cast<uint>(cel.width) + srcX];
		}
	}
}

inline void drawCel(Graphics::Surface &screen, const CelImage &cel,
		const Common::Rect &dest, const Common::Rect &clip) {
	if (cel.width == 0 || cel.height == 0)
		return;

	Common::Rect paint = dest;
	paint.clip(clip);
	paint.clip(Common::Rect(0, 0, screen.w, screen.h));
	if (paint.isEmpty())
		return;

	const int celRight = static_cast<int>(dest.left) + cel.width;
	const int celBottom = static_cast<int>(dest.top) + cel.height;
	if (celRight <= paint.left || celBottom <= paint.top)
		return;
	if (paint.right > celRight)
		paint.right = static_cast<int16>(celRight);
	if (paint.bottom > celBottom)
		paint.bottom = static_cast<int16>(celBottom);

	const int copyWidth = paint.width();
	for (int y = paint.top; y < paint.bottom; ++y) {
		const int srcY = y - dest.top;
		const int srcX = paint.left - dest.left;
		const uint srcOffset = static_cast<uint>(srcY) * static_cast<uint>(cel.width) + srcX;
		const byte *src = &cel.pixels[srcOffset];
		const byte *opaque = &cel.opaque[srcOffset];
		byte *dst = reinterpret_cast<byte *>(screen.getBasePtr(paint.left, y));
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
		const size_t srcOffset = static_cast<size_t>(srcY + y) * static_cast<uint>(width) + srcX;
		memcpy(screen.getBasePtr(dstX, dstY + y),
				pixels + srcOffset, static_cast<size_t>(copyWidth));
	}
}

inline void copyFrameToScreen(Graphics::Surface &screen, const FrameImage &frame,
		int dstX, int dstY) {
	copyFramePixelsToScreen(screen, frame.pixels.begin(), frame.width,
			frame.height, dstX, dstY);
}

} // End of namespace DreamFactory

#endif
