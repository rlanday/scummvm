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

#ifndef CYBERFLIX_RUNTIME_PATHS_H
#define CYBERFLIX_RUNTIME_PATHS_H

#include "common/fs.h"
#include "common/str.h"

namespace Cyberflix {

Common::String canonicalCDLabel(const Common::String &label);
bool findExtractedCDRoot(const Common::String &label, Common::FSNode &out);
bool validateTitanicDiscLayout();

class PathRuntime {
public:
	enum {
		kPathSlotCount = 9
	};

	Common::String pathSlot(int slot, const Common::String *newPath);
	Common::String currentCD(const Common::String *requested);

	void setPathSlot(int slot, const Common::String &path);
	const Common::String &pathSlotValue(int slot) const { return _pathSlots[slot]; }
	void setCurrentDiscRootName(const Common::String &name);

private:
	void registerPathSlotDirectory(int slot);

	Common::String _pathSlots[kPathSlotCount];        ///< Native path slots 0..8 (FUN_00438450).
	Common::String _pathSlotArchives[kPathSlotCount]; ///< SearchMan archive names for slots 1..8.
	Common::String _currentCD;                        ///< Native current CD Pascal string (DAT_00460d60).
};

} // End of namespace Cyberflix

#endif
