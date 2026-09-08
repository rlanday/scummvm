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

#ifndef DREAMFACTORY_RUNTIME_PATHS_H
#define DREAMFACTORY_RUNTIME_PATHS_H

#include "common/fs.h"
#include "common/str.h"

namespace DreamFactory {

class GameSupport;

/**
 * Bridges DreamFactory's script-visible path state to ScummVM file lookup.
 *
 * The path(slot[, value]) builtin reads or writes one of nine logical path
 * strings. These retain the original colon-separated syntax, such as
 * "Titanic2:data:". Slot 0 is the startup root used to construct other paths;
 * assigning slots 1 through 8 resolves the logical path through GameSupport
 * and exposes the resulting directory through SearchMan. The currentcd([name])
 * builtin separately tracks the logical disc selected by the game script.
 *
 * Slot meanings are assigned by each game's scripts rather than by the
 * DreamFactory runtime. After Titanic's BOOTFILE setpath() initialization,
 * they are used as follows:
 *
 * - 0: startup root supplied by the host.
 * - 1: slot 0 followed by "tour:".
 * - 2: slot 0 followed by "local:".
 * - 3: the selected disc's "data:" directory.
 * - 4: "puppets2:" on disc 1 or "puppets1:" on disc 2.
 * - 5: the selected disc's "movies:" directory.
 * - 6: not assigned by the retail BOOTFILE.
 * - 7: the selected disc's "narend:" ending directory when needed.
 * - 8: not assigned by the retail BOOTFILE.
 */
class PathRuntime {
public:
	/** Bind the per-title path policy, which must outlive this runtime. */
	explicit PathRuntime(const GameSupport &gameSupport);

	enum {
		kPathSlotCount = 9
	};

	/** Return the script-visible value of a path slot. */
	Common::String getPathSlot(int slot) const;

	/** Set a path slot and refresh its SearchMan directory registration. */
	Common::String setPathSlot(int slot, const Common::String &newPath);

	/** Return the logical disc label exposed by currentcd(). */
	Common::String getCurrentCD() const;

	/** Select a logical disc through the per-game path policy. */
	Common::String setCurrentCD(const Common::String &requested);

	/** Restore a slot value from engine state and refresh file lookup. */
	void setPathSlotValue(int slot, const Common::String &path);

	/** Return a slot already known by the caller to be in range. */
	const Common::String &pathSlotValue(int slot) const { return _pathSlots[slot]; }

	/** Initialize slot 0 and currentcd() from the detected startup disc. */
	void setCurrentDiscRootName(const Common::String &name);

private:
	void registerPathSlotDirectory(int slot);

	Common::String _pathSlots[kPathSlotCount];        ///< Values returned by the original path() builtin (FUN_00438450).
	Common::String _pathSlotArchives[kPathSlotCount]; ///< SearchMan registration keys used to replace slots 1 through 8.
	/**
	 * Script-visible disc label returned by currentcd(), such as "Titanic2".
	 * This mirrors the original Pascal string at DAT_00460d60, but is stored as
	 * a Common::String. Calling currentcd(name) verifies the requested disc and
	 * records its logical label. It does not update SearchMan; the game's
	 * setpath() script separately assigns the corresponding path slots.
	 */
	Common::String _currentCD;
	const GameSupport &_gameSupport;                  ///< Non-owning per-title path policy.
};

} // End of namespace DreamFactory

#endif
