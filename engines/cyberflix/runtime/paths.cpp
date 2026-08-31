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

#include "cyberflix/cyberflix.h"
#include "cyberflix/game_support.h"
#include "cyberflix/runtime/paths.h"

namespace CyberFlix {

PathRuntime::PathRuntime() : _gameSupport(nullptr) {
}

static bool validNativeCDLabel(const Common::String &label) {
	if (label.size() > 11)
		return false;
	for (uint i = 0; i < label.size(); ++i) {
		const char c = label[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
				(c >= '0' && c <= '9')))
			return false;
	}
	return true;
}

Common::String PathRuntime::getPathSlot(int slot) const {
	if (slot < 0 || slot > 8) {
		warning("CyberFlix: path(%d): invalid slot", slot);
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
		warning("CyberFlix: currentcd('%s'): invalid CD label", requested.c_str());
	} else {
		Common::String mountedLabel;
		if (_gameSupport && _gameSupport->resolveDisc(requested, mountedLabel)) {
			_currentCD = mountedLabel;
		} else {
			_currentCD.clear();
			debug(1, "CyberFlix: currentcd('%s') did not find an extracted disc directory",
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
	const Common::String canonical = _gameSupport ?
			_gameSupport->canonicalDiscLabel(name) : name;
	_pathSlots[0] = canonical + ":";
	_currentCD = canonical;
}

void PathRuntime::registerPathSlotDirectory(int slot) {
	if (slot < 1 || slot > 8)
		return;

	if (!_pathSlotArchives[slot].empty()) {
		SearchMan.remove(_pathSlotArchives[slot]);
		_pathSlotArchives[slot].clear();
	}

	if (_pathSlots[slot].empty())
		return;

	Common::FSNode dir;
	if (!_gameSupport || !_gameSupport->resolvePathDirectory(_pathSlots[slot], dir)) {
		debug(1, "CyberFlix: path slot %d '%s' did not resolve to a directory",
				slot, _pathSlots[slot].c_str());
		return;
	}

	_pathSlotArchives[slot] = Common::String::format("cyberflix-path%d", slot);
	SearchMan.addDirectory(_pathSlotArchives[slot], dir, 10, 1, false);
	debug(1, "CyberFlix: path slot %d '%s' -> '%s'", slot, _pathSlots[slot].c_str(),
			dir.getPath().toString(Common::Path::kNativeSeparator).c_str());
}

Common::String CyberFlixEngine::getPathSlot(int slot) {
	return _pathRuntime.getPathSlot(slot);
}

Common::String CyberFlixEngine::setPathSlot(int slot, const Common::String &newPath) {
	return _pathRuntime.setPathSlot(slot, newPath);
}

Common::String CyberFlixEngine::getCurrentCD() {
	return _pathRuntime.getCurrentCD();
}

Common::String CyberFlixEngine::setCurrentCD(const Common::String &requested) {
	return _pathRuntime.setCurrentCD(requested);
}

} // End of namespace CyberFlix
