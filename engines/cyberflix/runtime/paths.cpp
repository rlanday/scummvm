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
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/path.h"

#include "gui/message.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/runtime/paths.h"

namespace Cyberflix {

static bool findCaselessChildDir(const Common::FSNode &root, const Common::String &name,
		Common::FSNode &out) {
	Common::FSList children;
	if (!root.getChildren(children, Common::FSNode::kListDirectoriesOnly, true))
		return false;
	for (Common::FSList::const_iterator it = children.begin(); it != children.end(); ++it) {
		if (it->getName().equalsIgnoreCase(name)) {
			out = *it;
			return true;
		}
	}
	return false;
}

static bool findCaselessChildFile(const Common::FSNode &root, const Common::String &name,
		Common::FSNode &out) {
	Common::FSList children;
	if (!root.getChildren(children, Common::FSNode::kListFilesOnly, true))
		return false;
	for (Common::FSList::const_iterator it = children.begin(); it != children.end(); ++it) {
		if (it->getName().equalsIgnoreCase(name)) {
			out = *it;
			return true;
		}
	}
	return false;
}

static bool findCaselessPathDir(const Common::FSNode &root,
		const Common::Array<Common::String> &components, Common::FSNode &out) {
	if (!root.exists() || !root.isDirectory() || components.empty())
		return false;

	Common::FSNode node = root;
	for (uint i = 0; i < components.size(); ++i) {
		Common::FSNode child;
		if (!findCaselessChildDir(node, components[i], child))
			return false;
		node = child;
	}

	out = node;
	return out.exists() && out.isDirectory();
}

static Common::Array<Common::String> splitCyberflixPath(const Common::String &path) {
	Common::Array<Common::String> components;
	Common::String token;
	for (uint i = 0; i < path.size(); ++i) {
		if (path[i] == ':') {
			if (!token.empty()) {
				token.toLowercase();
				components.push_back(token);
				token.clear();
			}
		} else {
			token += path[i];
		}
	}
	if (!token.empty()) {
		token.toLowercase();
		components.push_back(token);
	}
	return components;
}

static bool isTitanicCDLabel(const Common::String &label) {
	return label.equalsIgnoreCase("Titanic1") || label.equalsIgnoreCase("Titanic2");
}

// A merged tree holds both discs' assets side by side, so requiring one file
// per disc distinguishes it from a plain TITANIC1/DATA: the retail DATA folder
// has BOOTFILE but never SMETH1.PUP, which ships on CD 2.
static bool isRepackagedDataDir(const Common::FSNode &dir) {
	if (!dir.exists() || !dir.isDirectory())
		return false;
	Common::FSNode file;
	return findCaselessChildFile(dir, "BOOTFILE", file) &&
			findCaselessChildFile(dir, "SMETH1.PUP", file);
}

// Re-releases (the Steam build, for one) ship an already-installed tree with
// every asset flattened into a single LOCAL directory instead of the two-CD
// TITANIC1/TITANIC2 layout the retail installer produces. Resolve that root
// once per game path; the result feeds the search path, the layout check and
// every script-visible path lookup.
bool findRepackagedDataRoot(Common::FSNode &out) {
	static Common::String cachedFor;
	static Common::FSNode cachedRoot;
	static bool cachedValid = false;

	const Common::Path gamePath = ConfMan.getPath("path");
	const Common::String key = gamePath.toString();
	if (key != cachedFor) {
		cachedFor = key;
		cachedValid = false;

		const Common::FSNode gameDir(gamePath);
		Common::FSNode local;
		if (isRepackagedDataDir(gameDir)) {
			cachedRoot = gameDir;
			cachedValid = true;
		} else if (findCaselessChildDir(gameDir, "LOCAL", local) && isRepackagedDataDir(local)) {
			cachedRoot = local;
			cachedValid = true;
		}
	}

	if (cachedValid)
		out = cachedRoot;
	return cachedValid;
}

static bool resolveCyberflixPathDir(const Common::String &path, Common::FSNode &out) {
	Common::Array<Common::String> components = splitCyberflixPath(path);
	if (components.empty())
		return false;

	// In a repackaged tree there is only one asset directory, so every path the
	// scripts ask for ("Titanic1:Data", "Titanic2:Puppets1", "Blkjack", ...)
	// resolves to it.
	if (findRepackagedDataRoot(out))
		return true;

	Common::Array<Common::Array<Common::String> > patterns;
	patterns.push_back(components);
	if (components.size() > 1 && !isTitanicCDLabel(components[0])) {
		Common::Array<Common::String> tail;
		for (uint i = 1; i < components.size(); ++i)
			tail.push_back(components[i]);
		patterns.push_back(tail);
	}
	if (!isTitanicCDLabel(components[0])) {
		Common::Array<Common::String> finalComponent;
		finalComponent.push_back(components[components.size() - 1]);
		patterns.push_back(finalComponent);
	}

	const Common::FSNode gameDir(ConfMan.getPath("path"));
	Common::FSNode roots[3] = { gameDir, gameDir.getParent(), gameDir.getParent().getParent() };
	for (uint p = 0; p < patterns.size(); ++p) {
		for (uint r = 0; r < ARRAYSIZE(roots); ++r) {
			if (findCaselessPathDir(roots[r], patterns[p], out))
				return true;
		}
	}
	return false;
}

Common::String canonicalCDLabel(const Common::String &label) {
	if (label.equalsIgnoreCase("Titanic1"))
		return "Titanic1";
	if (label.equalsIgnoreCase("Titanic2"))
		return "Titanic2";
	return label;
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

bool findExtractedCDRoot(const Common::String &label, Common::FSNode &out) {
	const Common::FSNode gameDir(ConfMan.getPath("path"));
	if (gameDir.exists() && gameDir.isDirectory() &&
			gameDir.getName().equalsIgnoreCase(label)) {
		out = gameDir;
		return true;
	}

	Common::FSNode child;
	if (findCaselessChildDir(gameDir, label, child)) {
		out = child;
		return true;
	}

	Common::FSNode parent = gameDir.getParent();
	if (parent.exists() && parent.isDirectory() &&
			parent.getName().equalsIgnoreCase(label)) {
		out = parent;
		return true;
	}

	Common::FSNode sibling;
	if (findCaselessChildDir(parent, label, sibling)) {
		out = sibling;
		return true;
	}

	Common::FSNode grandparent = parent.getParent();
	if (findCaselessChildDir(grandparent, label, sibling)) {
		out = sibling;
		return true;
	}

	return false;
}

static bool hasDiscFile(const Common::FSNode &discRoot, const char *dirName, const char *fileName) {
	Common::FSNode dir;
	if (!findCaselessChildDir(discRoot, dirName, dir))
		return false;
	Common::FSNode file;
	return findCaselessChildFile(dir, fileName, file);
}

static void addMissingTitanicFile(Common::String &missing, const char *path) {
	missing += "\n  - ";
	missing += path;
}

static bool hasFlatFile(const Common::FSNode &root, const char *fileName) {
	Common::FSNode file;
	return findCaselessChildFile(root, fileName, file);
}

bool validateTitanicDiscLayout() {
	Common::String missing;
	Common::FSNode cd1Root, cd2Root;

	// A repackaged tree carries both discs in one directory; check the same
	// per-disc landmarks there rather than demanding the two-CD layout.
	Common::FSNode flatRoot;
	if (findRepackagedDataRoot(flatRoot)) {
		static const char *const required[] = {
			"BOOTFILE", "PLAYMODE.MOV", "DECKC.TRK", "DATECAB.MOV", "SMETH1.PUP"
		};
		for (uint i = 0; i < ARRAYSIZE(required); ++i) {
			if (!hasFlatFile(flatRoot, required[i]))
				addMissingTitanicFile(missing, required[i]);
		}
		if (missing.empty())
			return true;

		Common::String message =
				"This looks like a repackaged (single-folder) Titanic install, but some "
				"data files are missing.\n\nMissing from '";
		message += flatRoot.getPath().toString(Common::Path::kNativeSeparator);
		message += "':";
		message += missing;
		GUIErrorMessage(message);
		warning("Cyberflix: incomplete repackaged Titanic data:%s", missing.c_str());
		return false;
	}

	if (!findExtractedCDRoot("Titanic1", cd1Root)) {
		addMissingTitanicFile(missing, "TITANIC1/");
	} else {
		if (!hasDiscFile(cd1Root, "DATA", "BOOTFILE"))
			addMissingTitanicFile(missing, "TITANIC1/DATA/BOOTFILE");
		if (!hasDiscFile(cd1Root, "MOVIES", "PLAYMODE.MOV"))
			addMissingTitanicFile(missing, "TITANIC1/MOVIES/PLAYMODE.MOV");
	}

	if (!findExtractedCDRoot("Titanic2", cd2Root)) {
		addMissingTitanicFile(missing, "TITANIC2/");
	} else {
		if (!hasDiscFile(cd2Root, "DATA", "DECKC.TRK"))
			addMissingTitanicFile(missing, "TITANIC2/DATA/DECKC.TRK");
		if (!hasDiscFile(cd2Root, "MOVIES", "DATECAB.MOV"))
			addMissingTitanicFile(missing, "TITANIC2/MOVIES/DATECAB.MOV");
		if (!hasDiscFile(cd2Root, "PUPPETS1", "SMETH1.PUP"))
			addMissingTitanicFile(missing, "TITANIC2/PUPPETS1/SMETH1.PUP");
	}

	if (missing.empty())
		return true;

	Common::String message =
			"Titanic: Adventure Out of Time requires data from both Windows CDs.\n\n"
			"Extract each CD into a sibling folder named TITANIC1 and TITANIC2, without merging them. "
			"Then add/select TITANIC1 or their parent folder in ScummVM.\n\n"
			"Expected layout examples:\n"
			"  TITANIC1/DATA/BOOTFILE\n"
			"  TITANIC1/MOVIES/PLAYMODE.MOV\n"
			"  TITANIC2/DATA/DECKC.TRK\n"
			"  TITANIC2/MOVIES/DATECAB.MOV\n"
			"  TITANIC2/PUPPETS1/SMETH1.PUP\n\n"
			"Missing:";
	message += missing;

	GUIErrorMessage(message);
	warning("Cyberflix: missing Titanic two-disc data:%s", missing.c_str());
	return false;
}

Common::String PathRuntime::getPathSlot(int slot) const {
	if (slot < 0 || slot > 8) {
		warning("Cyberflix: path(%d): invalid slot", slot);
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
		warning("Cyberflix: currentcd('%s'): invalid CD label", requested.c_str());
	} else {
		Common::FSNode cdRoot;
		// Both discs are present at once in a repackaged tree, so any disc the
		// scripts switch to is always "mounted".
		if (isTitanicCDLabel(requested) && findRepackagedDataRoot(cdRoot)) {
			_currentCD = canonicalCDLabel(requested);
		} else if (findExtractedCDRoot(requested, cdRoot)) {
			_currentCD = canonicalCDLabel(cdRoot.getName());
		} else {
			_currentCD.clear();
			debug(1, "Cyberflix: currentcd('%s') did not find an extracted disc directory",
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
	_pathSlots[0] = canonicalCDLabel(name) + ":";
	_currentCD = canonicalCDLabel(name);
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
	if (!resolveCyberflixPathDir(_pathSlots[slot], dir)) {
		debug(1, "Cyberflix: path slot %d '%s' did not resolve to a directory",
				slot, _pathSlots[slot].c_str());
		return;
	}

	_pathSlotArchives[slot] = Common::String::format("cyberflix-path%d", slot);
	SearchMan.addDirectory(_pathSlotArchives[slot], dir, 10, 1, false);
	debug(1, "Cyberflix: path slot %d '%s' -> '%s'", slot, _pathSlots[slot].c_str(),
			dir.getPath().toString(Common::Path::kNativeSeparator).c_str());
}

Common::String CyberflixEngine::getPathSlot(int slot) {
	return _pathRuntime.getPathSlot(slot);
}

Common::String CyberflixEngine::setPathSlot(int slot, const Common::String &newPath) {
	return _pathRuntime.setPathSlot(slot, newPath);
}

Common::String CyberflixEngine::getCurrentCD() {
	return _pathRuntime.getCurrentCD();
}

Common::String CyberflixEngine::setCurrentCD(const Common::String &requested) {
	return _pathRuntime.setCurrentCD(requested);
}

} // End of namespace Cyberflix
