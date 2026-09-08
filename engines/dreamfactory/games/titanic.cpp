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
#include "common/util.h"

#include "gui/message.h"

#include "dreamfactory/dreamfactory.h"
#include "dreamfactory/game_support.h"
#include "dreamfactory/games/titanic.h"
#include "dreamfactory/runtime/paths.h"
#include "dreamfactory/script.h"
#include "dreamfactory/vm.h"

namespace DreamFactory {

static const int kCargoPaintingTimerFrames = 10000;
static const int kCargoPaintingTimerLogSeconds = 10;

class TitanicGameSupport : public GameSupport {
public:
	const GameProfile &profile() const override;
	Common::Error initializePaths(PathRuntime &paths) const override;
	bool patchBootScript(Script &script) const override;
	Common::String canonicalDiscLabel(const Common::String &label) const override;
	bool resolveDisc(const Common::String &requested,
			Common::String &mountedLabel) const override;
	bool resolvePathDirectory(const Common::String &path,
			Common::FSNode &out) const override;
	void onForceUpdate(DreamFactoryEngine &engine) override;
	void onGameStateLoaded(const ScriptVM &vm) override;
	bool shouldLogScriptVariable(const Common::String &name) const override;
	bool shouldLogScriptDispatch(const Common::String &name,
			const Common::String &self) const override;

private:
	bool findRepackagedDataRoot(Common::FSNode &out) const;
	bool validateDiscLayout() const;

	mutable bool _repackagedDataRootChecked = false;
	mutable bool _repackagedDataRootValid = false;
	mutable Common::FSNode _repackagedDataRoot;

	int _cargoPaintingTimerStartFrame = 0;
	int _lastCargoPaintingTimerLogBucket = -1;
	bool _cargoPaintingTimerExpiredLogged = false;
};

// Finds a child directory of @p root whose name matches @p name case-insensitively.
static bool findCaselessChildDir(const Common::FSNode &root, const Common::String &name,
		Common::FSNode &out) {
	Common::FSList children;
	if (!root.getChildren(children, Common::FSNode::kListDirectoriesOnly, true))
		return false;
	for (const Common::FSNode &child : children) {
		if (child.getName().equalsIgnoreCase(name)) {
			out = child;
			return true;
		}
	}
	return false;
}

// Finds a child file of @p root whose name matches @p name case-insensitively.
static bool findCaselessChildFile(const Common::FSNode &root, const Common::String &name,
		Common::FSNode &out) {
	Common::FSList children;
	if (!root.getChildren(children, Common::FSNode::kListFilesOnly, true))
		return false;
	for (const Common::FSNode &child : children) {
		if (child.getName().equalsIgnoreCase(name)) {
			out = child;
			return true;
		}
	}
	return false;
}

// Walks @p components downward from @p root, matching every level
// case-insensitively, and reports the final directory in @p out.
static bool findCaselessPathDir(const Common::FSNode &root,
		const Common::Array<Common::String> &components, Common::FSNode &out) {
	if (!root.exists() || !root.isDirectory() || components.empty())
		return false;

	Common::FSNode node = root;
	for (const Common::String &component : components) {
		Common::FSNode child;
		if (!findCaselessChildDir(node, component, child))
			return false;
		node = child;
	}

	out = node;
	return true;
}

// Splits a colon-separated DreamFactory path such as "titanic1:data" into its
// lowercased components.
static Common::Array<Common::String> splitDreamFactoryPath(const Common::String &path) {
	Common::Array<Common::String> components;
	Common::String token;
	for (const char c : path) {
		if (c == ':') {
			if (!token.empty()) {
				token.toLowercase();
				components.push_back(token);
				token.clear();
			}
		} else {
			token += c;
		}
	}
	if (!token.empty()) {
		token.toLowercase();
		components.push_back(token);
	}
	return components;
}

// True when @p label names either of Titanic's two discs, whatever the casing.
static bool isTitanicCDLabel(const Common::String &label) {
	return label.equalsIgnoreCase("Titanic1") || label.equalsIgnoreCase("Titanic2");
}

// Maps either casing of a disc label to its canonical "Titanic1"/"Titanic2"
// spelling; any other label passes through unchanged.
static Common::String canonicalTitanicCDLabel(const Common::String &label) {
	if (label.equalsIgnoreCase("Titanic1"))
		return "Titanic1";
	if (label.equalsIgnoreCase("Titanic2"))
		return "Titanic2";
	return label;
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

// The Steam re-release ships an already-installed tree with every asset
// flattened into one LOCAL directory instead of the retail two-CD layout.
// Returns true and sets @p out when the configured game directory or its LOCAL
// child contains both BOOTFILE and SMETH1.PUP, identifying a merged data tree.
// The lookup result is cached; @p out is only assigned on success.
bool TitanicGameSupport::findRepackagedDataRoot(Common::FSNode &out) const {
	if (!_repackagedDataRootChecked) {
		_repackagedDataRootChecked = true;
		const Common::FSNode gameDir(ConfMan.getPath("path"));
		Common::FSNode local;
		// The launcher may point directly at the directory containing the
		// merged assets, or at the installation directory containing LOCAL.
		// Accept either choice of game-data path.
		if (isRepackagedDataDir(gameDir)) {
			_repackagedDataRoot = gameDir;
			_repackagedDataRootValid = true;
		} else if (findCaselessChildDir(gameDir, "LOCAL", local) &&
				isRepackagedDataDir(local)) {
			_repackagedDataRoot = local;
			_repackagedDataRootValid = true;
		}
	}

	if (_repackagedDataRootValid)
		out = _repackagedDataRoot;
	return _repackagedDataRootValid;
}

// Locates an extracted disc root named @p label near the game directory: the
// game dir itself, a child of it, its parent, or a sibling of the game dir or
// of the parent.
// The launcher stores one game-data path, but the retail assets span TITANIC1
// and TITANIC2. That path may point to their common parent, to TITANIC1 itself,
// or to TITANIC1/DATA. These searches let us find both discs in each layout
// without requiring the user to merge their contents or configure two paths.
// For example, from TITANIC1/DATA, CD 1 is the parent and CD 2 is its sibling.
static bool findExtractedCDRoot(const Common::String &label, Common::FSNode &out) {
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

// True when <discRoot>/<dirName>/<fileName> exists (case-insensitive segments).
static bool hasDiscFile(const Common::FSNode &discRoot, const char *dirName,
		const char *fileName) {
	Common::FSNode dir;
	if (!findCaselessChildDir(discRoot, dirName, dir))
		return false;
	Common::FSNode file;
	return findCaselessChildFile(dir, fileName, file);
}

// True when @p fileName sits directly inside @p root (case-insensitive).
static bool hasFlatFile(const Common::FSNode &root, const char *fileName) {
	Common::FSNode file;
	return findCaselessChildFile(root, fileName, file);
}

// Appends one indented line to the missing-file list shown by the layout error dialogs.
static void addMissingTitanicFile(Common::String &missing, const char *path) {
	missing += "\n  - ";
	missing += path;
}

// Checks that either the repackaged single-folder install or the extracted
// TITANIC1/TITANIC2 two-disc layout holds the files the engine needs, showing
// an error dialog that lists whatever is missing.
bool TitanicGameSupport::validateDiscLayout() const {
	Common::String missing;
	Common::FSNode cd1Root, cd2Root;

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
		warning("DreamFactory: incomplete repackaged Titanic data:%s", missing.c_str());
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
	warning("DreamFactory: missing Titanic two-disc data:%s", missing.c_str());
	return false;
}

// Reads a VM global as an integer, defaulting to 0 when unset or non-numeric.
static int globalIntValue(const ScriptVM &vm, const char *name) {
	Common::HashMap<Common::String, Value>::const_iterator it = vm.globalVars().find(name);
	if (it == vm.globalVars().end())
		return 0;
	if (it->_value.type != Value::kBool && it->_value.type != Value::kInt)
		return 0;
	return it->_value.intValue;
}

// The TI.EXE runtime executable name and Titanic's default save signature.
const GameProfile &TitanicGameSupport::profile() const {
	static const GameProfile profile = { "TI.EXE", "Titanic 1.0" };
	return profile;
}

// Supplies startup file-search paths without requiring the Windows installer.
// The original expects an installed layout: the installer copies BOOTFILE
// beside TI.EXE and creates LOCAL and TOUR directories. On extracted CDs,
// BOOTFILE remains in TITANIC1/DATA, movies are in TITANIC1/MOVIES, and TI.EXE
// is inside the installer directories. Searching beside TI.EXE would therefore
// find neither BOOTFILE nor the movies. Register DATA and MOVIES with SearchMan
// instead.
//
// SearchMan registration keys identify search sources, not script paths or
// prefixes to add to filenames. The merged directory uses "dreamfactory-data";
// addSubDirectoryMatching() uses each matched child's actual name (usually
// "DATA" and "MOVIES"), preserving its casing. SearchMan uses these keys to
// detect duplicate registrations and to find, remove or reprioritize a source.
// This engine does not refer to the startup keys again. Their spelling does
// not determine file-search order: priority and insertion order do. The later
// "dreamfactory-pathN" keys let PathRuntime replace a slot's search directory
// without removing these startup registrations.
//
// For the Steam release, register its single merged asset directory instead.
// Both layouts start with the logical values path(0) = "Titanic1:" and
// currentcd() = "Titanic1"; these need not name a real directory or OS volume.
// Even the merged layout needs this label: scripts still read currentcd()
// and build paths from path(0). Merging the files does not remove that script
// state, so use the normal startup label rather than "LOCAL" or an empty name.
// Scripts use path(0) to construct startup paths such as "Titanic1:local:".
// Later, BOOTFILE's setpath() selects paths for the required disc's data,
// movies and puppets. PathRuntime resolves slots 1..8 to host directories and
// registers them at a higher priority than the startup paths. Setting
// currentcd() alone does not change slot 0 or register search directories.
//
// Disc 2's retail paths are selected by those scripts, not registered here,
// though validateDiscLayout() checks required files from both CDs at startup.
// For Steam, resolveDisc() accepts either Titanic disc label and
// resolvePathDirectory() maps the scripts' paths to the same merged directory:
// the scripts retain their two-disc names without requiring a physical swap.
//
// Native verification (retail BINX TI.EXE): FUN_00439730 initializes slots 0..8
// to the executable's directory, obtained through FUN_0043d460/FUN_0043d4e0
// using GetModuleFileNameA. Startup code at 0x00437d97 reads slot 0, then
// appends "bootfile". INSTALL/32BIT/CFSETUP.INI specifies the installed layout.
Common::Error TitanicGameSupport::initializePaths(PathRuntime &paths) const {
	Common::FSNode cd1Root, flatRoot;
	if (findRepackagedDataRoot(flatRoot)) {
		SearchMan.addDirectory("dreamfactory-data", flatRoot, 0, 1, false);
	} else if (findExtractedCDRoot("Titanic1", cd1Root)) {
		SearchMan.addSubDirectoryMatching(cd1Root, "data");
		SearchMan.addSubDirectoryMatching(cd1Root, "movies");
	}
	paths.setCurrentDiscRootName("Titanic1");

	return validateDiscLayout() ? Common::kNoError : Common::kNoGameDataFoundError;
}

// Repairs the authored BOOTFILE boot script: finds the instruction pushing the
// "titanic1:" literal, walks back to its enclosing IF and neutralizes that
// whole branch so the CD check it performs no longer runs.
bool TitanicGameSupport::patchBootScript(Script &script) const {
	const uint32 count = script.getInstructionCount();
	int literal = -1;
	for (uint32 i = 0; i < count; ++i) {
		const uint16 opcode = script.getInstruction(i).opcode;
		if (opcode == Script::kOpPush3 &&
				script.getSelfRelStringRef(i).equalsIgnoreCase("titanic1:")) {
			literal = static_cast<int>(i);
			break;
		}
	}
	if (literal < 0)
		return false;

	const int ifIndex = script.findEnclosingIf(static_cast<uint32>(literal));
	if (ifIndex < 0)
		return false;

	const int endIfIndex = script.findMatchingEndIf(static_cast<uint32>(ifIndex));
	if (endIfIndex < 0)
		return false;

	script.neutralizeRange(static_cast<uint32>(ifIndex), static_cast<uint32>(endIfIndex));
	debug(0, "DreamFactory: excised Titanic boot CD check (instructions %d..%d)",
			ifIndex, endIfIndex);
	return true;
}

// Returns the canonical Titanic1/Titanic2 spelling of a requested disc label.
Common::String TitanicGameSupport::canonicalDiscLabel(const Common::String &label) const {
	return canonicalTitanicCDLabel(label);
}

// Checks whether data for the requested logical disc is available. For the
// Steam release, the merged asset directory satisfies both "Titanic1" and
// "Titanic2". Otherwise, look for an extracted disc directory whose name
// matches the request, ignoring case, near the configured game directory.
// On success, return true and set mountedLabel to the name currentcd() should
// report, using the canonical "Titanic1"/"Titanic2" spelling for those labels.
// On failure, return false and leave mountedLabel unchanged. Despite that
// parameter's name, this does not mount an OS volume, change the current disc
// state or register search paths; PathRuntime and the scripts handle those
// state/path updates separately.
bool TitanicGameSupport::resolveDisc(const Common::String &requested,
		Common::String &mountedLabel) const {
	Common::FSNode discRoot;
	if (isTitanicCDLabel(requested) && findRepackagedDataRoot(discRoot)) {
		mountedLabel = canonicalTitanicCDLabel(requested);
		return true;
	}
	if (!findExtractedCDRoot(requested, discRoot))
		return false;
	mountedLabel = canonicalTitanicCDLabel(discRoot.getName());
	return true;
}

// Resolves a colon-separated script path to an existing host directory.
// Scripts name directories in the original installation layout, while the
// launcher's game-data path may point at the common parent of the extracted
// CDs, a disc root, or its DATA directory. Try the full script path beneath
// that configured directory, its parent and its grandparent to accommodate
// those choices. Match each directory component without regard to case.
//
// For paths not starting with Titanic1/Titanic2, also try dropping the first
// component, then using just the final component. These fallbacks accommodate
// original installation prefixes not present in the user's directory layout.
// Never shorten a path explicitly naming either Titanic disc: resolving
// "Titanic2:data:" as just "data" could silently select disc 1's DATA folder.
// Try all three roots for each pattern before moving to the next fallback.
//
// The Steam release needs no such search: every path with at least one
// component resolves to its merged asset directory. Return true and set out
// when a directory is found; callers should use out only on success.
bool TitanicGameSupport::resolvePathDirectory(const Common::String &path,
		Common::FSNode &out) const {
	Common::Array<Common::String> components = splitDreamFactoryPath(path);
	if (components.empty())
		return false;

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
	Common::FSNode roots[3] = {
		gameDir, gameDir.getParent(), gameDir.getParent().getParent()
	};
	for (const Common::Array<Common::String> &pattern : patterns) {
		for (const Common::FSNode &root : roots) {
			if (findCaselessPathDir(root, pattern, out))
				return true;
		}
	}
	return false;
}

// Per-frame diagnostics for BINL.SET's cargo-painting timer: tracks when the
// authored timer starts, expires, and how much time it has left. This logging
// helps test the different gameplay paths after engine changes. Testing the
// "painting is no longer in the cargo hold" case requires waiting for the
// timer to expire. The logging also helps verify that the timer counts down
// correctly.
// Kept here so the shared scheduler stays unaware of Titanic story globals
// and props.
void TitanicGameSupport::onForceUpdate(DreamFactoryEngine &engine) {
	// BINL.SET measures this authored timer in absolute script frames. Keep its
	// diagnostics with the game policy so the shared scheduler remains unaware
	// of Titanic story globals and props.
	const ScriptVM &vm = engine.scriptVM();
	const int mission = globalIntValue(vm, "mission");
	const int phase = globalIntValue(vm, "phase");
	const int paintFrame = globalIntValue(vm, "paintframe");
	Shop::Prop *painting = engine.propRuntime().findProp("painting");
	const bool active = mission == 2 && phase == 0 && paintFrame > 0 &&
			(!painting || painting->owner.equalsIgnoreCase("none"));
	if (!active) {
		if (_cargoPaintingTimerStartFrame > 0) {
			debug(1, "DreamFactory: cargo painting timer stopped at script frame %d "
					"(mission=%d phase=%d owner='%s')",
					engine.frameCounter(), mission, phase,
					painting ? painting->owner.c_str() : "unloaded");
		}
		_cargoPaintingTimerStartFrame = 0;
		_lastCargoPaintingTimerLogBucket = -1;
		_cargoPaintingTimerExpiredLogged = false;
		return;
	}

	if (_cargoPaintingTimerStartFrame != paintFrame) {
		_cargoPaintingTimerStartFrame = paintFrame;
		_lastCargoPaintingTimerLogBucket = -1;
		_cargoPaintingTimerExpiredLogged = false;
		debug(1, "DreamFactory: cargo painting timer started at script frame %d; "
				"BINL.SET expires it when elapsed frames exceed %d",
				paintFrame, kCargoPaintingTimerFrames);
	}

	const int elapsedFrames = MAX(0, engine.frameCounter() - paintFrame);
	const int remainingFrames = kCargoPaintingTimerFrames - elapsedFrames;
	if (remainingFrames < 0) {
		if (!_cargoPaintingTimerExpiredLogged) {
			debug(1, "DreamFactory: cargo painting timer expired after %d frames; "
					"BINL.SET will give the painting to Hack on the cargo-bin click",
					elapsedFrames);
			_cargoPaintingTimerExpiredLogged = true;
		}
		return;
	}

	_cargoPaintingTimerExpiredLogged = false;
	const int frameRate = MAX(1, engine.getFrameRate());
	const int remainingSeconds = (remainingFrames * frameRate + 59) / 60;
	const int logBucket =
			(remainingSeconds + kCargoPaintingTimerLogSeconds - 1) /
			kCargoPaintingTimerLogSeconds;
	if (logBucket == _lastCargoPaintingTimerLogBucket)
		return;

	_lastCargoPaintingTimerLogBucket = logBucket;
	debug(1, "DreamFactory: cargo painting timer remaining about %d:%02d "
			"(%d/%d script frames)", remainingSeconds / 60,
			remainingSeconds % 60, remainingFrames, kCargoPaintingTimerFrames);
}

// Logs durable Titanic story state and resets load-local timer diagnostics.
void TitanicGameSupport::onGameStateLoaded(const ScriptVM &vm) {
	debug(1, "DreamFactory: load story state: mission=%d phase=%d "
			"smethphase=%d pennyphase=%d burnsphase=%d neckphase=%d "
			"paintframe=%d savedeck=%s",
			globalIntValue(vm, "mission"), globalIntValue(vm, "phase"),
			globalIntValue(vm, "smethphase"), globalIntValue(vm, "pennyphase"),
			globalIntValue(vm, "burnsphase"), globalIntValue(vm, "neckphase"),
			globalIntValue(vm, "paintframe"),
			vm.globalVars().contains("savedeck") ?
					vm.globalVars()["savedeck"].toString().c_str() : "<unset>");
	_cargoPaintingTimerStartFrame = 0;
	_lastCargoPaintingTimerLogBucket = -1;
	_cargoPaintingTimerExpiredLogged = false;
}

// Names the story globals worth tracing while scripts run.
bool TitanicGameSupport::shouldLogScriptVariable(const Common::String &name) const {
	return name == "dialmess" || name == "goodmess" || name == "countdial" ||
			name == "mission" || name == "phase" || name == "smethphase" ||
			name == "pennyphase" || name == "paintframe" || name == "savedeck" ||
			name == "burnsphase" || name == "neckphase";
}

// Filters script dispatch tracing down to the dialogue-counter and
// enigma-keypad messages this game's debugging cares about.
bool TitanicGameSupport::shouldLogScriptDispatch(const Common::String &name,
		const Common::String &self) const {
	const bool enigmaContext = self.equalsIgnoreCase("enigma.stg") ||
			self.equalsIgnoreCase("enigma 1") || self.equalsIgnoreCase("ctl 1");
	return name.equalsIgnoreCase("checkey") ||
			name.equalsIgnoreCase("advancedial") ||
			name.equalsIgnoreCase("dialset") ||
			name.equalsIgnoreCase("goodkey") ||
			name.equalsIgnoreCase("badkey") ||
			(enigmaContext && (name.equalsIgnoreCase("keydown") ||
					name.equalsIgnoreCase("keyrepeat")));
}

// Factory backing createGameSupport() for the Titanic target.
GameSupport *createTitanicGameSupport() {
	return new TitanicGameSupport();
}

} // End of namespace DreamFactory
