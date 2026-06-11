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

#include "engines/engine.h"

#include "cyberflix/detection.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

class Console;
class Script;
}

namespace Common {
class PEResources;
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

	const CyberflixGameDescription *_gameDescription;
	Common::RandomSource _rnd;
	Console *_console;

	Common::PEResources *_exe = nullptr;
	bool _exeTried = false;
	Common::HashMap<Common::String, Graphics::WinCursorGroup *> _cursorCache;
	Common::String _activeCursor;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_CYBERFLIX_H
