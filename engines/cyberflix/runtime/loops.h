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

#ifndef CYBERFLIX_RUNTIME_LOOPS_H
#define CYBERFLIX_RUNTIME_LOOPS_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Cyberflix {

class CyberflixEngine;

class LoopRuntime {
public:
	struct ScheduledLoop {
		enum Kind {
			kUnknown,
			kScene,
			kFlat,
			kStage,
			kProp,
			kShop,
			kActor
		};
		Kind kindId = kUnknown;
		Common::String kind;
		Common::String target;
		Common::String message;
		int32 remainingPasses = 0;
		uint32 createdPass = 0;
	};

	struct CricketState {
		Common::String name;
		bool paused = false;
	};

	static ScheduledLoop::Kind scheduledLoopKind(const Common::String &kind);

	void makeLoop(const Common::String &kind, const Common::String &target,
			const Common::String &message, int delay);
	void stopLoop(const Common::String &kind, const Common::String &target);
	void pauseLoop(const Common::String &kind, bool paused);
	void makeCricket(CyberflixEngine &engine, const Common::String &name);
	void stopCricket(const Common::String &name);
	void pauseCricket(const Common::String &kind, bool paused);
	void processScheduledLoops(CyberflixEngine &engine);

	void clear() {
		_scheduledLoops.clear();
		_loopsPaused = false;
		_processingScheduledLoops = false;
		_scheduledLoopPass = 0;
		_crickets.clear();
		_cricketsPaused = false;
	}
	void setLoopsPaused(bool paused) { _loopsPaused = paused; }
	void setCricketsPaused(bool paused) { _cricketsPaused = paused; }
	void restoreLoop(const ScheduledLoop &loop) { _scheduledLoops.push_back(loop); }
	void restoreCricket(const CricketState &cricket) { _crickets.push_back(cricket); }

	bool loopsPaused() const { return _loopsPaused; }
	bool processingScheduledLoops() const { return _processingScheduledLoops; }
	bool cricketsPaused() const { return _cricketsPaused; }
	const Common::Array<ScheduledLoop> &scheduledLoops() const { return _scheduledLoops; }
	const Common::Array<CricketState> &crickets() const { return _crickets; }

private:
	Common::Array<ScheduledLoop> _scheduledLoops;
	bool _loopsPaused = false;
	bool _processingScheduledLoops = false;
	uint32 _scheduledLoopPass = 0;

	Common::Array<CricketState> _crickets;
	bool _cricketsPaused = false;
};

} // End of namespace Cyberflix

#endif
