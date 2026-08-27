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

#include "cyberflix/cyberflix.h"
#include "cyberflix/game_support.h"
#include "cyberflix/games/titanic.h"
#include "cyberflix/runtime/paths.h"
#include "cyberflix/script.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

static const int kCargoPaintingTimerFrames = 10000;
static const int kCargoPaintingTimerLogSeconds = 10;

// Finds a child directory of @p root whose name matches @p name case-insensitively.
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

// Finds a child file of @p root whose name matches @p name case-insensitively.
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

// Walks @p components downward from @p root, matching every level
// case-insensitively, and reports the final directory in @p out.
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

// Splits a colon-separated CyberFlix path such as "titanic1:data" into its
// lowercased components.
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
static bool findRepackagedDataRoot(Common::FSNode &out) {
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
		} else if (findCaselessChildDir(gameDir, "LOCAL", local) &&
				isRepackagedDataDir(local)) {
			cachedRoot = local;
			cachedValid = true;
		}
	}

	if (cachedValid)
		out = cachedRoot;
	return cachedValid;
}

// Locates an extracted disc root named @p label near the game directory: the
// game dir itself, a child of it, its parent, or a sibling of the game dir or
// of the parent.
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
static bool validateTitanicDiscLayout() {
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

// Reads a VM global as an integer, defaulting to 0 when unset or non-numeric.
static int globalIntValue(const ScriptVM &vm, const char *name) {
	Common::HashMap<Common::String, Value>::const_iterator it = vm.globalVars().find(name);
	if (it == vm.globalVars().end())
		return 0;
	if (it->_value.type != Value::kBool && it->_value.type != Value::kInt)
		return 0;
	return it->_value.intValue;
}

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
	void onForceUpdate(CyberflixEngine &engine) override;
	void restoreGameState(CyberflixEngine &engine, const ScriptVM &vm,
			GameLoadContext &context) override;
	bool shouldLogScriptVariable(const Common::String &name) const override;
	bool shouldLogScriptDispatch(const Common::String &name,
			const Common::String &self) const override;

private:
	int _cargoPaintingTimerStartFrame = 0;
	int _lastCargoPaintingTimerLogBucket = -1;
	bool _cargoPaintingTimerExpiredLogged = false;
};

// The TI.EXE runtime executable name and Titanic's default save signature.
const GameProfile &TitanicGameSupport::profile() const {
	static const GameProfile profile = { "TI.EXE", "Titanic 1.0" };
	return profile;
}

// Mounts the discovered data directories into the search manager (the merged
// repackaged tree, or TITANIC1's DATA/MOVIES folders) and validates the layout.
Common::Error TitanicGameSupport::initializePaths(PathRuntime &paths) const {
	Common::FSNode cd1Root, flatRoot;
	if (findRepackagedDataRoot(flatRoot)) {
		SearchMan.addDirectory("cyberflix-data", flatRoot, 0, 1, false);
		paths.setCurrentDiscRootName("Titanic1");
	} else if (findExtractedCDRoot("Titanic1", cd1Root)) {
		SearchMan.addSubDirectoryMatching(cd1Root, "data");
		SearchMan.addSubDirectoryMatching(cd1Root, "movies");
		paths.setCurrentDiscRootName(cd1Root.getName());
	}

	return validateTitanicDiscLayout() ? Common::kNoError : Common::kNoGameDataFoundError;
}

// Repairs the authored BOOTFILE boot script: finds the instruction pushing the
// "titanic1:" literal, walks back to its enclosing IF and neutralizes that
// whole branch so the CD check it performs no longer runs.
bool TitanicGameSupport::patchBootScript(Script &script) const {
	const uint32 count = script.getInstructionCount();
	int literal = -1;
	for (uint32 i = 0; i < count; ++i) {
		const uint16 opcode = script.getInstruction(i).opcode;
		if ((opcode == Script::kOpPush3 || opcode == Script::kOpPush4 ||
				opcode == Script::kOpPushSym) &&
				script.getSelfRelString(i).equalsIgnoreCase("titanic1:")) {
			literal = static_cast<int>(i);
			break;
		}
	}
	if (literal < 0)
		return false;

	int ifIndex = -1;
	int depth = 0;
	for (int i = literal - 1; i >= 0; --i) {
		const uint16 opcode = script.getInstruction(static_cast<uint32>(i)).opcode;
		if (opcode == Script::kOpEndIf) {
			++depth;
		} else if (opcode == Script::kOpIf) {
			if (depth == 0) {
				ifIndex = i;
				break;
			}
			--depth;
		}
	}
	if (ifIndex < 0)
		return false;

	const int endIfIndex = script.findMatchingEndIf(static_cast<uint32>(ifIndex));
	if (endIfIndex < 0)
		return false;

	script.neutralizeRange(static_cast<uint32>(ifIndex), static_cast<uint32>(endIfIndex));
	debug(0, "Cyberflix: excised Titanic boot CD check (instructions %d..%d)",
			ifIndex, endIfIndex);
	return true;
}

// Returns the canonical Titanic1/Titanic2 spelling of a requested disc label.
Common::String TitanicGameSupport::canonicalDiscLabel(const Common::String &label) const {
	return canonicalTitanicCDLabel(label);
}

// Maps a requested disc label to what is actually mounted: in a repackaged
// install either disc label resolves to the merged tree; otherwise the matching
// extracted TITANICn folder must exist somewhere near the game directory.
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

// Resolves a colon-separated CyberFlix path to a real directory. The merged
// repackaged tree answers every path; otherwise several case-insensitive
// patterns (full path, path without the disc prefix, bare final component) are
// tried under the game dir and its two parents.
bool TitanicGameSupport::resolvePathDirectory(const Common::String &path,
		Common::FSNode &out) const {
	Common::Array<Common::String> components = splitCyberflixPath(path);
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
	for (uint p = 0; p < patterns.size(); ++p) {
		for (uint r = 0; r < ARRAYSIZE(roots); ++r) {
			if (findCaselessPathDir(roots[r], patterns[p], out))
				return true;
		}
	}
	return false;
}

// Per-frame diagnostics for BINL.SET's cargo-painting timer: tracks when the
// authored timer starts, expires, and how much time it has left. Kept here so
// the shared scheduler stays unaware of Titanic story globals and props.
void TitanicGameSupport::onForceUpdate(CyberflixEngine &engine) {
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
			debug(1, "Cyberflix: cargo painting timer stopped at script frame %d "
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
		debug(1, "Cyberflix: cargo painting timer started at script frame %d; "
				"BINL.SET expires it when elapsed frames exceed %d",
				paintFrame, kCargoPaintingTimerFrames);
	}

	const int elapsedFrames = MAX(0, engine.frameCounter() - paintFrame);
	const int remainingFrames = kCargoPaintingTimerFrames - elapsedFrames;
	if (remainingFrames < 0) {
		if (!_cargoPaintingTimerExpiredLogged) {
			debug(1, "Cyberflix: cargo painting timer expired after %d frames; "
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
	debug(1, "Cyberflix: cargo painting timer remaining about %d:%02d "
			"(%d/%d script frames)", remainingSeconds / 60,
			remainingSeconds % 60, remainingFrames, kCargoPaintingTimerFrames);
}

// Titanic-specific repairs after a save load: rebuilds the boot-time cast state
// legacy saves lost, infers their missing dialogue counters, and resumes old
// saves' cargo-painting timer at its recorded start frame.
void TitanicGameSupport::restoreGameState(CyberflixEngine &engine,
		const ScriptVM &vm, GameLoadContext &context) {
	if (context.variablesSeen) {
		debug(1, "Cyberflix: load story state: mission=%d phase=%d "
				"smethphase=%d pennyphase=%d burnsphase=%d neckphase=%d "
				"paintframe=%d savedeck=%s",
				globalIntValue(vm, "mission"), globalIntValue(vm, "phase"),
				globalIntValue(vm, "smethphase"), globalIntValue(vm, "pennyphase"),
				globalIntValue(vm, "burnsphase"), globalIntValue(vm, "neckphase"),
				globalIntValue(vm, "paintframe"),
				vm.globalVars().contains("savedeck") ?
						vm.globalVars()["savedeck"].toString().c_str() : "<unset>");
	}

	if (!context.castStatePresent) {
		// Early CyberFlix saves wrote an empty CAST chunk. Titanic keeps GANG.CST
		// open after boot and room scripts assume its global actors are available,
		// so reconstruct that boot-time state only for those legacy saves.
		engine.actorRuntime().openCastFile(engine, "gang.cst");
		Common::Array<Value> noArgs;
		engine.actorRuntime().sendToCast(engine, "gang.cst", "initactors", noArgs);

		// Those saves also lost actor dialogue counters. A story state beyond the
		// initial cabin encounter implies that Smethels has already been handled.
		const int mission = globalIntValue(vm, "mission");
		const int phase = globalIntValue(vm, "phase");
		const int smethPhase = globalIntValue(vm, "smethphase");
		if (mission > 1 || (mission == 1 && phase > 0) || smethPhase > 0)
			engine.actorRuntime().setActorValue("smeth", 1);

		if (engine.setRuntime().set() && engine.setRuntime().set()->isOpen())
			engine.sendToSet("openset", noArgs);
	}

	const int paintFrame = globalIntValue(vm, "paintframe");
	const int mission = globalIntValue(vm, "mission");
	const int phase = globalIntValue(vm, "phase");
	Shop::Prop *painting = engine.propRuntime().findProp("painting");
	// Older saves lack the absolute frame base. Resume an active cargo timer at
	// its recorded start instead of freezing it until frame() catches up.
	if ((!context.frameCounterSeen || context.frameCounter < paintFrame) &&
			mission == 2 && phase == 0 && paintFrame > 0 && painting &&
			painting->owner.equalsIgnoreCase("none"))
		context.frameCounter = paintFrame;

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

} // End of namespace Cyberflix
