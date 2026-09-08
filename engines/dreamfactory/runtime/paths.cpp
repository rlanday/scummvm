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

#include "common/archive.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/path.h"
#include "common/util.h"

#include "dreamfactory/dreamfactory.h"
#include "dreamfactory/game_support.h"
#include "dreamfactory/runtime/paths.h"

namespace DreamFactory {

PathRuntime::PathRuntime(const GameSupport &gameSupport) : _gameSupport(gameSupport) {
}

// TI.EXE's currentcd() validator at 0x00439e70 caps the input Pascal-string
// length byte at 0x0b (11 characters).
static const uint kMaxNativeCDLabelLength = 11;

static bool validNativeCDLabel(const Common::String &label) {
	if (label.size() > kMaxNativeCDLabelLength)
		return false;
	for (const char c : label) {
		if (!Common::isAlnum(c))
			return false;
	}
	return true;
}

Common::String PathRuntime::getPathSlot(int slot) const {
	if (slot < 0 || slot >= kPathSlotCount) {
		warning("DreamFactory: path(%d): invalid slot", slot);
		return Common::String();
	}

	return _pathSlots[slot];
}

Common::String PathRuntime::setPathSlot(int slot, const Common::String &newPath) {
	setPathSlotValue(slot, newPath);
	return getPathSlot(slot);
}

Common::String PathRuntime::getCurrentCD() const {
	return _currentCD;
}

Common::String PathRuntime::setCurrentCD(const Common::String &requested) {
	if (requested.empty()) {
		_currentCD.clear();
	} else if (!validNativeCDLabel(requested)) {
		warning("DreamFactory: currentcd('%s'): invalid CD label", requested.c_str());
	} else {
		Common::String mountedLabel;
		if (_gameSupport.resolveDisc(requested, mountedLabel)) {
			_currentCD = mountedLabel;
		} else {
			_currentCD.clear();
			debug(1, "DreamFactory: currentcd('%s') did not find an extracted disc directory",
					requested.c_str());
		}
	}
	return _currentCD;
}

void PathRuntime::setPathSlotValue(int slot, const Common::String &path) {
	if (slot < 0 || slot >= kPathSlotCount)
		return;
	_pathSlots[slot] = path;
	registerPathSlotDirectory(slot);
}

void PathRuntime::setCurrentDiscRootName(const Common::String &name) {
	const Common::String canonical = _gameSupport.canonicalDiscLabel(name);
	_pathSlots[0] = canonical + ":";
	_currentCD = canonical;
}

void PathRuntime::registerPathSlotDirectory(int slot) {
	if (slot < 1 || slot >= kPathSlotCount)
		return;

	if (!_pathSlotArchives[slot].empty()) {
		SearchMan.remove(_pathSlotArchives[slot]);
		_pathSlotArchives[slot].clear();
	}

	if (_pathSlots[slot].empty())
		return;

	Common::FSNode dir;
	if (!_gameSupport.resolvePathDirectory(_pathSlots[slot], dir)) {
		debug(1, "DreamFactory: path slot %d '%s' did not resolve to a directory",
				slot, _pathSlots[slot].c_str());
		return;
	}

	// SearchMan treats directories and archive files uniformly as named search
	// sources. A stable name lets us remove the directory when this slot changes;
	// priority 10 places script-selected paths ahead of the startup data roots.
	_pathSlotArchives[slot] = Common::String::format("dreamfactory-path%d", slot);
	SearchMan.addDirectory(_pathSlotArchives[slot], dir, 10, 1, false);
	debug(1, "DreamFactory: path slot %d '%s' -> '%s'", slot, _pathSlots[slot].c_str(),
			dir.getPath().toString(Common::Path::kNativeSeparator).c_str());
}

Common::String DreamFactoryEngine::getPathSlot(int slot) {
	return _pathRuntime.getPathSlot(slot);
}

Common::String DreamFactoryEngine::setPathSlot(int slot, const Common::String &newPath) {
	return _pathRuntime.setPathSlot(slot, newPath);
}

Common::String DreamFactoryEngine::getCurrentCD() {
	return _pathRuntime.getCurrentCD();
}

Common::String DreamFactoryEngine::setCurrentCD(const Common::String &requested) {
	return _pathRuntime.setCurrentCD(requested);
}

} // End of namespace DreamFactory
