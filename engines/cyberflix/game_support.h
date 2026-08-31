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

#ifndef CYBERFLIX_GAME_SUPPORT_H
#define CYBERFLIX_GAME_SUPPORT_H

#include "common/error.h"
#include "common/str.h"

namespace Common {
class FSNode;
}

namespace CyberFlix {

class CyberFlixEngine;
class PathRuntime;
class Script;
class ScriptVM;

/** Static properties that differ between games using the Bicycle runtime. */
struct GameProfile {
	const char *runtimeExecutable;
	const char *defaultSaveSignature;
};

/**
 * Per-game policy around the shared CyberFlix/Bicycle interpreter. File
 * formats, script execution and runtime systems stay outside this interface;
 * only installation and authored-data quirks belong here.
 */
class GameSupport {
public:
	virtual ~GameSupport() = default;

	virtual const GameProfile &profile() const = 0;
	virtual Common::Error initializePaths(PathRuntime &paths) const = 0;
	virtual bool patchBootScript(Script &script) const = 0;

	virtual Common::String canonicalDiscLabel(const Common::String &label) const = 0;
	virtual bool resolveDisc(const Common::String &requested,
			Common::String &mountedLabel) const = 0;
	virtual bool resolvePathDirectory(const Common::String &path,
			Common::FSNode &out) const = 0;

	virtual void onForceUpdate(CyberFlixEngine &) {}
	virtual void onGameStateLoaded(const ScriptVM &) {}

	virtual bool shouldLogScriptVariable(const Common::String &) const {
		return false;
	}
	virtual bool shouldLogScriptDispatch(const Common::String &,
			const Common::String &) const {
		return false;
	}
};

GameSupport *createGameSupport(int gameType);

} // End of namespace CyberFlix

#endif // CYBERFLIX_GAME_SUPPORT_H
