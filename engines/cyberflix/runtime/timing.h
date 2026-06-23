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

#ifndef CYBERFLIX_RUNTIME_TIMING_H
#define CYBERFLIX_RUNTIME_TIMING_H

#include "common/scummsys.h"

namespace Cyberflix {

class FramePacingRuntime {
public:
	int setFrameRate(int newRate);
	int getFrameRate() const { return _frameRate; }

	void beginIdle() { _idleForceUpdatePresented = false; }
	void noteForceUpdatePresented(bool presented) { _idleForceUpdatePresented = presented; }
	bool forceUpdatePresentedDuringIdle() const { return _idleForceUpdatePresented; }

	uint32 delayMillisUntilDeadline(int currentTick) const;
	void noteFrameTick(int tick) { _lastFrameTick = tick; }

private:
	int _frameRate = 3;       ///< DAT_00461126, scaled 60 Hz units between compositor passes.
	int _lastFrameTick = 0;   ///< DAT_00486788, last completed compositor tick.
	bool _idleForceUpdatePresented = false; ///< Current idle() already presented via forceupdate().
};

} // End of namespace Cyberflix

#endif
