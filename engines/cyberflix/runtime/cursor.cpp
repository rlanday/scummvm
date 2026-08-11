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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/ptr.h"

#include "common/formats/winexe_pe.h"

#include "graphics/cursorman.h"
#include "graphics/wincursor.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/runtime/cursor.h"

namespace Cyberflix {

// The cursor bitmaps are copyrighted game art, so they are loaded at runtime
// from the player's own TI.EXE rather than shipped with ScummVM. TI.EXE is the
// CyberFlix "Bicycle" runtime; in an installed game it lives under INSTALL/BINX
// (or INSTALL/BIN). It is a Win32 PE whose RT_GROUP_CURSOR resources are named
// CURS.ARROW, CURS.HAND, CURS.GOUP, ...
Common::PEResources *CursorRuntime::gameExe() {
	if (_exeTried)
		return _exe.get();
	_exeTried = true;

	const Common::FSNode gameDir(ConfMan.getPath("path"));

	// The 2-CD retail installer puts the runtime under INSTALL/BINX (older
	// builds: INSTALL/BIN). Re-releases repackage an already-installed tree, so
	// also accept the same subdirectories at the top level and TI.EXE beside the
	// data files.
	static const char *const candidates[][3] = {
		{ "INSTALL", "BINX", "TI.EXE" },
		{ "INSTALL", "BIN",  "TI.EXE" },
		{ "BINX",    nullptr, "TI.EXE" },
		{ "BIN",     nullptr, "TI.EXE" },
		{ nullptr,   nullptr, "TI.EXE" }
	};
	for (uint c = 0; c < ARRAYSIZE(candidates); ++c) {
		Common::FSNode node = gameDir;
		for (uint part = 0; part < 2; ++part) {
			if (candidates[c][part])
				node = node.getChild(candidates[c][part]);
		}
		node = node.getChild(candidates[c][2]);
		if (tryLoadExe(node))
			return _exe.get();
	}

	// Fall back to searching for the runtime, so a repackaged layout still
	// yields the bitmaps rather than leaving the game cursorless. Matching
	// TI.EXE by name only costs a string compare per entry, so that pass can go
	// deeper; the pass that opens every executable to probe for the cursor group
	// is the expensive one and stays shallow.
	if (scanForExe(gameDir, kNameSearchDepth, true))
		return _exe.get();
	if (scanForExe(gameDir, kProbeSearchDepth, false))
		return _exe.get();

	warning("Cyberflix: could not locate TI.EXE (or any executable holding the "
			"CURS.* cursor resources) under '%s'", gameDir.getPath().toString().c_str());
	return nullptr;
}

// Parse @p node as a PE and keep it if it holds the cursor resources.
bool CursorRuntime::tryLoadExe(const Common::FSNode &node, bool requireCursors) {
	if (!node.exists() || node.isDirectory())
		return false;
	Common::ScopedPtr<Common::SeekableReadStream> stream(node.createReadStream());
	if (!stream)
		return false;
	Common::ScopedPtr<Common::PEResources> exe(new Common::PEResources());
	const bool loaded = exe->loadFromEXE(stream.get(), DisposeAfterUse::YES);
	stream.release();
	if (!loaded)
		return false; // exe (and the stream it owns) is freed as it goes out of scope
	if (requireCursors) {
		// A repackaged tree can ship several executables (launchers, installers);
		// only the CyberFlix runtime carries the cursor group.
		Common::ScopedPtr<Graphics::WinCursorGroup> probe(
				Graphics::WinCursorGroup::createCursorGroup(exe.get(), Common::WinResourceID("CURS.ARROW")));
		if (!probe || probe->cursors.empty())
			return false;
	}
	debug(1, "Cyberflix: cursor resources loaded from '%s'", node.getPath().toString().c_str());
	_exe.reset(exe.release());
	return true;
}

// With @p byNameOnly, accept only files literally called TI.EXE and skip the
// cursor-group probe; otherwise open every executable and keep the first that
// actually carries CURS.ARROW.
bool CursorRuntime::scanForExe(const Common::FSNode &dir, int depth, bool byNameOnly) {
	Common::FSList entries;
	if (!dir.getChildren(entries, Common::FSNode::kListAll))
		return false;
	for (Common::FSList::const_iterator it = entries.begin(); it != entries.end(); ++it) {
		if (it->isDirectory())
			continue;
		Common::String name = it->getName();
		name.toUppercase();
		if (byNameOnly ? (name != "TI.EXE") : !name.hasSuffix(".EXE"))
			continue;
		if (tryLoadExe(*it, !byNameOnly))
			return true;
	}
	if (depth <= 0)
		return false;
	for (Common::FSList::const_iterator it = entries.begin(); it != entries.end(); ++it) {
		if (it->isDirectory() && scanForExe(*it, depth - 1, byNameOnly))
			return true;
	}
	return false;
}

bool CursorRuntime::setCursor(const Common::String &name) {
	// Early-out on the APPLIED cursor, not _activeCursor: the load path
	// records the restored name via setActiveCursorName() before calling
	// here, and the bitmap must still be (re)applied in that case.
	if (_appliedCursor == name && _cursorCache.contains(name))
		return true;

	Common::SharedPtr<Graphics::WinCursorGroup> group;
	if (_cursorCache.contains(name)) {
		group = _cursorCache[name];
	} else {
		Common::PEResources *exe = gameExe();
		if (!exe)
			return false;
		group = Common::SharedPtr<Graphics::WinCursorGroup>(
				Graphics::WinCursorGroup::createCursorGroup(exe, Common::WinResourceID(name)));
		_cursorCache[name] = group; // cache even null to avoid re-parsing
	}
	if (!group || group->cursors.empty()) {
		debug(1, "Cyberflix: cursor '%s' missing/empty in TI.EXE", name.c_str());
		return false;
	}

	CursorMan.replaceCursor(group->cursors[0].cursor);
	_activeCursor = name;
	_appliedCursor = name;
	debug(1, "Cyberflix: cursor -> %s", name.c_str());
	return true;
}

bool CyberflixEngine::setGameCursor(const Common::String &name) {
	const Common::String oldCursor = _cursorRuntime.activeCursor();
	const bool ok = _cursorRuntime.setCursor(name);
	if (ok && (oldCursor != _cursorRuntime.activeCursor() || !CursorMan.isVisible()))
		_cursorPresentationDirty = true;
	return ok;
}

} // End of namespace Cyberflix
