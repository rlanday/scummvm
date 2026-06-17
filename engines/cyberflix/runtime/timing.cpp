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

#include "common/util.h"

#include "cyberflix/runtime/timing.h"

namespace Cyberflix {

int FramePacingRuntime::frameRate(const int *newRate) {
	if (newRate)
		_frameRate = CLIP(*newRate, 0, 60);
	return _frameRate;
}

uint32 FramePacingRuntime::delayMillisUntilDeadline(int currentTick) const {
	if (_frameRate <= 0)
		return 0;

	const int remainingTicks = _lastFrameTick + _frameRate - currentTick;
	if (remainingTicks <= 0)
		return 0;

	uint32 delay = (uint32)((remainingTicks * 1000 + 59) / 60);
	if (delay > 17)
		delay = 17;
	return delay;
}

} // End of namespace Cyberflix
