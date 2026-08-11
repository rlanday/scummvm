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

#ifndef CYBERFLIX_RUNTIME_MOVIE_H
#define CYBERFLIX_RUNTIME_MOVIE_H

#include "common/str.h"

namespace Cyberflix {

class CyberflixEngine;

class MovieRuntime {
public:
	void playMovie(CyberflixEngine &engine, const Common::String &name);

	/**
	 * Diagnostic: decode @p name's frames and write each to a PPM in @p dir.
	 *
	 * Frames are emitted in coded order (the order their video resources
	 * appear in the container), which for a single-segment movie is the order
	 * the player displays them. Delta frames only decode correctly as a
	 * sequence, so the whole chain is applied even though every frame is
	 * written out. Driven by --dump-movie; not part of normal playback.
	 */
	bool dumpMovieFrames(CyberflixEngine &engine, const Common::String &name,
			const Common::String &dir);

private:
	/** Blit one clipped band of a decoded frame (TI.EXE FUN_00410660). */
	static void blitMovieBand(CyberflixEngine &engine, const byte *pixels, int w, int h,
			int x0, int y0, int left, int top, int right, int bottom);

	/**
	 * Reveal a decoded frame with one of the geometric draw-op transitions
	 * (TI.EXE FUN_0040eef0's non-blit cases), one band per 60 Hz tick.
	 * Members rather than free functions so they inherit the engine friendship
	 * that reaching _system/_eventMan requires.
	 */
	static void runMovieTransition(CyberflixEngine &engine, uint16 op, const byte *pixels,
			int w, int h, int x0, int y0, int steps);
};

} // End of namespace Cyberflix

#endif
