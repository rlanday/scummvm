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

#include "common/debug.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/runtime/loops.h"

namespace Cyberflix {

LoopRuntime::ScheduledLoop::Kind LoopRuntime::scheduledLoopKind(
		const Common::String &kind) {
	if (kind.equalsIgnoreCase("scene"))
		return ScheduledLoop::kScene;
	if (kind.equalsIgnoreCase("flat"))
		return ScheduledLoop::kFlat;
	if (kind.equalsIgnoreCase("stage"))
		return ScheduledLoop::kStage;
	if (kind.equalsIgnoreCase("prop"))
		return ScheduledLoop::kProp;
	if (kind.equalsIgnoreCase("shop"))
		return ScheduledLoop::kShop;
	if (kind.equalsIgnoreCase("actor"))
		return ScheduledLoop::kActor;
	return ScheduledLoop::kUnknown;
}

void LoopRuntime::makeLoop(const Common::String &kind, const Common::String &target,
		const Common::String &message, int delay) {
	if (kind.empty() || message.empty()) {
		warning("Cyberflix: makeloop('%s', '%s', '%s', %d): invalid arguments",
				kind.c_str(), target.c_str(), message.c_str(), delay);
		return;
	}

	stopLoop(kind, target);

	ScheduledLoop loop;
	loop.kindId = scheduledLoopKind(kind);
	loop.kind = kind;
	loop.kind.toLowercase();
	loop.target = target;
	loop.message = message;
	loop.remainingPasses = delay;
	loop.createdPass = _scheduledLoopPass;
	_scheduledLoops.push_back(loop);
	debug(2, "Cyberflix: makeloop('%s', '%s', '%s', %d)",
			kind.c_str(), target.c_str(), message.c_str(), delay);
}

void LoopRuntime::stopLoop(const Common::String &kind, const Common::String &target) {
	if (kind.empty())
		return;
	const bool all = kind.equalsIgnoreCase("all");
	const ScheduledLoop::Kind kindId = scheduledLoopKind(kind);
	for (int i = static_cast<int>(_scheduledLoops.size()) - 1; i >= 0; --i) {
		const ScheduledLoop &loop = _scheduledLoops[static_cast<uint32>(i)];
		const bool kindMatches = kindId == ScheduledLoop::kUnknown
				? loop.kind.equalsIgnoreCase(kind)
				: loop.kindId == kindId;
		if (all || (kindMatches &&
				(target.empty() || loop.target.equalsIgnoreCase(target))))
			_scheduledLoops.remove_at(static_cast<uint32>(i));
	}
}

void LoopRuntime::pauseLoop(const Common::String &kind, bool paused) {
	if (kind.equalsIgnoreCase("all"))
		_loopsPaused = paused;
}

void LoopRuntime::makeCricket(CyberflixEngine &engine, const Common::String &name) {
	if (name.empty())
		return;
	for (uint i = 0; i < _crickets.size(); ++i) {
		if (_crickets[i].name.equalsIgnoreCase(name)) {
			_crickets[i].paused = _cricketsPaused;
			engine.playSound(name, 1);
			return;
		}
	}
	CricketState cricket;
	cricket.name = name;
	cricket.paused = _cricketsPaused;
	_crickets.push_back(cricket);
	engine.playSound(name, 1);
}

void LoopRuntime::stopCricket(const Common::String &name) {
	if (name.equalsIgnoreCase("all")) {
		_crickets.clear();
		return;
	}
	for (int i = static_cast<int>(_crickets.size()) - 1; i >= 0; --i) {
		if (_crickets[static_cast<uint>(i)].name.equalsIgnoreCase(name))
			_crickets.remove_at(static_cast<uint>(i));
	}
	debug(2, "Cyberflix: stopcricket('%s')", name.c_str());
}

void LoopRuntime::pauseCricket(const Common::String &kind, bool paused) {
	if (kind.equalsIgnoreCase("all")) {
		_cricketsPaused = paused;
		for (uint i = 0; i < _crickets.size(); ++i)
			_crickets[i].paused = paused;
	} else {
		for (uint i = 0; i < _crickets.size(); ++i)
			if (_crickets[i].name.equalsIgnoreCase(kind))
				_crickets[i].paused = paused;
	}
	debug(2, "Cyberflix: pausecricket('%s', %d)", kind.c_str(), paused ? 1 : 0);
}

void LoopRuntime::processScheduledLoops(CyberflixEngine &engine) {
	if (_loopsPaused || _scheduledLoops.empty())
		return;

	// Native forceupdate() (TI.EXE FUN_00423a60 -> FUN_00423ff0 per loop record)
	// re-scans its scheduled-loop table on every pass, and marks the record it is
	// about to dispatch INACTIVE before dispatching it. So when a callback itself
	// calls forceupdate(), the nested pass still fires every OTHER due loop -- only
	// the record currently being dispatched is skipped (FUN_00423a60 tests
	// `*psVar6 != 0`; FUN_00423ff0 sets `*param_4 = 0` before the dispatch).
	//
	// A single global "already processing" guard here would instead starve every
	// other callback for the whole duration of a script's forceupdate() wait loop.
	// That is what deadlocked the Willie fencing turn: willieattack()'s
	// `for count = 0 to 8 { forceupdate() }` lunge loop (and its follow-on
	// sendtoflat/sendtostage handlers, which loop on forceupdate() too) blocked
	// the scheduled callback that hands the turn back to the player, so neither
	// side could act.
	//
	// Mirror native: allow re-entrant passes, remove the loop before dispatching
	// it (so a nested pass skips it), and re-scan after each dispatch since the
	// dispatch may add/remove loops. `processedPass` stops a loop being
	// decremented twice within one pass; a nested pass gets its own id and
	// decrements independently, matching native's per-forceupdate table walk.
	const bool wasProcessing = _processingScheduledLoops;
	_processingScheduledLoops = true;
	const uint32 pass = ++_scheduledLoopPass;
	Common::Array<Value> noArgs;

	bool rescan = true;
	while (rescan) {
		rescan = false;
		for (uint32 i = 0; i < _scheduledLoops.size(); ++i) {
			ScheduledLoop &sl = _scheduledLoops[i];
			if (sl.createdPass >= pass || sl.processedPass == pass)
				continue;
			sl.processedPass = pass;
			if (--sl.remainingPasses > 0)
				continue;

			ScheduledLoop loop = _scheduledLoops[i];
			_scheduledLoops.remove_at(i);
			switch (loop.kindId) {
			case ScheduledLoop::kScene:
				engine.sendToScene(loop.target, loop.message, noArgs);
				break;
			case ScheduledLoop::kFlat:
				engine.sendToFlat(loop.target, loop.message, noArgs);
				break;
			case ScheduledLoop::kStage:
				engine.sendToStage(loop.message, noArgs);
				break;
			case ScheduledLoop::kProp:
				engine.sendToProp(loop.target, loop.message, noArgs);
				break;
			case ScheduledLoop::kShop:
				engine.sendToShop(loop.target, loop.message, noArgs);
				break;
			case ScheduledLoop::kActor:
				engine.sendToActor(loop.target, loop.message, noArgs);
				break;
			default:
				debug(1, "Cyberflix: makeloop kind '%s' unhandled", loop.kind.c_str());
			}
			// The dispatch may have re-entered this routine and/or mutated the
			// array (makeloop/stoploop); restart the scan from the top.
			rescan = true;
			break;
		}
	}

	_processingScheduledLoops = wasProcessing;
}

} // End of namespace Cyberflix
