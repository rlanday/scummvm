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

#ifndef DREAMFACTORY_GAME_SUPPORT_H
#define DREAMFACTORY_GAME_SUPPORT_H

#include "common/error.h"
#include "common/str.h"

namespace Common {
class FSNode;
}

namespace DreamFactory {

class DreamFactoryEngine;
class PathRuntime;
class Script;
class ScriptVM;

/** Static properties that differ between games using the DreamFactory runtime. */
struct GameProfile {
	const char *runtimeExecutable;
	const char *defaultSaveSignature;
};

/**
 * Per-game policy around the shared CyberFlix DreamFactory interpreter. File
 * formats, script execution and runtime systems stay outside this interface;
 * only installation and authored-data quirks belong here.
 * Currently only Titanic: Adventure Out of Time is supported, but support
 * for Dust: A Tale of the Wired West is anticipated soon.
 */
class GameSupport {
public:
	/** Allow the engine to destroy title-specific state through this interface. */
	virtual ~GameSupport() = default;

	/** Return the title's executable name and default save signature; no ownership transfer. */
	virtual const GameProfile &profile() const = 0;
	/**
	 * Locate startup data directories, register them with SearchMan (ScummVM's
	 * file-search service), and initialize the current disc name in @p paths.
	 * Titanic registers the Steam release's merged asset directory, or the
	 * extracted TITANIC1/DATA and TITANIC1/MOVIES directories, then initializes
	 * path(0) to "Titanic1:" and currentcd() to "Titanic1" in either case.
	 * Validate the installation and return kNoError on success, or an error
	 * that prevents engine startup when required game data is missing.
	 */
	virtual Common::Error initializePaths(PathRuntime &paths) const = 0;
	/**
	 * Apply a title-specific workaround to the parsed boot script in memory,
	 * before execution; never modify the game files. Return true if patched,
	 * false if no matching code was found. Titanic disables its original boot
	 * CD-check branch because ScummVM locates and validates the data itself.
	 */
	virtual bool patchBootScript(Script &script) const = 0;

	/**
	 * Normalize known disc-label spellings, e.g. "titanic1" to "Titanic1".
	 * Unknown labels pass through unchanged; this does not check availability.
	 */
	virtual Common::String canonicalDiscLabel(const Common::String &label) const = 0;
	/**
	 * Check whether the requested disc's data is available in the installation.
	 * On success, set @p mountedLabel to the name currentcd() should report;
	 * on failure, return false. This does not mount an OS volume or register
	 * search paths. A merged Steam installation can satisfy either Titanic CD.
	 */
	virtual bool resolveDisc(const Common::String &requested,
			Common::String &mountedLabel) const = 0;
	/**
	 * Map a script's colon-separated path (e.g. "Titanic1:Data") to an existing
	 * host directory, accounting for the title's supported installation layouts.
	 * Return true and set @p out when found; use @p out only on success.
	 * PathRuntime, not this method, registers the directory with SearchMan.
	 */
	virtual bool resolvePathDirectory(const Common::String &path,
			Common::FSNode &out) const = 0;

	/**
	 * Optional hook during forceUpdate(), after advancing animations and the
	 * script frame counter but before rendering. Titanic uses it to log the
	 * cargo-painting timer; the default does nothing.
	 */
	virtual void onForceUpdate(DreamFactoryEngine &) {}
	/**
	 * Optional hook after restoring saved script/runtime state. Titanic logs
	 * restored story variables and resets its timer diagnostics so they do not
	 * carry over from the previous state. The default does nothing.
	 */
	virtual void onGameStateLoaded(const ScriptVM &) {}

	/**
	 * Select assignments to trace in the VM debug log, given a lowercased
	 * variable name (local or global). Does not affect assignment behavior or
	 * enable logging by itself; the default selects no variables.
	 */
	virtual bool shouldLogScriptVariable(const Common::String &) const {
		return false;
	}
	/**
	 * Select calls to trace in the VM debug log, given the handler name and
	 * current self name. Does not affect handler lookup or enable logging by
	 * itself; the default selects no calls.
	 */
	virtual bool shouldLogScriptDispatch(const Common::String &,
			const Common::String &) const {
		return false;
	}
};

/**
 * Create the policy for a detected GType_* value. The caller owns the result;
 * DreamFactoryEngine keeps it in a ScopedPtr. Unsupported types call error()
 * rather than returning null.
 */
GameSupport *createGameSupport(int gameType);

} // End of namespace DreamFactory

#endif // DREAMFACTORY_GAME_SUPPORT_H
