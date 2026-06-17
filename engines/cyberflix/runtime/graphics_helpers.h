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
				pixels + (uint)(srcY + y) * width + srcX, copyWidth);
	}
}

inline void copyFrameToScreen(Graphics::Surface &screen, const FrameImage &frame,
		int dstX, int dstY) {
	copyFramePixelsToScreen(screen, frame.pixels.begin(), frame.width,
			frame.height, dstX, dstY);
}

} // End of namespace Cyberflix

#endif
