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

#ifndef DREAMFACTORY_RUNTIME_MOVIE_H
#define DREAMFACTORY_RUNTIME_MOVIE_H

#include "common/array.h"
#include "common/str.h"

#include "audio/mixer.h"

namespace DreamFactory {

class DreamFactoryEngine;

class MovieRuntime {
public:
	void playMovie(DreamFactoryEngine &engine, const Common::String &name);

private:
	/** Blit one clipped band of a decoded frame (TI.EXE FUN_00410660). */
	static void blitMovieBand(DreamFactoryEngine &engine, const byte *pixels, int w, int h,
			int x0, int y0, int left, int top, int right, int bottom);

	/**
	 * Reveal a decoded frame with one of the geometric draw-op transitions
	 * (TI.EXE FUN_0040eef0's non-blit cases), one band per 60 Hz tick.
	 * Members rather than free functions so they inherit the engine friendship
	 * that reaching _system/_eventMan requires.
	 */
	static void runMovieTransition(DreamFactoryEngine &engine, uint16 op, const byte *pixels,
			int w, int h, int x0, int y0, int steps);

	/** True while any frame cue is still sounding (TI.EXE FUN_0042fcc0). */
	static bool cueStillPlaying(DreamFactoryEngine &engine,
			const Common::Array<Audio::SoundHandle> &handles);
};

} // End of namespace DreamFactory

#endif
