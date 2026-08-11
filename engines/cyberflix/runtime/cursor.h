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

#ifndef CYBERFLIX_RUNTIME_CURSOR_H
#define CYBERFLIX_RUNTIME_CURSOR_H

#include "common/fs.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/ptr.h"
#include "common/str.h"

namespace Common {
class PEResources;
}

namespace Graphics {
struct WinCursorGroup;
}

namespace Cyberflix {

class CursorRuntime {
public:
	bool setCursor(const Common::String &name);

	const Common::String &activeCursor() const { return _activeCursor; }

	/** Record the cursor name without applying its bitmap. Used by the load
	 *  path so the restored name is kept for future saves even when TI.EXE is
	 *  missing; setCursor() still applies the bitmap on its next call. */
	void setActiveCursorName(const Common::String &name) { _activeCursor = name; }

	/** True once an executable holding cursor resources has been located, so
	 *  callers can tell a missing resource from a missing executable. */
	bool haveCursorExe() { return gameExe() != nullptr; }

private:
	Common::PEResources *gameExe();

	/** Parse @p node as a PE and adopt it as the cursor source. With
	 *  @p requireCursors it is only adopted when it actually holds CURS.ARROW,
	 *  so a scan skips launchers and installers. */
	bool tryLoadExe(const Common::FSNode &node, bool requireCursors = false);

	/** Search @p dir (and @p depth levels below it) for the runtime. With
	 *  @p byNameOnly only files called TI.EXE are considered and no PE is
	 *  opened until one matches, which is why that pass can search deeper. */
	bool scanForExe(const Common::FSNode &dir, int depth, bool byNameOnly);

	enum {
		kNameSearchDepth = 4,  ///< Cheap: one string compare per directory entry.
		kProbeSearchDepth = 1  ///< Expensive: opens and parses every executable.
	};

	Common::ScopedPtr<Common::PEResources> _exe;
	bool _exeTried = false;
	Common::HashMap<Common::String, Common::SharedPtr<Graphics::WinCursorGroup> > _cursorCache;
	Common::String _activeCursor;  ///< Recorded name (may not be applied yet).
	Common::String _appliedCursor; ///< Name last handed to CursorMan.
};

} // End of namespace Cyberflix

#endif
