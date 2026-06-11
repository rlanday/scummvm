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

#ifndef CYBERFLIX_CYBERFLIX_H
#define CYBERFLIX_CYBERFLIX_H

#include "common/random.h"
#include "common/error.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/ptr.h"

#include "engines/engine.h"

#include "cyberflix/detection.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

class Console;
class Script;
class Stage;
class Set;
}

namespace Common {
class PEResources;
struct Event;
}

namespace Audio {
class SoundHandle;
}

namespace Graphics {
struct WinCursorGroup;
}

namespace Cyberflix {

// The game renders into a 512x384, 8-bit palettised framebuffer (the menu and
// in-game node images are full 512x384; the LOGO movie's frames are 512x264 and
// sit letterboxed within it).
enum {
	kScreenWidth = 512,
	kScreenHeight = 384
};

class CyberflixEngine : public Engine, public VMHost {
public:
	CyberflixEngine(OSystem *syst, const CyberflixGameDescription *gameDesc);
	~CyberflixEngine() override;

	Common::Error run() override;

	bool hasFeature(EngineFeature f) const override;

	int getGameType() const;
	const char *getGameId() const;
	Common::Language getLanguage() const;
	Common::Platform getPlatform() const;

	// VMHost
	void playMovie(const Common::String &name) override;
	void openStageFile(const Common::String &name) override;
	void sendToStage(const Common::String &message, const Common::Array<Value> &args) override;
	void openSetFile(const Common::String &name,
			const Common::String &scene = Common::String(),
			const Common::String &view = Common::String()) override;
	void closeSetFile() override;
	Common::String currentSet() override;
	void sendToScene(const Common::String &scene) override;
	bool actionFrame(int n) override;

private:
	/**
	 * Special-case the boot script: excise its CD presence check so the game
	 * can be run from an installed directory. The check is the if-block guarded
	 * by the "titanic1:" path literal; replacing it with no-op padding removes
	 * the notedialog/quit it would otherwise reach. Returns true if patched.
	 */
	static bool exciseBootCdCheck(Script &script);

	/**
	 * Install the named mouse cursor, decoding it on demand from the user's
	 * copy of TI.EXE. The cursor bitmaps are copyrighted game assets, so they
	 * are never embedded in ScummVM: they are read at runtime from the game's
	 * PE executable (RT_GROUP_CURSOR resources named CURS.ARROW, CURS.HAND, ...
	 * documented in files/decomp/movie-playback.md). The PEResources handle and
	 * decoded cursor groups are cached for reuse. Returns true on success.
	 */
	bool setGameCursor(const Common::String &name);

	/** Lazily open the game's TI.EXE for resource access. Returns nullptr if
	 *  it cannot be found (the game can still run without a custom cursor). */
	Common::PEResources *gameExe();

	/**
	 * Render node @p node of the currently open stage to the screen: decode its
	 * background frame (compositing from the nearest keyframe), apply the stage
	 * palette and show the navigation cursor. Mirrors TI.EXE FUN_0040b180.
	 */
	void renderStageNode(int node);

	/**
	 * Render the current angle of scene @p scene of the currently open set to the
	 * screen: replay the panorama frames up to the camera angle (cold-start buffer
	 * state), apply the set palette and show the navigation cursor. Mirrors the
	 * background-paint half of TI.EXE FUN_00431200 (sendtoscene). @p angle is the
	 * panorama index; the heading-to-view selection (FUN_00442b70 / FUN_00426250)
	 * lands with panorama navigation.
	 */
	void renderSetScene(int scene, int angle);

	/**
	 * Process the global/movie keyboard shortcuts that the original handles
	 * during playback, mirroring TI.EXE's WndProc (FUN_00403690) and movie key
	 * handler (FUN_0040e430): Esc / Ctrl+Q / Ctrl+. skip (when @p skippable),
	 * Ctrl+T pause/resume, F12 the About dialog, and backquote/Ctrl+D the debug
	 * console. Sets @p skip when the movie should be aborted. Returns the number
	 * of milliseconds spent paused, so wall-clock callers can shift their time
	 * references. @p audioHandle is the movie soundtrack handle (paused/resumed).
	 */
	uint32 handleMovieHotkeys(const Common::Event &event, bool skippable,
			const Audio::SoundHandle &audioHandle, bool &skip);

	/** Show the original's F12 "About" dialog (TI.EXE FUN_00404120). */
	void showAboutDialog();

	const CyberflixGameDescription *_gameDescription;
	Common::RandomSource _rnd;
	Console *_console; ///< Owned by the engine framework's debugger, not by us.

	Common::ScopedPtr<Common::PEResources> _exe;
	bool _exeTried = false;
	Common::HashMap<Common::String, Common::SharedPtr<Graphics::WinCursorGroup> > _cursorCache;
	Common::String _activeCursor;

	Common::ScopedPtr<Stage> _stage; ///< Currently open stage (DATA/*.STG), or null.
	Common::ScopedPtr<Set> _set;     ///< Currently open set (DATA/*.SET), or null.
	int _setScene = -1;              ///< Active scene index within _set, or -1.
	int _setAngle = 0;               ///< Active panorama angle within _setScene.

	/**
	 * The script VM driving the boot/stage scripts, with BOOTFILE res2
	 * registered as the global function library (TI.EXE keeps the equivalent
	 * scope chain around DAT_0045f010 / the boot resources).
	 */
	ScriptVM _vm;
	Common::ScopedPtr<Script> _globalLib;  ///< BOOTFILE res2 (function library).
	Common::ScopedPtr<Script> _bootScript; ///< BOOTFILE res1 (boot + handlers).

	/**
	 * Action-frame bitmask, mirroring TI.EXE DAT_0046112a: cleared by each
	 * playmovie (FUN_00446f80), bit 0/1 ORed in by the player main loop when
	 * the presented frame matches the master header cue-name field at +0x40 /
	 * +0x50 (FUN_0043b800 callers at 0x0040d19a/0x0040d1af). actionframe(n)
	 * (0x4e73, FUN_004362c0) tests bit n-1. See decomp/movie-playback.md.
	 */
	uint16 _actionFrameMask = 0;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_CYBERFLIX_H
