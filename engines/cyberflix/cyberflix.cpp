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
#include "common/debug-channels.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/endian.h"
#include "common/keyboard.h"
#include "common/memstream.h"
#include "common/archive.h"
#include "common/system.h"
#include "common/util.h"

#include "engines/util.h"

#include "gui/message.h"

#include "audio/audiostream.h"
#include "audio/mixer.h"
#include "audio/decoders/raw.h"

#include "common/formats/winexe_pe.h"

#include "graphics/cursorman.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#ifdef USE_FREETYPE2
#include "graphics/fonts/ttf.h"
#endif
#include "graphics/palette.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"
#include "graphics/wincursor.h"

#include "cyberflix/cast.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/archive.h"
#include "cyberflix/console.h"
#include "cyberflix/image.h"
#include "cyberflix/script.h"
#include "cyberflix/set.h"
#include "cyberflix/sound.h"
#include "cyberflix/stage.h"
#include "cyberflix/vm.h"

#include <math.h>

namespace Cyberflix {

static const double kDefaultPaletteGamma = 0.65;
static const double kPaletteGammaUp = 1.05;
static const double kPaletteGammaDown = 0.9523809523809523;
static const double kPaletteGammaMin = 0.15;
static const double kPaletteGammaMax = 2.5;

static Shop::WorldCamera makeWorldCamera(const Set::CameraData &camera) {
	Shop::WorldCamera out;
	out.heading = camera.heading;
	out.cameraX = camera.cameraX;
	out.cameraY = camera.cameraY;
	out.cameraZ = camera.cameraZ;
	out.baseZ = camera.baseZ;
	out.nearPlane = camera.nearPlane;
	out.farPlane = camera.farPlane;
	out.viewportLeft = camera.viewportLeft;
	out.viewportTop = camera.viewportTop;
	out.viewportRight = camera.viewportRight;
	out.viewportBottom = camera.viewportBottom;
	out.centerX = camera.centerX;
	out.centerY = camera.centerY;
	out.focal = camera.focal;
	return out;
}

static void drawScaledCel(Graphics::Surface *screen, const CelImage &cel,
		const Common::Rect &dest, const Common::Rect &clip) {
	const int destW = dest.width();
	const int destH = dest.height();
	if (destW <= 0 || destH <= 0 || cel.width <= 0 || cel.height <= 0)
		return;
	Common::Rect paint = dest;
	paint.clip(clip);
	if (paint.isEmpty())
		return;
	for (int y = paint.top; y < paint.bottom; ++y) {
		int srcY = (int)((int64)(y - dest.top) * cel.height / destH);
		for (int x = paint.left; x < paint.right; ++x) {
			int srcX = (int)((int64)(x - dest.left) * cel.width / destW);
			if (cel.isOpaque(srcX, srcY))
				*((byte *)screen->getBasePtr(x, y)) =
						cel.pixels[(uint)srcY * cel.width + srcX];
		}
	}
}

static void drawCel(Graphics::Surface *screen, const CelImage &cel,
		const Common::Rect &dest, const Common::Rect &clip) {
	Common::Rect paint = dest;
	paint.clip(clip);
	paint.clip(Common::Rect(dest.left, dest.top, dest.left + cel.width, dest.top + cel.height));
	if (paint.isEmpty() || cel.width == 0 || cel.height == 0)
		return;

	const int copyWidth = paint.width();
	for (int y = paint.top; y < paint.bottom; ++y) {
		const int srcY = y - dest.top;
		const int srcX = paint.left - dest.left;
		const byte *src = cel.pixels.begin() + (uint)srcY * cel.width + srcX;
		const byte *opaque = cel.opaque.begin() + (uint)srcY * cel.width + srcX;
		byte *dst = (byte *)screen->getBasePtr(paint.left, y);
		for (int x = 0; x < copyWidth; ++x) {
			if (opaque[x])
				dst[x] = src[x];
		}
	}
}

static void copyFramePixelsToScreen(Graphics::Surface &screen, const byte *pixels,
		int width, int height, int dstX, int dstY) {
	if (!pixels || width <= 0 || height <= 0)
		return;

	int srcX = 0;
	int srcY = 0;
	int copyWidth = width;
	int copyHeight = height;
	if (dstX < 0) {
		srcX = -dstX;
		copyWidth -= srcX;
		dstX = 0;
	}
	if (dstY < 0) {
		srcY = -dstY;
		copyHeight -= srcY;
		dstY = 0;
	}
	if (dstX + copyWidth > screen.w)
		copyWidth = screen.w - dstX;
	if (dstY + copyHeight > screen.h)
		copyHeight = screen.h - dstY;
	if (copyWidth <= 0 || copyHeight <= 0)
		return;

	// Frame backgrounds are fully opaque. Clip once, then copy whole rows; this
	// avoids the per-pixel bounds checks in the SET transition hot path.
	for (int y = 0; y < copyHeight; ++y) {
		memcpy(screen.getBasePtr(dstX, dstY + y),
				pixels + (uint)(srcY + y) * width + srcX, copyWidth);
	}
}

static void copyFrameToScreen(Graphics::Surface &screen, const FrameImage &frame,
		int dstX, int dstY) {
	copyFramePixelsToScreen(screen, frame.pixels.begin(), frame.width,
			frame.height, dstX, dstY);
}

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

static bool resolveCyberflixPathDir(const Common::String &path, Common::FSNode &out) {
	Common::Array<Common::String> components = splitCyberflixPath(path);
	if (components.empty())
		return false;

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

static Common::String canonicalCDLabel(const Common::String &label) {
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

static bool validateTitanicDiscLayout() {
	Common::String missing;
	Common::FSNode cd1Root, cd2Root;

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


CyberflixEngine::CyberflixEngine(OSystem *syst, const CyberflixGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _rnd("cyberflix"), _console(nullptr) {
}

CyberflixEngine::~CyberflixEngine() {
	// _console is owned by the debugger registered with the engine framework.
	// _exe, _cursorCache (SharedPtr values) and _stage free themselves.
}

int CyberflixEngine::getGameType() const {
	return _gameDescription->gameType;
}

const char *CyberflixEngine::getGameId() const {
	return _gameDescription->desc.gameId;
}

Common::Language CyberflixEngine::getLanguage() const {
	return _gameDescription->desc.language;
}

Common::Platform CyberflixEngine::getPlatform() const {
	return _gameDescription->desc.platform;
}

bool CyberflixEngine::hasFeature(EngineFeature f) const {
	return (f == kSupportsReturnToLauncher) ||
			(f == kSupportsLoadingDuringRuntime) ||
			(f == kSupportsSavingDuringRuntime);
}

// The cursor bitmaps are copyrighted game art, so they are loaded at runtime
// from the player's own TI.EXE rather than shipped with ScummVM. TI.EXE is the
// CyberFlix "Bicycle" runtime; in an installed game it lives under INSTALL/BINX
// (or INSTALL/BIN). It is a Win32 PE whose RT_GROUP_CURSOR resources are named
// CURS.ARROW, CURS.HAND, CURS.GOUP, ... (see files/decomp/movie-playback.md).
Common::PEResources *CyberflixEngine::gameExe() {
	if (_exeTried)
		return _exe.get();
	_exeTried = true;

	const Common::FSNode gameDir(ConfMan.getPath("path"));
	static const char *const candidates[][3] = {
		{ "INSTALL", "BINX", "TI.EXE" },
		{ "INSTALL", "BIN", "TI.EXE" }
	};
	for (uint c = 0; c < ARRAYSIZE(candidates); ++c) {
		Common::FSNode node = gameDir.getChild(candidates[c][0])
				.getChild(candidates[c][1]).getChild(candidates[c][2]);
		if (!node.exists())
			continue;
		Common::SeekableReadStream *stream = node.createReadStream();
		if (!stream)
			continue;
		Common::ScopedPtr<Common::PEResources> exe(new Common::PEResources());
		if (exe->loadFromEXE(stream, DisposeAfterUse::YES)) {
			_exe.reset(exe.release());
			return _exe.get();
		}
		// exe (and the stream it owns) is freed as it goes out of scope.
	}
	warning("Cyberflix: could not locate TI.EXE for cursor resources");
	return nullptr;
}

bool CyberflixEngine::setGameCursor(const Common::String &name) {
	if (_activeCursor == name && _cursorCache.contains(name))
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
	debug(1, "Cyberflix: cursor -> %s", name.c_str());
	return true;
}

// openstagefile(name): open a DATA/*.STG deck. The boot script calls this for
// MAIN.STG just before sendtostage(0). Mirrors TI.EXE FUN_004090b0 (which parses
// via FUN_00409150). See files/decomp/stage-notes.md.
void CyberflixEngine::openStageFile(const Common::String &name) {
	if (name.empty())
		return;
	debug(1, "Cyberflix: openstagefile('%s') from stage '%s' flat '%s'",
			name.c_str(), currentStage().c_str(), currentFlat().c_str());
	Common::SharedPtr<Stage> stage(new Stage());
	if (!stage->open(name)) {
		debug(1, "Cyberflix: openstagefile('%s') failed", name.c_str());
		return;
	}
	clearStageShellFrame();
	_stage = stage;
	_stageVisible = true;
	debug(1, "Cyberflix: stage '%s' open (%u nodes)", name.c_str(), _stage->nodeCount());

	// The original renders the stage's current node immediately on open:
	// FUN_004090b0 parses the file (FUN_00409150), then calls the frame
	// renderer FUN_0040b180 on the current node and dispatches openstage().
	// MAIN.STG node 0 is the persistent UI shell (art-deco frame + inventory
	// bar) that room viewports later draw on top of.
	renderStageNode(0);
	Common::Array<Value> noArgs;
	sendToStage("openstage", noArgs);
	sendToFlat(currentFlat(), "openflat", noArgs);
}

void CyberflixEngine::closeStageFile() {
	if (!_stage || !_stage->isOpen())
		return;
	debug(1, "Cyberflix: closestagefile() closing stage '%s' flat '%s'",
			currentStage().c_str(), currentFlat().c_str());
	Common::Array<Value> noArgs;
	sendToFlat(currentFlat(), "closeflat", noArgs);
	sendToStage("closestage", noArgs);
	_stage.reset();
	_stageNode = 0;
	_stageVisible = false;
	clearStageShellFrame();
	blackScreen();
}

void CyberflixEngine::gotoFlat(const Value &flat) {
	if (!_stage || !_stage->isOpen())
		return;
	debug(1, "Cyberflix: gotoflat(%s) in stage '%s' flat '%s'",
			flat.toString().c_str(), currentStage().c_str(), currentFlat().c_str());
	int node = -1;
	if (flat.type == Value::kInt) {
		node = flat.intValue - 1;
	} else {
		node = _stage->findNode(flat.strValue);
	}
	if (node < 0 || (uint32)node >= _stage->nodeCount()) {
		warning("Cyberflix: gotoflat('%s') not found in stage '%s'",
				flat.toString().c_str(), _stage->name().c_str());
		return;
	}
	if (node == _stageNode)
		return;
	debug(1, "Cyberflix: gotoflat(%s) resolved node %d", flat.toString().c_str(), node);
	Common::String openedName = _stage->name();
	uint32 openedCount = _stage->nodeCount();
	Common::String oldFlat = currentFlat();
	Common::Array<Value> noArgs;
	sendToFlat(oldFlat, "closeflat", noArgs);
	if (!_stage || !_stage->isOpen() || _stage->name() != openedName ||
			_stage->nodeCount() != openedCount)
		return;
	renderStageNode(node);
	sendToFlat(currentFlat(), "openflat", noArgs);
}

Common::String CyberflixEngine::currentStage() {
	if (_stage && _stage->isOpen())
		return _stage->name();
	return "None";
}

bool CyberflixEngine::stageVisible(const bool *newVisible) {
	if (!_stage || !_stage->isOpen())
		return false;
	if (newVisible)
		_stageVisible = *newVisible;
	return _stageVisible;
}

Common::String CyberflixEngine::currentFlat() {
	if (_stage && _stage->isOpen())
		return _stage->nodeName((uint32)_stageNode);
	return "None";
}

void CyberflixEngine::clearStageShellFrame() {
	_stageShellFrame.width = 0;
	_stageShellFrame.height = 0;
	_stageShellFrame.pixels.clear();
	_stageShellFrameValid = false;
}

const FrameImage *CyberflixEngine::stageShellFrame() {
	if (!_stage || !_stage->isOpen())
		return nullptr;
	if (!_stageShellFrameValid) {
		// SET navigation redraws many room frames over the same STG node-0 shell
		// (the art-deco frame and inventory bar). Native keeps that as a backing
		// surface; caching the decoded frame here avoids re-running the STG frame
		// decompressor for every transition frame.
		if (!_stage->renderNode(0, _stageShellFrame))
			return nullptr;
		_stageShellFrameValid = true;
	}
	return &_stageShellFrame;
}

// sendtostage(message(...)): deliver a message call to the stage's script
// scope chain. Mirrors TI.EXE FUN_0040ad80, which dispatches the unevaluated
// message against [stage script, BOOTFILE res2].
void CyberflixEngine::sendToStage(const Common::String &message, const Common::Array<Value> &args) {
	if (!_stage || !_stage->isOpen()) {
		warning("Cyberflix: sendtostage('%s') with no stage open", message.c_str());
		return;
	}
	Common::SharedPtr<Stage> dispatchStage = _stage;
	dispatchWithScopes(dispatchStage->stageScript(), nullptr,
			dispatchStage->name(), Common::String(), message, args, "stage");
	refreshPropsIfDirty();
}

Value CyberflixEngine::sendToStageFx(const Common::String &message, const Common::Array<Value> &args) {
	if (!_stage || !_stage->isOpen()) {
		warning("Cyberflix: sendtostagefx('%s') with no stage open", message.c_str());
		return Value();
	}
	Common::SharedPtr<Stage> dispatchStage = _stage;
	return dispatchWithScopesValue(dispatchStage->stageScript(), nullptr,
			dispatchStage->name(), Common::String(), message, args, "stagefx");
}

// sendtoboot(message(...)): dispatch against [BOOTFILE res1, BOOTFILE res2].
// CTL.STG's QUIT button reaches BOOTFILE res1 menuselect("quit") through this
// path. Mirrors TI.EXE FUN_00439080 -> FUN_004390a0.
void CyberflixEngine::sendToBoot(const Common::String &message, const Common::Array<Value> &args) {
	if (!_bootScript) {
		warning("Cyberflix: sendtoboot('%s') before BOOTFILE loaded", message.c_str());
		return;
	}
	dispatchWithScopes(_bootScript.get(), nullptr, "bootfile", Common::String(),
			message, args, "boot");
	refreshPropsIfDirty();
}

Value CyberflixEngine::sendToBootFx(const Common::String &message, const Common::Array<Value> &args) {
	if (!_bootScript) {
		warning("Cyberflix: sendtobootfx('%s') before BOOTFILE loaded", message.c_str());
		return Value();
	}
	return dispatchWithScopesValue(_bootScript.get(), nullptr, "bootfile",
			Common::String(), message, args, "bootfx");
}

// sendtoflat(flat, message): dispatch against [node script, stage script,
// BOOTFILE res2] without changing the current node (TI.EXE FUN_0040a960).
void CyberflixEngine::sendToFlat(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!_stage || !_stage->isOpen()) {
		warning("Cyberflix: sendtoflat('%s') with no stage open", flat.c_str());
		return;
	}
	Common::SharedPtr<Stage> dispatchStage = _stage;
	int node = flat.empty() ? _stageNode : dispatchStage->findNode(flat);
	if (node < 0 || (uint32)node >= dispatchStage->nodeCount()) {
		warning("Cyberflix: stage '%s' has no flat named '%s'",
				dispatchStage->name().c_str(), flat.c_str());
		return;
	}
	Common::String flatName = dispatchStage->nodeName((uint32)node);
	dispatchWithScopes(dispatchStage->nodeScript((uint32)node),
			dispatchStage->stageScript(), flatName, flatName, message, args, "flat");
	refreshPropsIfDirty();
}

Value CyberflixEngine::sendToFlatFx(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!_stage || !_stage->isOpen()) {
		warning("Cyberflix: sendtoflatfx('%s') with no stage open", flat.c_str());
		return Value();
	}
	Common::SharedPtr<Stage> dispatchStage = _stage;
	int node = flat.empty() ? _stageNode : dispatchStage->findNode(flat);
	if (node < 0 || (uint32)node >= dispatchStage->nodeCount()) {
		warning("Cyberflix: stage '%s' has no flat named '%s'",
				dispatchStage->name().c_str(), flat.c_str());
		return Value();
	}
	Common::String flatName = dispatchStage->nodeName((uint32)node);
	return dispatchWithScopesValue(dispatchStage->nodeScript((uint32)node),
			dispatchStage->stageScript(), flatName, flatName, message, args, "flatfx");
}

// sendtobutton(flat, button, message): dispatch against [button script, node
// script, stage script, BOOTFILE res2] (TI.EXE FUN_0040a430).
void CyberflixEngine::sendToButton(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!_stage || !_stage->isOpen()) {
		warning("Cyberflix: sendtobutton('%s') with no stage open", button.c_str());
		return;
	}
	Common::SharedPtr<Stage> dispatchStage = _stage;
	int node = flat.empty() ? _stageNode : dispatchStage->findNode(flat);
	if (node < 0 || (uint32)node >= dispatchStage->nodeCount()) {
		warning("Cyberflix: stage '%s' has no flat named '%s'",
				dispatchStage->name().c_str(), flat.c_str());
		return;
	}
	if (!dispatchStage->hasButton((uint32)node, button)) {
		warning("Cyberflix: stage '%s' flat '%s' has no button named '%s'",
				dispatchStage->name().c_str(), flat.c_str(), button.c_str());
		return;
	}
	Common::Array<const Script *> scopes;
	scopes.push_back(dispatchStage->buttonScript((uint32)node, button));
	scopes.push_back(dispatchStage->nodeScript((uint32)node));
	scopes.push_back(dispatchStage->stageScript());
	debug(1, "Cyberflix: sendtobutton('%s', '%s') -> %s(%u args)",
			dispatchStage->nodeName((uint32)node).c_str(), button.c_str(),
			message.c_str(), args.size());
	dispatchWithScopeChain(scopes, button, button, message, args, "button");
	refreshPropsIfDirty();
}

Value CyberflixEngine::sendToButtonFx(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!_stage || !_stage->isOpen()) {
		warning("Cyberflix: sendtobuttonfx('%s') with no stage open", button.c_str());
		return Value();
	}
	Common::SharedPtr<Stage> dispatchStage = _stage;
	int node = flat.empty() ? _stageNode : dispatchStage->findNode(flat);
	if (node < 0 || (uint32)node >= dispatchStage->nodeCount()) {
		warning("Cyberflix: stage '%s' has no flat named '%s'",
				dispatchStage->name().c_str(), flat.c_str());
		return Value();
	}
	if (!dispatchStage->hasButton((uint32)node, button)) {
		warning("Cyberflix: stage '%s' flat '%s' has no button named '%s'",
				dispatchStage->name().c_str(), flat.c_str(), button.c_str());
		return Value();
	}
	Common::Array<const Script *> scopes;
	scopes.push_back(dispatchStage->buttonScript((uint32)node, button));
	scopes.push_back(dispatchStage->nodeScript((uint32)node));
	scopes.push_back(dispatchStage->stageScript());
	return dispatchWithScopeChainValue(scopes, button, button, message, args, "buttonfx");
}

void CyberflixEngine::renderStageNode(int node, bool resetCursor) {
	if (!_stage || !_stage->isOpen()) {
		warning("Cyberflix: sendtostage(%d) with no stage open", node);
		return;
	}
	_stageNode = node;

	FrameImage frame;
	if (!_stage->renderNode((uint32)node, frame))
		return;

	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	// Apply the stage palette only when the screen palette is live. While it
	// is black (between clut('black') and the next fade-in) the original
	// paints invisibly and the palette is brought up later by blacktoscreen.
	if (_stage->loadStagePalette(rgb) && !paletteIsBlack())
		programPalette(rgb);

	advancePropPoses();
	Graphics::Surface *screen = _system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	// Stage nodes are full-screen items: the compositor FUN_004436d0 clips
	// them against the whole screen rect DAT_00460d58, not the set viewport,
	// so they paint from the top-left corner (MAIN.STG is 512x384).
	copyFrameToScreen(*screen, frame, 0, 0);
	// CTL.STG and other flats can place screen-space SHOP props over the stage
	// with propxy()/propvisible(); the native compositor draws those display
	// items after the stage backing buffer.
	Common::Array<const Shop::Prop *> draw;
	Common::Array<const Shop *> drawShop;
	collectScreenProps(draw, drawShop);
	for (uint32 i = 0; i < draw.size(); ++i) {
		Common::SharedPtr<CelImage> cel;
		Common::Rect r;
		if (!drawShop[i]->renderProp(*draw[i], cel, r))
			continue;
		drawCel(screen, *cel, r, Common::Rect(kScreenWidth, kScreenHeight));
	}
	_system->unlockScreen();

	// FUN_0040b180 installs the default arrow for direct stage-node renders
	// (openstagefile/gotoflat). FUN_00446910 -> FUN_00423a60 compositor
	// repaints do not touch the cursor; BOOTFILE idle hittest/setcursor owns it.
	if (resetCursor && setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
	_dirtyRects.clear();
	_propsDirty = false;
	_system->updateScreen();
	if (node == 0) {
		_stageShellFrame = frame;
		_stageShellFrameValid = true;
	}

	debug(1, "Cyberflix: rendered stage '%s' node %d (%ux%u)",
			_stage->name().c_str(), node, frame.width, frame.height);
}

void CyberflixEngine::repaintDirtyStageRects() {
	if (!_stage || !_stage->isOpen() || _dirtyRects.empty())
		return;

	FrameImage frame;
	if (!_stage->renderNode((uint32)_stageNode, frame)) {
		// Repaint fallback only: preserve the cursor chosen by the script
		// hittest path, matching FUN_00442d90's lack of cursor side effects.
		renderStageNode(_stageNode, false);
		return;
	}

	advancePropPoses();
	Common::Array<const Shop::Prop *> draw;
	Common::Array<const Shop *> drawShop;
	collectScreenProps(draw, drawShop);

	Graphics::Surface *screen = _system->lockScreen();
	for (uint32 r = 0; r < _dirtyRects.size(); ++r) {
		Common::Rect dirty = _dirtyRects[r];
		dirty.clip(Common::Rect(kScreenWidth, kScreenHeight));
		if (dirty.isEmpty())
			continue;

		for (int y = dirty.top; y < dirty.bottom; ++y) {
			for (int x = dirty.left; x < dirty.right; ++x) {
				if (x < frame.width && y < frame.height)
					*((byte *)screen->getBasePtr(x, y)) = frame.pixels[(uint)y * frame.width + x];
				else
					*((byte *)screen->getBasePtr(x, y)) = 0;
			}
		}

		for (uint32 i = 0; i < draw.size(); ++i) {
			Common::SharedPtr<CelImage> cel;
			Common::Rect propRect;
			if (!drawShop[i]->renderProp(*draw[i], cel, propRect))
				continue;
			if (!dirty.intersects(propRect))
				continue;
			drawCel(screen, *cel, propRect, dirty);
		}
	}
	_system->unlockScreen();

	// Dirty stage rects are compositor work (native FUN_00442d90/FUN_00407000),
	// not direct flat navigation, so they must not overwrite the cursor selected
	// by BOOTFILE idle hittest and the prop/flat setcursor scripts.
	_dirtyRects.clear();
	_propsDirty = false;
	_system->updateScreen();
}

// opensetfile(name[, scene[, view]]): open a DATA/*.SET room file and make the
// optionally named scene/view current. The global changeset() function (BOOTFILE
// res2) calls opensetfile(setname & '.set', scenename, viewname). Mirrors TI.EXE
// FUN_00430690 (loads via FUN_004307f0 into the set-archive global DAT_00461180,
// then resolves the optional scene/view names). See files/decomp/stage-notes.md.
void CyberflixEngine::openSetFile(const Common::String &name,
		const Common::String &scene, const Common::String &view) {
	if (name.empty())
		return;

	int previousHeading = 0;
	if (_set && _set->isOpen() && _setScene >= 0) {
		Set::CameraData cameraData;
		if (_setTransitionType == kSetTransitionForward && _setTransitionResource != 0) {
			if (_set->transitionCameraData(_setTransitionResource, _setTransitionFrame, cameraData))
				previousHeading = cameraData.heading;
		} else if (_set->cameraData((uint32)_setScene, (uint32)_setTable,
				(uint32)_setAngle, cameraData)) {
			previousHeading = cameraData.heading;
		}
	}

	Common::ScopedPtr<Set> set(new Set());
	if (!set->open(name)) {
		warning("Cyberflix: opensetfile('%s') failed", name.c_str());
		return;
	}
	_set.reset(set.release());
	_setScene = -1;
	_setTable = 0;
	_setAngle = 0;
	_setView.clear();
	_setTransitionType = kSetTransitionNone;
	_setTransitionResource = 0;
	_setTransitionFrame = 0;
	_setFrameSequence.clear();
	_setVisible = true;
	debug(1, "Cyberflix: set '%s' open (%u scenes, name '%s', default scene '%s' view '%s')",
			name.c_str(), _set->sceneCount(), _set->setName().c_str(),
			_set->defaultScene().c_str(), _set->defaultView().c_str());
	refreshActorStarPositions();

	// FUN_004307f0: when no scene/view argument is given, the defaults come
	// from the set's master header (+0xa0e / +0xa1e).
	Common::String useScene = !scene.empty() ? scene : _set->defaultScene();
	Common::String useView = !view.empty() ? view : _set->defaultView();

	// The original finishes opensetfile by sending the system messages
	// (FUN_00430fa0): it runs openset() against [set script, BOOTFILE res2],
	// then (if the set did not change) runs "<scene>", openscene() through the
	// sendtoscene executor FUN_004311e0, which paints and dispatches against
	// [scene script, set script, BOOTFILE res2].
	Common::Array<Value> noArgs;
	Common::String openedName = _set->setName();
	dispatchSetMessage("openset", noArgs);

	if (_set && _set->setName() == openedName && !useScene.empty()) {
		int sceneIdx = _set->findScene(useScene);
		if (sceneIdx < 0) {
			if (_set->sceneCount() == 0) {
				warning("Cyberflix: set '%s' has no scenes", _set->name().c_str());
				return;
			}
			debug(1, "Cyberflix: set '%s' scene '%s' not found, using first scene '%s'",
					_set->name().c_str(), useScene.c_str(), _set->sceneName(0).c_str());
			sceneIdx = 0;
		}
		Common::String actualScene = _set->sceneName((uint32)sceneIdx);
		// View select (TI.EXE FUN_00433960 stores the view, FUN_004425e0 aims
		// the camera at the panorama record tagged with the view's index).
		int angle = 0;
		Common::String activeView;
		if (!useView.empty()) {
			int viewIdx = _set->findView((uint32)sceneIdx, useView);
			if (viewIdx < 0) {
				// FUN_00433960 preserves the requested view name, but if the
				// selected scene lacks it, it chooses the view whose authored
				// heading is closest to the previous camera heading before
				// calling FUN_004425e0.
				viewIdx = _set->nearestViewForHeading((uint32)sceneIdx, previousHeading);
				if (viewIdx >= 0)
					debug(1, "Cyberflix: opensetfile view '%s' not found in scene '%s', using nearest view '%s'",
							useView.c_str(), actualScene.c_str(),
							_set->viewName((uint32)sceneIdx, (uint32)viewIdx).c_str());
			}
			int viewAngle = _set->angleForView((uint32)sceneIdx, 0, viewIdx);
			if (viewAngle >= 0) {
				angle = viewAngle;
				activeView = _set->viewName((uint32)sceneIdx, (uint32)viewIdx);
			} else {
				warning("Cyberflix: opensetfile view '%s' not found in scene '%s'",
						useView.c_str(), actualScene.c_str());
			}
		}
		renderSetScene(sceneIdx, 0, angle, activeView);
		dispatchSceneMessage((uint32)sceneIdx, "openscene", noArgs);
	}
}

// closesetfile(): send the closing system messages, then drop the open set
// (TI.EXE builtin 0x2f01, core FUN_00430b20: FUN_00431050 first sends
// '"<scene>", closescene()' through the sendtoscene executor and then runs
// 'closeset()' in set scope, before FUN_00430ba0 releases the archive). The
// global closeset() calls putdownsound() which halts the room theme, and it
// switches on currentset(), so the messages must go out while the set is
// still current.
void CyberflixEngine::closeSetFile() {
	if (_set && _set->isOpen()) {
		Common::Array<Value> noArgs;
		Common::String openedName = _set->setName();
		if (_setScene >= 0)
			dispatchSceneMessage((uint32)_setScene, "closescene", noArgs);
		if (_set && _set->setName() == openedName)
			dispatchSetMessage("closeset", noArgs);
	}
	_set.reset();
	_setScene = -1;
	_setTable = 0;
	_setAngle = 0;
	_setView.clear();
	_setTransitionType = kSetTransitionNone;
	_setTransitionResource = 0;
	_setTransitionFrame = 0;
	_setFrameSequence.clear();
	_setVisible = false;
}

// currentset(): the open set's EMBEDDED name (master header +0x070, e.g.
// 'bedsit1' -- no '.set'), or 'none' (TI.EXE builtin 0x4e55 returns the set
// record's name field, copied from the header by FUN_004307f0; setupsound,
// themetype and changeset all switch/compare on this form).
Common::String CyberflixEngine::currentSet() {
	if (_set && _set->isOpen())
		return _set->setName();
	return "none";
}

// currentview(): DAT_004611dc in TI.EXE (FUN_00431ce0), or "Moving" while a
// panorama transition resource is active.
Common::String CyberflixEngine::currentView() {
	if (_setTransitionType != kSetTransitionNone)
		return "Moving";
	if (_set && _set->isOpen() && !_setView.empty())
		return _setView;
	return "none";
}

// currentscene([arg]): no-arg reads DAT_004611cc. With "left"/"right"/"strait",
// BOOTFILE res2's keydown fallback reaches TI.EXE FUN_00430c70/FUN_00442140 to
// navigate the current set; other strings are scene names to switch to.
Common::String CyberflixEngine::currentScene(const Common::String *target) {
	if (!_set || !_set->isOpen() || _setScene < 0)
		return "none";

	if (target && !target->empty()) {
		if (target->equalsIgnoreCase("left") || target->equalsIgnoreCase("right") ||
				target->equalsIgnoreCase("strait")) {
			navigateSet(*target);
		} else {
			int scene = _set->findScene(*target);
			if (scene >= 0) {
				int angle = 0;
				Common::String view = _set->defaultView();
				int viewIdx = _set->findView((uint32)scene, view);
				int viewAngle = _set->angleForView((uint32)scene, 0, viewIdx);
				if (viewAngle >= 0)
					angle = viewAngle;
				else
					view.clear();
				renderSetScene(scene, 0, angle, view);
			} else {
				warning("Cyberflix: currentscene('%s'): no such scene", target->c_str());
			}
		}
	}

	return (_set && _set->isOpen() && _setScene >= 0) ?
			_set->sceneName((uint32)_setScene) : Common::String("none");
}

int CyberflixEngine::countPaintings(const Common::String &scene, const Common::String &view) {
	if (!_set || !_set->isOpen())
		return 0;
	int sceneIdx = _set->findScene(scene);
	return sceneIdx >= 0 ? (int)_set->paintingCount((uint32)sceneIdx, view) : 0;
}

Common::String CyberflixEngine::indexToPainting(const Common::String &scene,
		const Common::String &view, int index) {
	if (!_set || !_set->isOpen() || index < 1)
		return Common::String();
	int sceneIdx = _set->findScene(scene);
	return sceneIdx >= 0 ? _set->indexToPainting((uint32)sceneIdx, view, (uint32)index) :
			Common::String();
}

bool CyberflixEngine::roadAhead(const Common::String &scene, const Common::String &view) {
	if (!_set || !_set->isOpen())
		return false;
	int sceneIdx = _set->findScene(scene);
	if (sceneIdx < 0)
		return false;
	int viewIdx = _set->findView((uint32)sceneIdx, view);
	return _set->forwardTransitionForView((uint32)sceneIdx, viewIdx) != 0;
}

bool CyberflixEngine::setVisible(const bool *newVisible) {
	if (!_set || !_set->isOpen())
		return false;
	if (newVisible) {
		bool wasVisible = _setVisible;
		_setVisible = *newVisible;
		if (_setVisible) {
			if (!wasVisible && _setScene >= 0)
				renderSetScene(_setScene, _setTable, _setAngle, _setView);
			else
				_propsDirty = true;
		} else {
			_setTransitionType = kSetTransitionNone;
			_propsDirty = false;
			if (_stage && _stage->isOpen())
				renderStageNode(_stageNode, false);
		}
	}
	return _setVisible;
}

Common::String CyberflixEngine::currentPuppet() {
	return (_puppet && _puppet->isOpen()) ? _puppet->sourceName() : Common::String("none");
}

// ---- Puppet subsystem (TI.EXE FUN_004473c0 and friends) --------------------
// RE notes: files/decomp/stage-notes.md. This models the verified PUP archive
// lifetime, script table, and palette state used by C73 Smethels. The native
// compositor branch (FUN_00448a60), speech runner (FUN_00447ce0/FUN_00448b60),
// and bevel queue/event path (FUN_00447b30/FUN_00449370/FUN_00449e40).

void CyberflixEngine::openPuppetFile(const Common::String &name) {
	if (name.empty())
		return;
	if (_puppet && _puppet->isOpen()) {
		warning("Cyberflix: openpuppetfile('%s'): puppet already open", name.c_str());
		return;
	}

	Common::String key = name;
	key.toLowercase();
	Common::SharedPtr<Puppet> puppet(new Puppet());
	if (!puppet->open(key))
		return;

	_puppet = puppet;
	_puppetVisible = true; // FUN_00447470 sets DAT_00461202 = 1.
	_puppetCurrentAction.clear();
	_puppetCurrentFrame = 0;
	_puppetBevels.clear();
	debug(1, "Cyberflix: puppet '%s' open", _puppet->sourceName().c_str());
}

void CyberflixEngine::closePuppetFile() {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: closepuppetfile(): no puppet open");
		return;
	}
	debug(1, "Cyberflix: puppet '%s' closed", _puppet->sourceName().c_str());
	_mixer->stopHandle(_puppetSpeechHandle);
	_puppet.reset();
	_puppetVisible = false;
	_puppetBase.clear();
	_puppetCurrentAction.clear();
	_puppetCurrentFrame = 0;
	_puppetBevels.clear();
}

void CyberflixEngine::sendToPuppet(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtopuppet('%s') -> %s(%u args)", puppetName.c_str(),
			message.c_str(), args.size());
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: sendtopuppet('%s'): no puppet open", puppetName.c_str());
		return;
	}

	Common::SharedPtr<Script> script = _puppet->scriptByName(puppetName);
	if (!script) {
		warning("Cyberflix: sendtopuppet('%s'): no such puppet script", puppetName.c_str());
		return;
	}

	// PUP script lookups are immutable while the puppet file is open. The hot
	// Smethels path repeatedly dispatches one-scope puppet messages, so use the
	// fixed helper instead of building a transient scope-chain array each time.
	dispatchWithScopes(script.get(), nullptr, _puppet->sourceName(), Common::String(),
			message, args, "puppet");
	refreshPropsIfDirty();
}

Value CyberflixEngine::sendToPuppetFx(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtopuppetfx('%s') -> %s(%u args)", puppetName.c_str(),
			message.c_str(), args.size());
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: sendtopuppetfx('%s'): no puppet open", puppetName.c_str());
		return Value();
	}

	Common::SharedPtr<Script> script = _puppet->scriptByName(puppetName);
	if (!script) {
		warning("Cyberflix: sendtopuppetfx('%s'): no such puppet script", puppetName.c_str());
		return Value();
	}

	return dispatchWithScopesValue(script.get(), nullptr, _puppet->sourceName(),
			Common::String(), message, args, "puppetfx");
}

void CyberflixEngine::puppetScript(const Common::String &name) {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: puppetscript('%s'): no puppet open", name.c_str());
		return;
	}
	if (!_puppet->scriptByName(name)) {
		warning("Cyberflix: puppetscript('%s'): no such puppet script", name.c_str());
		return;
	}
	debug(1, "Cyberflix: puppetscript('%s')", name.c_str());
}

void CyberflixEngine::puppetClear() {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: puppetclear(): no puppet open");
		return;
	}
	_puppetBevels.clear();
	renderCurrentPuppetFrame(true);
	debug(1, "Cyberflix: puppetclear()");
}

void CyberflixEngine::puppetSpeak(const Common::String &name, int mode) {
	if (!_puppet || !_puppet->isOpen() || !_puppetVisible) {
		warning("Cyberflix: puppetspeak('%s'): no visible puppet", name.c_str());
		return;
	}
	const Puppet::ActionEntry *action = _puppet->actionByName(name);
	if (!action) {
		warning("Cyberflix: puppetspeak('%s'): no such action", name.c_str());
		return;
	}
	debug(1, "Cyberflix: puppetspeak('%s', %d)", name.c_str(), mode);
	playPuppetAction(*action);
}

void CyberflixEngine::puppetBevel(const Common::String &name, int mode) {
	if (!_puppet || !_puppet->isOpen() || !_puppetVisible) {
		warning("Cyberflix: puppetbevel('%s'): no visible puppet", name.c_str());
		return;
	}
	PuppetBevelOption option;
	option.text = name;
	option.id = mode;
	const int top = kScreenHeight + ((int)_puppetBevels.size() - 5) * 24;
	option.rect = Common::Rect(0, top, kScreenWidth, top + 24);
	_puppetBevels.push_back(option);
	renderPuppetBevels(true);
	debug(1, "Cyberflix: puppetbevel('%s', %d)", name.c_str(), mode);
}

void CyberflixEngine::puppetGrab(bool enabled) {
	_puppetGrab = enabled;
	debug(1, "Cyberflix: puppetgrab(%s)", enabled ? "true" : "false");
}

int CyberflixEngine::puppetEvent(int timeout) {
	if (!_puppet || !_puppet->isOpen() || !_puppetVisible) {
		warning("Cyberflix: puppetevent(%d): no visible puppet", timeout);
		return -1;
	}
	renderCurrentPuppetFrame(true);
	if (_puppetBevels.empty())
		return -1;

	setGameCursor("CURS.ARROW");
	CursorMan.showMouse(true);
	int hoverState = -1;
	const uint32 start = _system->getMillis();
	Common::Event event;
	for (;;) {
		if (shouldQuit())
			return -1;
		if (timeout >= 0 && _system->getMillis() - start >= (uint32)timeout)
			return -1;

		Common::Point mouse = _eventMan->getMousePos();
		int hover = 0;
		for (uint i = 0; i < _puppetBevels.size(); ++i) {
			if (_puppetBevels[i].rect.contains(mouse)) {
				hover = 1;
				break;
			}
		}
		if (hover != hoverState) {
			setGameCursor(hover ? "CURS131" : "CURS.ARROW");
			hoverState = hover;
		}

		while (_eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN &&
					event.kbd.keycode == Common::KEYCODE_ESCAPE)
				return -1;
			if (event.type != Common::EVENT_LBUTTONDOWN)
				continue;
			mouse = _eventMan->getMousePos();
			for (uint i = 0; i < _puppetBevels.size(); ++i) {
				if (!_puppetBevels[i].rect.contains(mouse))
					continue;
				const int id = _puppetBevels[i].id;
				debug(1, "Cyberflix: puppetevent(%d) click (%d,%d) -> %d",
						timeout, mouse.x, mouse.y, id);
				_puppetBevels.clear();
				renderCurrentPuppetFrame(true);
				return id;
			}
		}
		_system->updateScreen();
		_system->delayMillis(10);
	}
}

Common::String CyberflixEngine::puppetBase(const Common::String *newBase) {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: puppetbase(): no puppet open");
		return Common::String();
	}
	if (newBase) {
		if (newBase->size() > 31) {
			warning("Cyberflix: puppetbase('%s'): name too long", newBase->c_str());
			return _puppetBase;
		}
		_puppetBase = *newBase;
		_puppetBase.toLowercase();
		_puppetCurrentAction = _puppetBase;
		_puppetCurrentFrame = 0;
		debug(1, "Cyberflix: puppetbase('%s')", _puppetBase.c_str());
	}
	return _puppetBase;
}

bool CyberflixEngine::puppetVisible(const bool *newVisible) {
	if (!_puppet || !_puppet->isOpen())
		return false;
	if (newVisible) {
		_puppetVisible = *newVisible;
		if (_puppetVisible)
			renderCurrentPuppetFrame(true);
		debug(1, "Cyberflix: puppetvisible(%s)", _puppetVisible ? "true" : "false");
	}
	return _puppetVisible;
}

const Puppet::ActionEntry *CyberflixEngine::currentPuppetAction() const {
	if (!_puppet || !_puppet->isOpen())
		return nullptr;
	if (!_puppetCurrentAction.empty()) {
		if (const Puppet::ActionEntry *action = _puppet->actionByName(_puppetCurrentAction))
			return action;
	}
	if (!_puppetBase.empty()) {
		if (const Puppet::ActionEntry *action = _puppet->actionByName(_puppetBase))
			return action;
	}
	return _puppet->actionAt(0);
}

static void copyPuppetGrabBackdropToScreen(const Common::Array<byte> &backdrop,
		Graphics::Surface &screen) {
	if (backdrop.size() != kScreenWidth * kScreenHeight)
		return;
	copyFramePixelsToScreen(screen, backdrop.begin(), kScreenWidth, kScreenHeight, 0, 0);
}

bool CyberflixEngine::capturePuppetGrabBackdrop(Common::Array<byte> &backdrop) {
	if (!_puppetGrab) {
		backdrop.clear();
		return false;
	}

	backdrop.resize(kScreenWidth * kScreenHeight);
	memset(backdrop.begin(), 0, backdrop.size());

	if (_setVisible && _set && _set->isOpen() && _setScene >= 0) {
		// TI.EXE FUN_00449150 copies from the retained SET backing surface
		// (0x486770). ScummVM keeps that same surface in _setFrameSequence, so
		// reuse it for puppetgrab instead of replaying the compressed panorama
		// once per puppet action. Fall back to rendering only if a save/load or
		// startup edge case reaches here before the retained surface exists.
		FrameImage frame;
		const byte *pixels = nullptr;
		uint16 frameWidth = 0;
		uint16 frameHeight = 0;
		if (!_setFrameSequence.empty()) {
			pixels = _setFrameSequence.pixels();
			frameWidth = _setFrameSequence.width();
			frameHeight = _setFrameSequence.height();
		} else if (_set->renderScene((uint32)_setScene, (uint32)_setTable,
				(uint32)_setAngle, _setFrameSequence, frame)) {
			pixels = frame.pixels.begin();
			frameWidth = frame.width;
			frameHeight = frame.height;
		}
		if (pixels) {
			// The native grab does not include stage/inventory-bar composite
			// pixels or SHOP props; it copies just the current SET backing rect.
			const int x0 = _set->viewLeft();
			const int y0 = _set->viewTop();
			int left = x0;
			int srcX = 0;
			int width = frameWidth;
			if (left < 0) {
				srcX = -left;
				width -= srcX;
				left = 0;
			}
			if (left + width > kScreenWidth)
				width = kScreenWidth - left;
			if (width > 0) {
				for (int y = 0; y < frameHeight; ++y) {
					const int sy = y0 + y;
					if (sy >= 0 && sy < kScreenHeight) {
						memcpy(backdrop.begin() + (uint)sy * kScreenWidth + left,
								pixels + (uint)y * frameWidth + srcX,
								width);
					}
				}
			}
		}
		return true;
	}

	if (_stage && _stage->isOpen()) {
		FrameImage frame;
		if (_stage->renderNode((uint32)_stageNode, frame)) {
			const int width = MIN<int>(frame.width, kScreenWidth);
			const int height = MIN<int>(frame.height, kScreenHeight);
			for (int y = 0; y < height; ++y) {
				memcpy(backdrop.begin() + (uint)y * kScreenWidth,
						frame.pixels.begin() + (uint)y * frame.width, width);
			}
		}
	}

	return true;
}

bool CyberflixEngine::paintPuppetGrabBackdrop(Graphics::Surface &screen,
		const Common::Array<byte> *cachedBackdrop) {
	if (!_puppetGrab)
		return false;

	Common::Array<byte> freshBackdrop;
	const Common::Array<byte> *backdrop = cachedBackdrop;
	if (!backdrop) {
		if (!capturePuppetGrabBackdrop(freshBackdrop))
			return false;
		backdrop = &freshBackdrop;
	}

	copyPuppetGrabBackdropToScreen(*backdrop, screen);
	return true;
}

bool CyberflixEngine::renderPuppetFrame(const Puppet::ActionEntry &action,
		uint32 frameIndex, bool present, const Common::Array<byte> *cachedBackdrop) {
	Graphics::Surface *screen = _system->lockScreen();
	const bool backdropPainted = paintPuppetGrabBackdrop(*screen, cachedBackdrop);
	if (!backdropPainted)
		screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	const bool drew = _puppet->renderActionFrame(action, frameIndex, *screen, _puppetGrab);
	_system->unlockScreen();
	renderPuppetBevels(false);
	if (present)
		_system->updateScreen();
	return drew;
}

bool CyberflixEngine::renderCurrentPuppetFrame(bool present) {
	const Puppet::ActionEntry *action = currentPuppetAction();
	if (!action)
		return false;
	return renderPuppetFrame(*action, _puppetCurrentFrame, present);
}

const Graphics::Font *CyberflixEngine::textFont(int size) {
#ifdef USE_FREETYPE2
	const bool antialiasing = ConfMan.hasKey(CYBERFLIX_OPTION_FONT_ANTIALIASING) &&
			ConfMan.getBool(CYBERFLIX_OPTION_FONT_ANTIALIASING);
	if (_nativeTextFont && _nativeTextFontSize == size &&
			_nativeTextFontAntialiasing == antialiasing)
		return _nativeTextFont.get();

	_nativeTextFont.reset();
	_nativeTextFontSize = size;
	_nativeTextFontAntialiasing = antialiasing;

	const Graphics::TTFRenderMode renderMode = antialiasing
			? Graphics::kTTFRenderModeLight
			: Graphics::kTTFRenderModeMonochrome;

	static const char *const arialNames[] = {
		"arial.ttf",
		"Arial.ttf",
		"ARIAL.TTF",
		nullptr
	};

	// TI.EXE creates "Arial" with CreateFontA(). For the closest match, put
	// Microsoft's Arial.ttf directly in the configured game root, or set this
	// target's ScummVM Extra Path to a directory containing Arial.ttf. Otherwise
	// we use the bundled Liberation Sans fallback, as other ScummVM engines do
	// for Arial-like text.
	for (const char *const *name = arialNames; *name; ++name) {
		Common::SeekableReadStream *stream = SearchMan.createReadStreamForMember(
				Common::Path(*name, Common::Path::kNoSeparator));
		if (!stream)
			continue;
		_nativeTextFont.reset(Graphics::loadTTFFont(stream, DisposeAfterUse::YES,
				size, Graphics::kTTFSizeModeCharacter, 0, 0, renderMode));
		if (_nativeTextFont)
			return _nativeTextFont.get();
	}

	_nativeTextFont.reset(Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf",
			size, Graphics::kTTFSizeModeCharacter, 0, 0, renderMode));
	if (_nativeTextFont)
		return _nativeTextFont.get();
#endif

	const Graphics::Font *font = size >= 12
			? FontMan.getFontByUsage(Graphics::FontManager::kGUIFont)
			: FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);
	return font;
}

void CyberflixEngine::renderPuppetBevels(bool present) {
	if (!_puppet || !_puppet->isOpen())
		return;

	Graphics::Surface *screen = _system->lockScreen();
	// FUN_00449370 always draws the PUP master bevel backdrop before checking
	// the queued bevel count, so Smethels' text-row panel stays visible even
	// during speech beats that offer no clickable choices.
	_puppet->renderBevelBackdrop(*screen, kScreenHeight, kScreenWidth);

	const Graphics::Font *font = textFont(_puppetParams[5]);
	if (font) {
		for (uint i = 0; i < _puppetBevels.size(); ++i) {
			const Common::Rect &rect = _puppetBevels[i].rect;
			Common::Rect clipped = rect;
			clipped.clip(Common::Rect(kScreenWidth, kScreenHeight));
			if (clipped.isEmpty())
				continue;
			const int baselineY = rect.top + 0x10;
			const int x = rect.left + _puppetParams[9];
			font->drawString(screen, _puppetBevels[i].text, x, baselineY - font->getFontAscent(),
					kScreenWidth - x, (uint32)CLIP<int>(_puppetParams[2], 0, 255));
		}
	}
	_system->unlockScreen();
	if (present)
		_system->updateScreen();
}

void CyberflixEngine::playPuppetAction(const Puppet::ActionEntry &action) {
	_puppetCurrentAction = action.name;
	_puppetCurrentFrame = 0;
	_puppetBevels.clear();
	_mixer->stopHandle(_puppetSpeechHandle);

	Common::Array<byte> pcm;
	_puppet->decodeActionAudio(action, pcm);
	if (!pcm.empty()) {
		byte *buf = (byte *)malloc(pcm.size());
		if (buf) {
			memcpy(buf, pcm.begin(), pcm.size());
			Audio::SeekableAudioStream *stream = Audio::makeRawStream(
					buf, pcm.size(), kAudioSampleRate, Audio::FLAG_UNSIGNED,
					DisposeAfterUse::YES);
			if (stream) {
				_mixer->playStream(Audio::Mixer::kSpeechSoundType, &_puppetSpeechHandle, stream);
				_mixer->setChannelVolume(_puppetSpeechHandle, effectiveAudioVolume(255));
			} else {
				free(buf);
			}
		}
	}

	const uint32 frameCount = action.frameCount ? action.frameCount : 1;
	uint32 lastFrame = (uint32)-1;
	uint32 lastPresentedFrame = (uint32)-1;
	const uint32 wallStart = _system->getMillis();
	// Puppet speech redraws animation frames at native 30 fps against the same
	// puppetgrab backdrop. Cache that grabbed SET/STG image once per action so
	// each frame only restores a memory copy instead of re-decoding the room
	// background and locking/unlocking the backend twice.
	Common::Array<byte> grabBackdrop;
	const Common::Array<byte> *cachedBackdrop =
			capturePuppetGrabBackdrop(grabBackdrop) ? &grabBackdrop : nullptr;
	Common::Event event;
	const uint32 kPuppetActionPollCapMs = 33; // one native 30 fps puppet frame
	for (;;) {
		if (shouldQuit())
			break;
		uint32 elapsed = _mixer->isSoundHandleActive(_puppetSpeechHandle)
				? _mixer->getSoundElapsedTime(_puppetSpeechHandle)
				: (_system->getMillis() - wallStart);
		uint32 frame = (uint32)((uint64)elapsed * 60 / 1000 / 2);
		if (frame >= frameCount)
			frame = frameCount - 1;
		if (frame != lastFrame) {
			_puppetCurrentFrame = frame;
			if (lastPresentedFrame == (uint32)-1 ||
					!_puppet->actionFramesVisuallyEqual(action, lastPresentedFrame, frame, _puppetGrab)) {
				renderPuppetFrame(action, frame, true, cachedBackdrop);
				lastPresentedFrame = frame;
			}
			lastFrame = frame;
		}
		bool aborted = false;
		while (_eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN &&
					event.kbd.keycode == Common::KEYCODE_ESCAPE) {
				_mixer->stopHandle(_puppetSpeechHandle);
				aborted = true;
				break;
			}
		}
		if (aborted)
			break;
		if (!_mixer->isSoundHandleActive(_puppetSpeechHandle)) {
			if (pcm.empty() && frame + 1 < frameCount) {
				const uint32 nextFrameMs = (uint32)(((uint64)frame + 1) * 1000 + 29) / 30;
				// Silent puppet actions are clocked from wall time at the same
				// 30 fps cadence as speech. Sleep toward the next frame boundary
				// instead of waking the backend event pump several times per
				// frame; there is no cursor tracking during action playback.
				_system->delayMillis(nextFrameMs > elapsed ?
						MIN<uint32>(nextFrameMs - elapsed, kPuppetActionPollCapMs) : 1);
				continue;
			}
			break;
		}
		const uint32 nextFrameMs = (uint32)(((uint64)frame + 1) * 1000 + 29) / 30;
		// Speech playback is frame-clocked from the mixer at native 30 fps.
		// Poll Esc at that same cadence, avoiding extra SDL/Cocoa event-pump
		// wakeups between frames when the picture cannot change.
		_system->delayMillis(nextFrameMs > elapsed ?
				MIN<uint32>(nextFrameMs - elapsed, kPuppetActionPollCapMs) : 1);
	}
	_puppetCurrentFrame = frameCount - 1;
	// If playback timing already presented the final visual frame, avoid one
	// redundant backend swap at the end of the action.
	if (lastPresentedFrame == (uint32)-1 ||
			!_puppet->actionFramesVisuallyEqual(action, lastPresentedFrame, _puppetCurrentFrame, _puppetGrab))
		renderPuppetFrame(action, _puppetCurrentFrame, true, cachedBackdrop);
}

int CyberflixEngine::puppetParam(int selector, const int *newValue) {
	if (selector < 1 || selector > (int)ARRAYSIZE(_puppetParams)) {
		warning("Cyberflix: puppetparam(%d): selector out of range", selector);
		return 0;
	}
	int16 &slot = _puppetParams[selector - 1];
	if (newValue) {
		slot = (int16)*newValue;
		debug(1, "Cyberflix: puppetparam(%d, %d)", selector, *newValue);
	}
	return slot;
}

int CyberflixEngine::countPuppets() {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: countpuppets(): no puppet open");
		return 0;
	}
	return (int)_puppet->scriptCount();
}

Common::String CyberflixEngine::indexToPuppet(int index) {
	if (!_puppet || !_puppet->isOpen()) {
		warning("Cyberflix: indextopuppet(%d): no puppet open", index);
		return Common::String();
	}
	if (index < 1 || (uint32)index > _puppet->scriptCount()) {
		warning("Cyberflix: indextopuppet(%d): index out of range", index);
		return Common::String();
	}
	return _puppet->scriptName((uint32)index - 1);
}

// ---- Cast/actor subsystem (TI.EXE FUN_0041f1c0 and friends) ---------------
// RE notes: files/decomp/stage-notes.md. The original keeps one global actor
// array (DAT_0046112c/DAT_00461130) across all open casts; lookups and
// countactors/indextoactor therefore span _casts in open order.

void CyberflixEngine::collectScreenProps(Common::Array<const Shop::Prop *> &draw,
		Common::Array<const Shop *> &drawShop) {
	for (uint32 s = 0; s < _shops.size(); ++s) {
		// Native FUN_0042ba40 walks the global SHOP prop array; replacement
		// stages rely on scripts to hide stale props and re-show live overlays.
		for (uint32 i = 0; i < _shops[s]->propCount(); ++i) {
			const Shop::Prop &p = _shops[s]->prop(i);
			if (p.visible && p.mode == 0) {
				draw.push_back(&p);
				drawShop.push_back(_shops[s].get());
			}
		}
	}
	// Native FUN_004434f0 sorts larger signed depths first, then hittest walks
	// the display list backward. This leaves more-negative screen props on top.
	for (uint32 i = 1; i < draw.size(); ++i) {
		const Shop::Prop *p = draw[i];
		const Shop *sh = drawShop[i];
		uint32 j = i;
		for (; j > 0 && draw[j - 1]->depth < p->depth; --j) {
			draw[j] = draw[j - 1];
			drawShop[j] = drawShop[j - 1];
		}
		draw[j] = p;
		drawShop[j] = sh;
	}
}

void CyberflixEngine::advancePropPoses() {
	for (uint32 i = 0; i < _shops.size(); ++i)
		_shops[i]->advancePropPoses();
}

bool CyberflixEngine::hasAnimatedScreenProps() const {
	for (uint32 s = 0; s < _shops.size(); ++s) {
		for (uint32 i = 0; i < _shops[s]->propCount(); ++i) {
			const Shop::Prop &p = _shops[s]->prop(i);
			if (p.visible && p.mode == 0 && p.poseCount > 1)
				return true;
		}
	}
	return false;
}

void CyberflixEngine::collectWorldProps(Common::Array<const Shop::Prop *> &draw,
		Common::Array<const Shop *> &drawShop, Common::Array<int16> &depths,
		const Shop::WorldCamera &camera) {
	if (!_set || !_set->isOpen())
		return;
	const Common::String &setName = _set->setName();
	for (uint32 s = 0; s < _shops.size(); ++s) {
		for (uint32 i = 0; i < _shops[s]->propCount(); ++i) {
			const Shop::Prop &p = _shops[s]->prop(i);
			if (!p.visible || p.mode == 0 || !p.setName.equalsIgnoreCase(setName))
				continue;
			Common::SharedPtr<CelImage> cel;
			Common::Rect rect;
			int16 depth = 0;
			if (!_shops[s]->renderWorldProp(p, camera, setName, cel, rect, depth))
				continue;
			draw.push_back(&p);
			drawShop.push_back(_shops[s].get());
			depths.push_back(depth);
		}
	}

	for (uint32 i = 1; i < draw.size(); ++i) {
		const Shop::Prop *p = draw[i];
		const Shop *sh = drawShop[i];
		int16 depth = depths[i];
		uint32 j = i;
		for (; j > 0 && depths[j - 1] < depth; --j) {
			draw[j] = draw[j - 1];
			drawShop[j] = drawShop[j - 1];
			depths[j] = depths[j - 1];
		}
		draw[j] = p;
		drawShop[j] = sh;
		depths[j] = depth;
	}
}

void CyberflixEngine::collectWorldActors(Common::Array<const Cast::Actor *> &draw,
		Common::Array<const Cast *> &drawCast, Common::Array<int16> &depths,
		const Shop::WorldCamera &camera) {
	if (!_set || !_set->isOpen())
		return;
	const Common::String &setName = _set->setName();
	for (uint32 c = 0; c < _casts.size(); ++c) {
		for (uint32 i = 0; i < _casts[c]->actorCount(); ++i) {
			const Cast::Actor &actor = _casts[c]->actor(i);
			if (!actor.visible || !actor.setName.equalsIgnoreCase(setName))
				continue;
			CelImage cel;
			Common::Rect rect;
			int16 depth = 0;
			if (!_casts[c]->renderWorldActor(actor, camera, setName, cel, rect, depth))
				continue;
			draw.push_back(&actor);
			drawCast.push_back(_casts[c].get());
			depths.push_back(depth);
		}
	}

	for (uint32 i = 1; i < draw.size(); ++i) {
		const Cast::Actor *actor = draw[i];
		const Cast *cast = drawCast[i];
		int16 depth = depths[i];
		uint32 j = i;
		for (; j > 0 && depths[j - 1] < depth; --j) {
			draw[j] = draw[j - 1];
			drawCast[j] = drawCast[j - 1];
			depths[j] = depths[j - 1];
		}
		draw[j] = actor;
		drawCast[j] = cast;
		depths[j] = depth;
	}
}

bool CyberflixEngine::screenPropRect(const Shop &shop, const Shop::Prop &prop, Common::Rect &rect) const {
	if (!prop.visible || prop.mode != 0)
		return false;

	Common::SharedPtr<CelImage> cel;
	if (!shop.renderProp(prop, cel, rect))
		return false;
	rect.clip(Common::Rect(kScreenWidth, kScreenHeight));
	return !rect.isEmpty();
}

void CyberflixEngine::queueDirtyRect(const Common::Rect &rect) {
	Common::Rect clipped = rect;
	clipped.clip(Common::Rect(kScreenWidth, kScreenHeight));
	if (clipped.isEmpty())
		return;

	for (uint32 i = 0; i < _dirtyRects.size(); ++i) {
		if (_dirtyRects[i].intersects(clipped)) {
			_dirtyRects[i].extend(clipped);
			return;
		}
	}
	_dirtyRects.push_back(clipped);
}

void CyberflixEngine::markPropDirty(const Shop &shop, const Shop::Prop &prop, const Common::Rect *oldRect) {
	if (oldRect)
		queueDirtyRect(*oldRect);
	Common::Rect newRect;
	if (screenPropRect(shop, prop, newRect))
		queueDirtyRect(newRect);
	_propsDirty = true;
}

void CyberflixEngine::markShopDirty(const Shop &shop) {
	for (uint32 i = 0; i < shop.propCount(); ++i) {
		Common::Rect rect;
		if (screenPropRect(shop, shop.prop(i), rect))
			queueDirtyRect(rect);
	}
	_propsDirty = true;
}

static bool isReplacementStage(const Common::SharedPtr<Stage> &stage) {
	return stage && stage->isOpen() && !stage->name().equalsIgnoreCase("main.stg");
}

// hittest(point) -> TI.EXE FUN_00435e70. The point packs (x << 16) | y; the
// in-rect helper FUN_0041ac60 checks the high word against the {t,l,b,r}
// rect's left/right and the low word against top/bottom. Probe order:
//  1. Sprite items, topmost first: FUN_004430f0 walks the compositor display
//     list backwards, rect test then a per-pixel test through the cel's
//     transparency mask (screen items via FUN_0043bb90). A hit is classified
//     actor (FUN_00422ff0) -> "actor" or prop (FUN_0042c150) -> "prop"; the
//     cast subsystem is pending, so only props can match here yet.
//  2. Open set, point inside its viewport rect (FUN_00443290): painting hit
//     FUN_004329f0 -> "painting" (paintings pending), else "scene" with the
//     current scene name (DAT_004611cc).
//  3. Open stage, point inside the stage rect (FUN_00443250; the full screen
//     — FUN_0043b610 sets DAT_00460d58 = {0,0,screenH,screenW}): button hit
//     FUN_0040af40 -> "button" (stage buttons pending), else "flat" with the
//     current node's name (node record +0x1e).
//  4. Fallback: kind "None" (DAT_00457568 — capitalised in the EXE; script
//     compares are case-insensitive), empty name (DAT_00459c98).
Common::String CyberflixEngine::hitTest(int32 packedPoint) {
	const int16 x = (int16)(packedPoint >> 16);
	const int16 y = (int16)(packedPoint & 0xffff);

	Common::Array<const Shop::Prop *> draw;
	Common::Array<const Shop *> drawShop;
	collectScreenProps(draw, drawShop);
	for (int i = (int)draw.size() - 1; i >= 0; --i) {
		Common::SharedPtr<CelImage> cel;
		Common::Rect r;
		if (!drawShop[i]->renderProp(*draw[i], cel, r))
			continue;
		if (x < r.left || x >= r.right || y < r.top || y >= r.bottom)
			continue;
		if (!cel->isOpaque(x - r.left, y - r.top))
			continue;
		_hitKind = "prop";
		return draw[i]->name;
	}

	if (_setVisible && _set && _set->isOpen() && _setScene >= 0 &&
			_setTransitionType == kSetTransitionNone) {
		Set::CameraData cameraData;
		if (_set->cameraData((uint32)_setScene, (uint32)_setTable,
				(uint32)_setAngle, cameraData)) {
			Shop::WorldCamera camera = makeWorldCamera(cameraData);
			Common::Array<const Shop::Prop *> worldDraw;
			Common::Array<const Shop *> worldShop;
			Common::Array<int16> worldDepths;
			Common::Array<const Cast::Actor *> actorDraw;
			Common::Array<const Cast *> actorCast;
			Common::Array<int16> actorDepths;
			collectWorldProps(worldDraw, worldShop, worldDepths, camera);
			collectWorldActors(actorDraw, actorCast, actorDepths, camera);
			Common::Array<byte> itemType;
			Common::Array<uint32> itemIndex;
			uint32 propIndex = 0, actorIndex = 0;
			while (propIndex < worldDraw.size() || actorIndex < actorDraw.size()) {
				const bool useActor = actorIndex < actorDraw.size() &&
						(propIndex >= worldDraw.size() || actorDepths[actorIndex] >= worldDepths[propIndex]);
				itemType.push_back(useActor ? 1 : 0);
				itemIndex.push_back(useActor ? actorIndex++ : propIndex++);
			}
			for (int i = (int)itemType.size() - 1; i >= 0; --i) {
				const CelImage *cel = nullptr;
				CelImage actorCel;
				Common::SharedPtr<CelImage> propCel;
				Common::Rect r;
				int16 depth = 0;
				Common::String name;
				if (itemType[(uint)i]) {
					uint32 idx = itemIndex[(uint)i];
					if (!actorCast[idx]->renderWorldActor(*actorDraw[idx], camera,
							_set->setName(), actorCel, r, depth))
						continue;
					cel = &actorCel;
					name = actorDraw[idx]->name;
				} else {
					uint32 idx = itemIndex[(uint)i];
					if (!worldShop[idx]->renderWorldProp(*worldDraw[idx], camera,
							_set->setName(), propCel, r, depth))
						continue;
					cel = propCel.get();
					name = worldDraw[idx]->name;
				}
				if (x < r.left || x >= r.right || y < r.top || y >= r.bottom)
					continue;
				int srcX = (int)((int64)(x - r.left) * cel->width / r.width());
				int srcY = (int)((int64)(y - r.top) * cel->height / r.height());
				if (!cel->isOpaque(srcX, srcY))
					continue;
				_hitKind = itemType[(uint)i] ? "actor" : "prop";
				return name;
			}
		}
	}

	if (_stageVisible && isReplacementStage(_stage)) {
		Common::String button = _stage->hitTestButton((uint32)_stageNode, x, y);
		if (!button.empty()) {
			_hitKind = "button";
			return button;
		}
		_hitKind = "flat";
		return _stage->nodeName((uint32)_stageNode);
	}

	if (_setVisible && _set && _set->isOpen() && _setScene >= 0) {
		const int16 vl = _set->viewLeft(), vt = _set->viewTop();
		if (x >= vl && x < vl + (int)_set->width() && y >= vt && y < vt + (int)_set->height()) {
			if (_setTransitionType == kSetTransitionNone) {
				Common::String painting = _set->hitTestPainting((uint32)_setScene, _setView, x, y);
				if (!painting.empty()) {
					_hitKind = "painting";
					return painting;
				}
			}
			_hitKind = "scene";
			return _set->sceneName((uint32)_setScene);
		}
	}

	if (_stageVisible && _stage && _stage->isOpen()) {
		Common::String button = _stage->hitTestButton((uint32)_stageNode, x, y);
		if (!button.empty()) {
			_hitKind = "button";
			return button;
		}
		_hitKind = "flat";
		return _stage->nodeName((uint32)_stageNode);
	}

	_hitKind = "None";
	return Common::String();
}

bool CyberflixEngine::pointInButton(const Common::String &flat,
		const Common::String &button, int32 packedPoint) {
	if (!_stage || !_stage->isOpen())
		return false;
	int node = flat.empty() ? _stageNode : _stage->findNode(flat);
	if (node < 0 || (uint32)node >= _stage->nodeCount())
		return false;
	const int16 x = (int16)(packedPoint >> 16);
	const int16 y = (int16)(packedPoint & 0xffff);
	bool hit = _stage->pointInButton((uint32)node, button, x, y);
	debug(1, "Cyberflix: pointinbutton('%s', '%s', %d,%d) -> %s",
			_stage->nodeName((uint32)node).c_str(), button.c_str(), x, y,
			hit ? "true" : "false");
	return hit;
}

bool CyberflixEngine::pointInPainting(const Common::String &scene,
		const Common::String &view, const Common::String &painting, int32 packedPoint) {
	if (!_set || !_set->isOpen())
		return false;
	int sceneIdx = _set->findScene(scene);
	if (sceneIdx < 0)
		return false;
	const int16 x = (int16)(packedPoint >> 16);
	const int16 y = (int16)(packedPoint & 0xffff);
	bool hit = _set->pointInPainting((uint32)sceneIdx, view, painting, x, y);
	debug(1, "Cyberflix: pointinpainting('%s', '%s', '%s', %d,%d) -> %s",
			scene.c_str(), view.c_str(), painting.c_str(), x, y,
			hit ? "true" : "false");
	return hit;
}

// result() -> TI.EXE FUN_004366a0: the kind recorded by the last hittest.
Common::String CyberflixEngine::hitTestResult() {
	return _hitKind;
}

// mouse() -> TI.EXE FUN_004368b0: the current mouse point, packed like every
// other point value ((x << 16) | y).
int32 CyberflixEngine::mousePoint() {
	const Common::Point m = _eventMan->getMousePos();
	return ((int32)(int16)m.x << 16) | ((int32)m.y & 0xffff);
}

int32 CyberflixEngine::makePoint(int x, int y) {
	return ((int32)(int16)x << 16) | ((int32)y & 0xffff);
}

bool CyberflixEngine::buttonDown() {
	Common::Event event;
	while (_eventMan->pollEvent(event)) {
		if (event.type == Common::EVENT_QUIT) {
			quitGame();
			return false;
		}
	}
	return (_eventMan->getButtonState() & Common::EventManager::LBUTTON) != 0;
}

bool CyberflixEngine::stillDown() {
	Common::Event event;
	while (_eventMan->pollEvent(event)) {
		if (event.type == Common::EVENT_QUIT) {
			quitGame();
			return false;
		}
	}
	return (_eventMan->getButtonState() &
			(Common::EventManager::LBUTTON | Common::EventManager::RBUTTON)) != 0;
}

int CyberflixEngine::tick() {
	return (int)((uint64)_system->getMillis() * 60 / 1000);
}

int CyberflixEngine::calcDeg(int32 a, int32 b) {
	const int16 ax = (int16)(a >> 16);
	const int16 ay = (int16)(a & 0xffff);
	const int16 bx = (int16)(b >> 16);
	const int16 by = (int16)(b & 0xffff);
	int deg = (int)(atan2((double)(by - ay), (double)(bx - ax)) *
			(256.0 / 6.28318530717958647692));
	deg %= 256;
	if (deg < 0)
		deg += 256;
	if (deg >= 128)
		deg -= 256;
	return deg;
}

int CyberflixEngine::calcMod(int a, int b) {
	return b != 0 ? a % b : 0;
}

// cursor(...) -> TI.EXE FUN_00446920, with the script name already resolved
// to a PE resource name by the VM (see VMHost::setCursorResource).
void CyberflixEngine::setCursorResource(const Common::String &resourceName) {
	if (setGameCursor(resourceName))
		CursorMan.showMouse(true);
	else
		warning("Cyberflix: cursor resource '%s' not found in TI.EXE", resourceName.c_str());
}

// Dispatch a message with a freshly built scope chain, mirroring the
// original's per-dispatch chains (FUN_0042ae80 builds [prop script, shop
// script, BOOTFILE res2]; FUN_0042b2b0 [shop script, BOOTFILE res2]). The
// chain REPLACES the active one for the duration of the call — TI.EXE passes
// each dispatch's complete chain to the call executor FUN_0040b690, and
// notably BOOTFILE res1 (the boot mousedown/idle handlers) is NOT part of a
// prop/shop dispatch, so a prop without its own handler leaves the message
// unhandled instead of recursing into the boot handler of the same name.
// The 0xfba/0xfbb context atoms are saved and restored around the call. The
// fixed one/two-scope helper is used by hot scheduled-loop callbacks to avoid
// first building a temporary "scopes" array that is immediately reversed again.
void CyberflixEngine::dispatchWithScopes(const Script *scope1, const Script *scope2,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args,
		const char *debugContext) {
	dispatchWithScopesValue(scope1, scope2, self, targetProp, message, args, debugContext);
}

Value CyberflixEngine::dispatchWithScopesValue(const Script *scope1, const Script *scope2,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args,
		const char *debugContext) {
	Common::String prevSelf = _vm.contextSelf();
	Common::String prevProp = _vm.contextProp();
	Common::Array<const Script *> prevChain = _vm.swapLibrariesFixed(
			scope1, scope2, nullptr, _globalLib.get());
	_vm.setDispatchContext(self, targetProp);

	bool handled = false;
	Value result = _vm.callFunction(message, args, &handled);
	if (!handled)
		debug(1, "Cyberflix: %s message '%s' unhandled", debugContext, message.c_str());

	_vm.setDispatchContext(prevSelf, prevProp);
	_vm.restoreLibraries(prevChain);
	return result;
}

Value CyberflixEngine::dispatchWithThreeScopesValue(const Script *scope1, const Script *scope2,
		const Script *scope3, const Common::String &self,
		const Common::String &targetProp, const Common::String &message,
		const Common::Array<Value> &args, const char *debugContext) {
	// Painting dispatch has exactly three native scopes: painting, scene, set.
	// Keep the same search order as dispatchWithScopeChainValue() but skip the
	// caller-side temporary scope array in this sampled hot path.
	Common::String prevSelf = _vm.contextSelf();
	Common::String prevProp = _vm.contextProp();
	Common::Array<const Script *> prevChain = _vm.swapLibrariesFixed(
			scope1, scope2, scope3, _globalLib.get());
	_vm.setDispatchContext(self, targetProp);

	bool handled = false;
	Value result = _vm.callFunction(message, args, &handled);
	if (!handled)
		debug(1, "Cyberflix: %s message '%s' unhandled", debugContext, message.c_str());

	_vm.setDispatchContext(prevSelf, prevProp);
	_vm.restoreLibraries(prevChain);
	return result;
}

void CyberflixEngine::dispatchWithScopeChain(const Common::Array<const Script *> &scopes,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args,
		const char *debugContext) {
	dispatchWithScopeChainValue(scopes, self, targetProp, message, args, debugContext);
}

Value CyberflixEngine::dispatchWithScopeChainValue(const Common::Array<const Script *> &scopes,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args,
		const char *debugContext) {
	Common::String prevSelf = _vm.contextSelf();
	Common::String prevProp = _vm.contextProp();
	Common::Array<const Script *> chain;
	chain.reserve((_globalLib ? 1 : 0) + scopes.size());
	if (_globalLib)
		chain.push_back(_globalLib.get()); // "System: " tail, searched last
	for (int i = (int)scopes.size() - 1; i >= 0; --i)
		if (scopes[(uint32)i])
			chain.push_back(scopes[(uint32)i]);
	Common::Array<const Script *> prevChain = _vm.swapLibraries(chain);
	_vm.setDispatchContext(self, targetProp);

	bool handled = false;
	Value result = _vm.callFunction(message, args, &handled);
	if (!handled)
		debug(1, "Cyberflix: %s message '%s' unhandled", debugContext, message.c_str());

	_vm.setDispatchContext(prevSelf, prevProp);
	_vm.restoreLibraries(prevChain);
	return result;
}

void CyberflixEngine::dispatchSetMessage(const Common::String &message, const Common::Array<Value> &args) {
	if (!_set || !_set->isOpen() || message.empty())
		return;
	Common::SharedPtr<Script> setScript = _set->setScriptShared();
	dispatchWithScopes(setScript.get(), nullptr, _set->setName(), Common::String(),
			message, args, "set");
}

Value CyberflixEngine::dispatchSetMessageValue(const Common::String &message, const Common::Array<Value> &args) {
	if (!_set || !_set->isOpen() || message.empty())
		return Value();
	Common::SharedPtr<Script> setScript = _set->setScriptShared();
	return dispatchWithScopesValue(setScript.get(), nullptr, _set->setName(),
			Common::String(), message, args, "setfx");
}

Value CyberflixEngine::sendToSetFx(const Common::String &message, const Common::Array<Value> &args) {
	return dispatchSetMessageValue(message, args);
}

void CyberflixEngine::dispatchSceneMessage(uint32 scene, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!_set || !_set->isOpen() || message.empty())
		return;
	Common::SharedPtr<Script> sceneScript = _set->sceneScriptShared(scene);
	Common::SharedPtr<Script> setScript = _set->setScriptShared();
	dispatchWithScopes(sceneScript.get(), setScript.get(), _set->sceneName(scene),
			Common::String(), message, args, "scene");
}

bool CyberflixEngine::closeCurrentSceneForNavigation() {
	if (!_set || !_set->isOpen() || _setScene < 0)
		return false;

	// FUN_00430c70 calls FUN_00430f30 before FUN_00442140 starts movement, and
	// aborts if the set archive changed while the current scene handled close.
	Common::String openedName = _set->setName();
	uint32 openedCount = _set->sceneCount();
	Common::Array<Value> noArgs;
	dispatchSceneMessage((uint32)_setScene, "closescene", noArgs);
	return _set && _set->isOpen() && _set->setName() == openedName &&
			_set->sceneCount() == openedCount;
}

void CyberflixEngine::openShopFile(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	if (findShop(key)) {
		debug(1, "Cyberflix: shop '%s' already open", key.c_str());
		return;
	}

	Common::SharedPtr<Shop> shop(new Shop());
	if (!shop->open(key))
		return;
	_shops.push_back(shop);

	// Post-parse dispatch (FUN_0042a680): sendtoshop("<shop>", openshop())
	// then, for each prop of THIS shop, sendtoprop("<prop>", openprop())
	// (dispatch strings 0x457ec8 / 0x457eb8).
	dispatchWithScopes(shop->shopScript(), nullptr, key, Common::String(),
			"openshop", Common::Array<Value>());
	for (uint32 i = 0; i < shop->propCount(); ++i) {
		Shop::Prop &prop = shop->prop(i);
		dispatchWithScopes(prop.script.get(), shop->shopScript(), prop.name, prop.name,
				"openprop", Common::Array<Value>());
	}
	refreshPropsIfDirty();
}

void CyberflixEngine::closeShopFile(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _shops.size(); ++i) {
		if (_shops[i]->name() == key) {
			debug(1, "Cyberflix: shop '%s' closed", key.c_str());
			markShopDirty(*_shops[i]);
			_shops.remove_at(i);
			refreshPropsIfDirty();
			return;
		}
	}
	debug(1, "Cyberflix: closeshopfile('%s'): shop not open", key.c_str());
}

void CyberflixEngine::propInstance(const Common::String &source, const Common::String &newName) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(source, &shop);
	if (!prop || !shop) {
		warning("Cyberflix: propinstance('%s', '%s'): source prop not found",
				source.c_str(), newName.c_str());
		return;
	}
	if (newName.size() > 15) {
		warning("Cyberflix: propinstance('%s', '%s'): name too long",
				source.c_str(), newName.c_str());
		return;
	}
	if (findProp(newName))
		return;

	if (shop->addPropInstance(*prop, newName)) {
		debug(1, "Cyberflix: propinstance('%s', '%s')", source.c_str(), newName.c_str());
		_propsDirty = true;
	}
}

void CyberflixEngine::sendToShop(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoshop('%s') -> %s(%u args)", shopName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Shop> shop = findShopShared(shopName);
	if (!shop) {
		warning("Cyberflix: sendtoshop('%s'): shop not open", shopName.c_str());
		return;
	}
	dispatchWithScopes(shop->shopScript(), nullptr, shop->name(), Common::String(),
			message, args);
	refreshPropsIfDirty();
}

Value CyberflixEngine::sendToShopFx(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoshopfx('%s') -> %s(%u args)", shopName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Shop> shop = findShopShared(shopName);
	if (!shop) {
		warning("Cyberflix: sendtoshopfx('%s'): shop not open", shopName.c_str());
		return Value();
	}

	Common::Array<const Script *> scopes;
	scopes.push_back(shop->shopScript());
	return dispatchWithScopeChainValue(scopes, shop->name(), Common::String(),
			message, args, "shopfx");
}

void CyberflixEngine::sendToProp(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoprop('%s') -> %s(%u args)", propName.c_str(),
			message.c_str(), args.size());
	Shop::Prop *prop = nullptr;
	Common::SharedPtr<Shop> shopOwner = findPropOwnerShared(propName, &prop);
	if (!prop) {
		warning("Cyberflix: sendtoprop('%s'): no such prop", propName.c_str());
		return;
	}
	dispatchWithScopes(prop->script.get(), shopOwner->shopScript(), prop->name, prop->name,
			message, args);
	refreshPropsIfDirty();
}

Value CyberflixEngine::sendToPropFx(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtopropfx('%s') -> %s(%u args)", propName.c_str(),
			message.c_str(), args.size());
	Shop::Prop *prop = nullptr;
	Common::SharedPtr<Shop> shopOwner = findPropOwnerShared(propName, &prop);
	if (!prop) {
		warning("Cyberflix: sendtopropfx('%s'): no such prop", propName.c_str());
		return Value();
	}
	return dispatchWithScopesValue(prop->script.get(), shopOwner->shopScript(),
			prop->name, prop->name, message, args, "propfx");
}

static bool shouldLogInterfaceProp(const Common::String &name) {
	return name.equalsIgnoreCase("life") ||
			name.equalsIgnoreCase("watch") ||
			name.equalsIgnoreCase("bag") ||
			name.equalsIgnoreCase("map") ||
			name.equalsIgnoreCase("lid") ||
			name.equalsIgnoreCase("light") ||
			name.equalsIgnoreCase("invenhelp");
}

bool CyberflixEngine::propVisible(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propvisible('%s'): no such prop", name.c_str());
		return false;
	}
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propvisible('%s') -> %s", name.c_str(),
				prop->visible ? "true" : "false");
	return prop->visible;
}

void CyberflixEngine::propVisible(const Common::String &name, bool visible) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propvisible('%s'): no such prop", name.c_str());
		return;
	}
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propvisible('%s', %s) old=%s", name.c_str(),
				visible ? "true" : "false", prop->visible ? "true" : "false");
	if (prop->visible != visible) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
		prop->visible = visible;
		markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
	}
}

Common::String CyberflixEngine::propView(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propview('%s'): no such prop", name.c_str());
		return Common::String();
	}
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propview('%s') -> '%s'", name.c_str(),
				prop->shapeName.c_str());
	return prop->shapeName;
}

void CyberflixEngine::propView(const Common::String &name, const Common::String &shape) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propview('%s'): no such prop", name.c_str());
		return;
	}
	// FUN_004293a0 validates the shape against the prop master (FUN_0042c0c0)
	// and leaves the prop on the shape's LAST pose (+0x20 = poseCount - 1).
	uint16 poseCount = 0;
	if (!shop->shapePoseCount(*prop, shape, poseCount)) {
		warning("Cyberflix: propview('%s'): no shape '%s'", name.c_str(), shape.c_str());
		return;
	}
	Common::String key = shape;
	key.toLowercase();
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propview('%s', '%s') old='%s'", name.c_str(),
				key.c_str(), prop->shapeName.c_str());
	uint16 newPoseIndex = poseCount ? poseCount - 1 : 0;
	if (prop->shapeName != key || prop->poseCount != poseCount || prop->poseIndex != newPoseIndex) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
		prop->shapeName = key;
		prop->poseCount = poseCount;
		prop->poseIndex = newPoseIndex;
		markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
	}
}

int CyberflixEngine::propXY(const Common::String &name, int selector) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propxy('%s', %d): no such prop", name.c_str(), selector);
		return 0;
	}
	switch (selector) {
	case 1:
		return prop->x;
	case 2:
		return prop->y;
	case 3:
		return ((int32)prop->x << 16) | ((int32)prop->y & 0xffff);
	default:
		warning("Cyberflix: propxy('%s', %d): bad selector", name.c_str(), selector);
		return 0;
	}
}

void CyberflixEngine::setPropXY(const Common::String &name, int x, int y) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propxy('%s'): no such prop", name.c_str());
		return;
	}
	// FUN_0042a370: screen-space placement — mode = 0, depth = -1 when the
	// prop was world-space (>= 0), anchor = (x, y) (record +0x16/+0x14).
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->mode = 0;
	if (prop->depth >= 0)
		prop->depth = -1;
	prop->x = (int16)x;
	prop->y = (int16)y;
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

void CyberflixEngine::propSet(const Common::String &name, const Common::String &setName) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propset('%s'): no such prop", name.c_str());
		return;
	}
	Common::String key = setName;
	key.toLowercase();
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propset('%s', '%s') mode %u -> 1", name.c_str(),
				key.c_str(), prop->mode);
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->setName = key;
	prop->mode = 1; // FUN_00428c20 writes record +0x12 = 1 for SET placement.
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

void CyberflixEngine::propXYZ(const Common::String &name, int x, int y, int z) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propxyz('%s'): no such prop", name.c_str());
		return;
	}
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propxyz('%s', %d, %d, %d) mode %u -> 1",
				name.c_str(), x, y, z, prop->mode);
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->mode = 1; // FUN_0042a140: world/SET-space placement.
	prop->x = (int16)x;
	prop->y = (int16)y;
	prop->z = (int16)z;
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

void CyberflixEngine::propScale(const Common::String &name, int scale) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propscale('%s'): no such prop", name.c_str());
		return;
	}
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->scale = scale < 0 ? 0 : scale;
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

void CyberflixEngine::propZClip(const Common::String &name, int dist) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propzclip('%s'): no such prop", name.c_str());
		return;
	}
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->zClip = dist;
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

void CyberflixEngine::propDist(const Common::String &name, int dist) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propdist('%s'): no such prop", name.c_str());
		return;
	}
	// FUN_004295c0: only applied to screen-space props with a negative value.
	if (prop->mode == 0 && dist < 0) {
		debug(1, "Cyberflix: propdist('%s', %d) depth %d -> %d",
				name.c_str(), dist, prop->depth, dist);
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
		prop->depth = (int16)dist;
		markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
	}
}

int CyberflixEngine::propDeg(const Common::String &name, const int *newDeg) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propdeg('%s'): no such prop", name.c_str());
		return 0;
	}
	if (newDeg && prop->angle != (int16)(*newDeg & 0xff)) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
		prop->angle = (int16)(*newDeg & 0xff);
		markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
	}
	return prop->angle;
}

Common::String CyberflixEngine::propOwner(const Common::String &name, const Common::String *newOwner) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propowner('%s'): no such prop", name.c_str());
		return Common::String();
	}
	if (newOwner)
		prop->owner = *newOwner; // FUN_00428d40: copy into record +0x8c
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propowner('%s'%s%s%s) -> '%s'", name.c_str(),
				newOwner ? ", '" : "", newOwner ? newOwner->c_str() : "",
				newOwner ? "'" : "",
				prop->owner.c_str());
	return prop->owner;
}

int CyberflixEngine::propValue(const Common::String &name, const int *newValue) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propvalue('%s'): no such prop", name.c_str());
		return 0;
	}
	if (newValue)
		prop->value = *newValue; // FUN_00428e00: copy int to record +0x46
	return prop->value;
}

int CyberflixEngine::countProps() {
	int total = 0;
	for (uint32 i = 0; i < _shops.size(); ++i)
		total += (int)_shops[i]->propCount();
	return total;
}

Common::String CyberflixEngine::indexToProp(int index) {
	// 1-based index into the global prop array (FUN_0042b550).
	int i = index - 1;
	for (uint32 s = 0; s < _shops.size(); ++s) {
		if (i >= 0 && i < (int)_shops[s]->propCount())
			return _shops[s]->prop((uint32)i).name;
		i -= (int)_shops[s]->propCount();
	}
	return Common::String();
}

void CyberflixEngine::refreshPropsIfDirty() {
	// The original recomposites the display list every tick; this engine
	// renders on demand, so repaint the current room after a dispatch that
	// changed prop state. While no scene is up yet (boot-time initprops) the
	// props are picked up by the next renderSetScene.
	if (!_propsDirty)
		return;
	if (_puppet && _puppet->isOpen() && _puppetVisible) {
		// Native sendtoprop/sendtoshop dispatch does not repaint immediately.
		// The next forceupdate() takes the puppet compositor branch, so keep
		// SET prop dirtiness queued until the puppet is hidden or closed.
		return;
	}
	if (!_setVisible || isReplacementStage(_stage)) {
		if (_stage && _stage->isOpen()) {
			if (!_dirtyRects.empty())
				repaintDirtyStageRects();
			else {
				// Prop refresh without dirty bounds is still a compositor repaint,
				// not navigation to a new flat, so keep the current script cursor.
				renderStageNode(_stageNode, false);
			}
		}
		_dirtyRects.clear();
		_propsDirty = false;
		return;
	}
	if (_set && _set->isOpen() && _setScene >= 0) {
		// ScummVM-only optimization matching the native backing-surface model:
		// prop mutations do not change the SET background, and the current
		// decoded/transitioned background is already retained in _setFrameSequence.
		// Recompose that surface with live props instead of re-decoding the same
		// compressed panorama/transition frame for every sendtoprop() refresh.
		if (!_setFrameSequence.empty())
			displaySetFrame(_setFrameSequence);
		else
			renderSetScene(_setScene, _setAngle);
	}
	_dirtyRects.clear();
}

// actionframe(n): did the last movie display its n'th action-cue frame?
// Mirrors TI.EXE FUN_004362c0 reading the DAT_0046112a bitmask (n in 1..2).
bool CyberflixEngine::actionFrame(int n) {
	if (n < 1 || n > 2)
		return false;
	return (_actionFrameMask & (1 << (n - 1))) != 0;
}

int CyberflixEngine::randomNumber(int n) {
	if (n < 1)
		return 0;

	// Native startup FUN_0041a990 seeds FUN_0041b010 from timer helper
	// FUN_00405130(), fills 55 words as state[0] = seed % 0xffff;
	// state[i] = (state[i - 1] * 31 + 1) % 0xffff, then FUN_0041b080
	// advances the lagged-XOR table twice per draw. Use ScummVM's registered
	// RNG for recorder/TAS replayability, but preserve the script contract
	// from FUN_0041b060: random(n) returns 1..n inclusive.
	return (int)_rnd.getRandomNumber((uint)n - 1) + 1;
}

int CyberflixEngine::frameRate(const int *newRate) {
	if (newRate)
		_frameRate = CLIP(*newRate, 0, 60);
	return _frameRate;
}

bool CyberflixEngine::keyAborts(const Common::String *resource, const Common::String *key,
		const bool *enabled) {
	if (enabled)
		_keyAborts = *enabled;
	return _keyAborts;
}

bool CyberflixEngine::optionKey() {
	return (_eventMan->getModifierState() & Common::KBD_SHIFT) != 0;
}

Common::String CyberflixEngine::pathSlot(int slot, const Common::String *newPath) {
	if (slot < 0 || slot > 8) {
		warning("Cyberflix: path(%d): invalid slot", slot);
		return Common::String();
	}

	if (newPath) {
		_pathSlots[slot] = *newPath;
		registerPathSlotDirectory(slot);
	}

	return _pathSlots[slot];
}

Common::String CyberflixEngine::currentCD(const Common::String *requested) {
	if (requested) {
		if (requested->empty()) {
			_currentCD.clear();
		} else if (!validNativeCDLabel(*requested)) {
			warning("Cyberflix: currentcd('%s'): invalid CD label", requested->c_str());
		} else {
			Common::FSNode cdRoot;
			if (findExtractedCDRoot(*requested, cdRoot)) {
				_currentCD = canonicalCDLabel(cdRoot.getName());
			} else {
				_currentCD.clear();
				debug(1, "Cyberflix: currentcd('%s') did not find an extracted disc directory",
						requested->c_str());
			}
		}
	}

	return _currentCD;
}

void CyberflixEngine::registerPathSlotDirectory(int slot) {
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

// Resolve a clut name the way TI.EXE's registry lookup does (FUN_004470b0):
// the built-in names "black"/"current", and "set"/"stage"/"puppet" which alias
// the palette embedded in the currently open file of that kind. Named cluts
// registered by scripts land later.
bool CyberflixEngine::resolveClut(const Common::String &name, byte (&rgb)[256 * 3]) {
	Common::String key = name;
	key.toLowercase();
	memset(rgb, 0, sizeof(rgb));
	if (key == "black" || key.empty())
		return true;
	if (key == "current") {
		memcpy(rgb, _screenClut, sizeof(rgb));
		return true;
	}
	if (key == "set")
		return _set && _set->isOpen() && _set->loadSetPalette(rgb);
	if (key == "stage")
		return _stage && _stage->isOpen() && _stage->loadStagePalette(rgb);
	if (key == "puppet") {
		if (!_puppet || !_puppet->isOpen() || !_puppet->loadPuppetPalette(rgb))
			return false;
		if (_puppetGrab) {
			byte backdrop[256 * 3];
			memset(backdrop, 0, sizeof(backdrop));
			bool haveBackdrop = false;
			if (_set && _set->isOpen())
				haveBackdrop = _set->loadSetPalette(backdrop);
			else if (_stage && _stage->isOpen())
				haveBackdrop = _stage->loadStagePalette(backdrop);
			if (haveBackdrop) {
				int first = CLIP<int>(_puppetParams[0], 0, 256);
				int last = CLIP<int>(_puppetParams[1], 0, 256);
				if (last > first)
					memcpy(rgb + first * 3, backdrop + first * 3, (last - first) * 3);
			}
		}
		return true;
	}
	warning("Cyberflix: clut '%s' not resolvable yet", name.c_str());
	return false;
}

// Program the hardware palette and mirror it in _screenClut ("current",
// TI.EXE DAT_0045f3c8 programmed by FUN_004010f0). The original forces
// entry 0 to black and 255 to white; the game palettes already obey that.
//
// The runtime never programs the clut values directly: FUN_00401170 maps
// every component through a per-channel gamma curve built by FUN_00401220,
// table[i] = trunc(pow(i / 255.0, gamma) * 255.0), with the gamma globals
// statically initialized to 0.65 (TI.EXE .data 0x457040/48/50) and runtime
// adjustable from F1-F8 (steps *1.05 / *0.952381, clamped 0.15..2.5,
// FUN_00403bf0 from the message pump FUN_00403690; F9 resets to 0.65).
// 0.65 < 1 brightens the mid-tones considerably, which is why the original
// renders noticeably lighter than the raw clut colors. _screenClut stays
// pre-gamma like DAT_0045f3c8 (fades interpolate raw cluts and re-apply the
// curve every step, matching FUN_0041ba80 -> FUN_004010f0).
void CyberflixEngine::updatePaletteGammaTable() {
	if (!_paletteGammaTableDirty)
		return;
	// Fades call programPalette() once per 60 Hz step. Gamma changes only via
	// F1-F9, so cache the expensive pow() lookup table and reuse it across fade
	// steps instead of rebuilding it for every palette update.
	for (int c = 0; c < 3; ++c) {
		for (int i = 0; i < 256; ++i)
			_paletteGammaTable[c][i] = (byte)(pow(i / 255.0, _paletteGamma[c]) * 255.0); // trunc, like __ftol
	}
	_paletteGammaTableDirty = false;
}

void CyberflixEngine::programPalette(const byte (&rgb)[256 * 3]) {
	memcpy(_screenClut, rgb, sizeof(_screenClut));
	updatePaletteGammaTable();

	byte hw[256 * 3];
	for (int i = 0; i < 256; ++i) {
		hw[i * 3 + 0] = _paletteGammaTable[0][_screenClut[i * 3 + 0]];
		hw[i * 3 + 1] = _paletteGammaTable[1][_screenClut[i * 3 + 1]];
		hw[i * 3 + 2] = _paletteGammaTable[2][_screenClut[i * 3 + 2]];
	}
	_system->getPaletteManager()->setPalette(hw, 0, 256);
}

// clut(name): snap the hardware palette to the named clut instantly
// (FUN_00446500 -> FUN_0041ba80). Pixels are untouched, so clut('black')
// makes whatever is (or gets) painted invisible until a fade reveals it.
void CyberflixEngine::setClut(const Common::String &name) {
	byte rgb[256 * 3];
	if (!resolveClut(name, rgb))
		return;
	programPalette(rgb);
	_system->updateScreen();
	debug(1, "Cyberflix: clut('%s')", name.c_str());
}

// blackscreen() (FUN_00446b80): fill the window with black pixels via a GDI
// rect fill in the original. The palette is not touched.
void CyberflixEngine::blackScreen() {
	Graphics::Surface *screen = _system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	_system->unlockScreen();
	_system->updateScreen();
	debug(1, "Cyberflix: blackscreen()");
}

bool CyberflixEngine::pumpCursorMotionEvents() {
	const Common::Point oldMouse = _eventMan->getMousePos();
	Common::Array<Common::Event> deferred;
	Common::Event event;

	while (_eventMan->pollEvent(event)) {
		switch (event.type) {
		case Common::EVENT_MOUSEMOVE:
			break;
		case Common::EVENT_QUIT:
		case Common::EVENT_RETURN_TO_LAUNCHER:
			quitGame();
			break;
		default:
			deferred.push_back(event);
			break;
		}
	}

	for (uint i = 0; i < deferred.size(); ++i)
		_eventMan->pushEvent(deferred[i]);

	const Common::Point newMouse = _eventMan->getMousePos();
	return oldMouse.x != newMouse.x || oldMouse.y != newMouse.y;
}

void CyberflixEngine::forceUpdate() {
	// forceupdate() (TI.EXE 0x2f14 -> FUN_00446910 -> FUN_00423a60): rebuild the
	// display list from LIVE prop visibility, step active SET transitions through
	// FUN_004420b0, composite, and present.
	const bool cursorMoved = pumpCursorMotionEvents();
	bool presented = false;
	processScheduledLoops();
	if (!_propsDirty && isReplacementStage(_stage) && hasAnimatedScreenProps())
		_propsDirty = true;
	refreshPropsIfDirty();
	if (_puppet && _puppet->isOpen() && _puppetVisible) {
		renderCurrentPuppetFrame(true);
		_screenUpdatePending = false;
		_idleForceUpdatePresented = true;
		presented = true;
	} else {
		advanceSetTransition();
		// Native forceupdate() presents after its compositor pass, but in
		// ScummVM advanceSetTransition() may be a no-op when a scene script calls
		// forceupdate() while no movement/dirty repaint is pending. Only upload
		// to the OpenGL backend if displaySetFramePixels() actually wrote new
		// pixels; this avoids redundant texture updates without changing the
		// script-visible timing of forceupdate(). If no pixels changed, leave
		// _idleForceUpdatePresented false so the main loop still does its cheap
		// cursor-only updateScreen() and mouse movement stays responsive.
		presented = presentPendingScreenUpdate();
		_idleForceUpdatePresented = presented;
	}
	if (cursorMoved && !presented) {
		// Native uses the Win32 cursor, which moves independently while script
		// wait loops call forceupdate(). ScummVM's cursor is software-drawn, so
		// after pumping mouse-motion events above we need a cursor-only present
		// if the compositor did not upload any pixels this pass.
		_system->updateScreen();
		_idleForceUpdatePresented = true;
	}
	if (_frameRate > 0) {
		const int deadline = _lastFrameTick + _frameRate;
		while (!shouldQuit()) {
			const int remainingTicks = deadline - tick();
			if (remainingTicks <= 0)
				break;
			uint32 delay = (uint32)((remainingTicks * 1000 + 59) / 60);
			if (delay > 17)
				delay = 17;
			_system->delayMillis(delay);
		}
	}
	_lastFrameTick = tick();
	debug(2, "Cyberflix: forceupdate()");
}

void CyberflixEngine::message(const Common::String &text) {
	debug(1, "Cyberflix message: %s", text.c_str());
}

void CyberflixEngine::flushEvents() {
	_eventMan->purgeMouseEvents();
	_eventMan->purgeKeyboardEvents();
}

void CyberflixEngine::drawString(const Common::String &text, int32 packedPoint, int color, int size) {
	const Graphics::Font *font = textFont(size);
	if (!font)
		return;

	const int16 x = (int16)(packedPoint >> 16);
	const int16 baselineY = (int16)(packedPoint & 0xffff);
	if (x >= kScreenWidth || baselineY >= kScreenHeight)
		return;

	Graphics::Surface *screen = _system->lockScreen();
	font->drawString(screen, text, x, baselineY - font->getFontAscent(),
			kScreenWidth - x, (uint32)CLIP(color, 0, 255));
	_system->unlockScreen();
	_system->updateScreen();
}

// blacktoscreen(target, n) / screentoblack(target, n): palette-only fade
// between black and the target clut, one interpolation step per 60 Hz tick
// (FUN_0041b3f0 / FUN_0041b3a0 stepping FUN_0041b200 against the scaled timer).
// Verified: TI.EXE's blacktoscreen (FUN_00446b00 -> FUN_004470b0 ->
// FUN_0041b3f0) only resolves the target CLUT and interpolates the palette; it
// does NOT re-render props. Scripts redraw first via visualeffect(plain, 0).
void CyberflixEngine::fadePalette(const Common::String &target, int steps, bool toBlack) {
	byte to[256 * 3];
	if (!resolveClut(target, to))
		return;
	if (steps < 1)
		steps = 1;

	byte from[256 * 3];
	if (toBlack) {
		memcpy(from, to, sizeof(from));
		memset(to, 0, sizeof(to));
	} else {
		memset(from, 0, sizeof(from));
	}

	debug(1, "Cyberflix: %s('%s', %d)", toBlack ? "screentoblack" : "blacktoscreen",
			target.c_str(), steps);
	fadePaletteSteps(from, to, steps);
}

bool CyberflixEngine::paletteIsBlack() const {
	for (int i = 0; i < 256 * 3; ++i)
		if (_screenClut[i])
			return false;
	return true;
}

void CyberflixEngine::fadePaletteSteps(const byte (&from)[256 * 3], const byte (&to)[256 * 3], int steps) {
	if (steps < 1)
		steps = 1;
	uint32 startMs = _system->getMillis();
	bool reachedFinalStep = false;
	for (int s = 1; s <= steps && !shouldQuit(); ++s) {
		byte cur[256 * 3];
		for (int i = 0; i < 256 * 3; ++i)
			cur[i] = (byte)(from[i] + ((int)to[i] - (int)from[i]) * s / steps);
		programPalette(cur);
		_system->updateScreen();
		if (s == steps)
			reachedFinalStep = true;
		// One step per 60 Hz tick of the original's scaled timer.
		uint32 deadline = startMs + (uint32)((uint64)s * 1000 / 60);
		uint32 now = _system->getMillis();
		if (now < deadline)
			_system->delayMillis(deadline - now);
		Common::Event event;
		while (_eventMan->pollEvent(event))
			; // keep the window live; fades are not skippable in the original
	}
	if (!reachedFinalStep) {
		// Preserve the old "finish at target palette" behavior if the loop exits
		// early, but avoid a duplicate final update when the last timed step
		// already displayed the target palette.
		programPalette(to);
		_system->updateScreen();
	}
}

// visualeffect(effect, dur) (FUN_00446400): mark a full-screen dirty rect
// (FUN_00441ce0(2)), run the compositor (FUN_00423a60), then apply the chosen
// visual transition to the full-screen backing buffer (FUN_004439c0). The boot
// scripts use plain (0x5dce) before blacktoscreen('set'/'stage') so the pixels
// are already redrawn while the palette is black.
void CyberflixEngine::setVisualEffect(uint16 effect, int duration) {
	if (duration < 1)
		duration = 1;
	else if (duration > 1000)
		duration = 1000;

	refreshPropsIfDirty();
	if (_puppet && _puppet->isOpen() && _puppetVisible) {
		renderCurrentPuppetFrame(false);
	} else if (_setVisible && _set && _set->isOpen() && _setScene >= 0) {
		if (_setTransitionType != kSetTransitionNone)
			advanceSetTransition();
		else
			renderSetScene(_setScene, _setTable, _setAngle, _setView);
	} else if (_stage && _stage->isOpen()) {
		// visualeffect() reaches the same update/compositor path as forceupdate();
		// it repaints pixels but native cursor state remains script-controlled.
		renderStageNode(_stageNode, false);
		_propsDirty = false;
	} else {
		_system->updateScreen();
	}

	debug(1, "Cyberflix: visualeffect(%#x, %d)", effect, duration);
}

CyberflixEngine::ScheduledLoop::Kind CyberflixEngine::scheduledLoopKind(
		const Common::String &kind) {
	if (kind.equalsIgnoreCase("scene"))
		return ScheduledLoop::kScene;
	if (kind.equalsIgnoreCase("flat"))
		return ScheduledLoop::kFlat;
	if (kind.equalsIgnoreCase("stage"))
		return ScheduledLoop::kStage;
	if (kind.equalsIgnoreCase("prop"))
		return ScheduledLoop::kProp;
	if (kind.equalsIgnoreCase("shop"))
		return ScheduledLoop::kShop;
	if (kind.equalsIgnoreCase("actor"))
		return ScheduledLoop::kActor;
	return ScheduledLoop::kUnknown;
}

void CyberflixEngine::makeLoop(const Common::String &kind, const Common::String &target,
		const Common::String &message, int delay) {
	if (kind.empty() || message.empty()) {
		warning("Cyberflix: makeloop('%s', '%s', '%s', %d): invalid arguments",
				kind.c_str(), target.c_str(), message.c_str(), delay);
		return;
	}

	stopLoop(kind, target);

	ScheduledLoop loop;
	loop.kindId = scheduledLoopKind(kind);
	loop.kind = kind;
	loop.kind.toLowercase();
	loop.target = target;
	loop.message = message;
	loop.remainingPasses = delay;
	loop.createdPass = _scheduledLoopPass;
	_scheduledLoops.push_back(loop);
	debug(2, "Cyberflix: makeloop('%s', '%s', '%s', %d)",
			kind.c_str(), target.c_str(), message.c_str(), delay);
}

void CyberflixEngine::stopLoop(const Common::String &kind, const Common::String &target) {
	if (kind.empty())
		return;
	const bool all = kind.equalsIgnoreCase("all");
	const ScheduledLoop::Kind kindId = scheduledLoopKind(kind);
	for (int i = (int)_scheduledLoops.size() - 1; i >= 0; --i) {
		const ScheduledLoop &loop = _scheduledLoops[(uint32)i];
		const bool kindMatches = kindId == ScheduledLoop::kUnknown
				? loop.kind.equalsIgnoreCase(kind)
				: loop.kindId == kindId;
		if (all || (kindMatches &&
				(target.empty() || loop.target.equalsIgnoreCase(target))))
			_scheduledLoops.remove_at((uint32)i);
	}
}

void CyberflixEngine::pauseLoop(const Common::String &kind, bool paused) {
	if (kind.equalsIgnoreCase("all"))
		_loopsPaused = paused;
}

void CyberflixEngine::makeCricket(const Common::String &name) {
	if (name.empty())
		return;
	for (uint i = 0; i < _crickets.size(); ++i) {
		if (_crickets[i].name.equalsIgnoreCase(name)) {
			_crickets[i].paused = _cricketsPaused;
			playSound(name, 1);
			return;
		}
	}
	CricketState cricket;
	cricket.name = name;
	cricket.paused = _cricketsPaused;
	_crickets.push_back(cricket);
	playSound(name, 1);
}

void CyberflixEngine::stopCricket(const Common::String &name) {
	if (name.equalsIgnoreCase("all")) {
		_crickets.clear();
		return;
	}
	for (int i = (int)_crickets.size() - 1; i >= 0; --i) {
		if (_crickets[(uint)i].name.equalsIgnoreCase(name))
			_crickets.remove_at((uint)i);
	}
	debug(2, "Cyberflix: stopcricket('%s')", name.c_str());
}

void CyberflixEngine::pauseCricket(const Common::String &kind, bool paused) {
	if (kind.equalsIgnoreCase("all")) {
		_cricketsPaused = paused;
		for (uint i = 0; i < _crickets.size(); ++i)
			_crickets[i].paused = paused;
	} else {
		for (uint i = 0; i < _crickets.size(); ++i)
			if (_crickets[i].name.equalsIgnoreCase(kind))
				_crickets[i].paused = paused;
	}
	debug(2, "Cyberflix: pausecricket('%s', %d)", kind.c_str(), paused ? 1 : 0);
}

void CyberflixEngine::processScheduledLoops() {
	if (_loopsPaused || _scheduledLoops.empty() || _processingScheduledLoops)
		return;

	// Scheduled callbacks are commonly zero-argument "scene"/"flat" loops that
	// fire from forceupdate(). Keep the hot path allocation-free except for the
	// actual script dispatch work measured under this routine.
	_processingScheduledLoops = true;
	const uint32 currentPass = ++_scheduledLoopPass;
	Common::Array<Value> noArgs;
	for (uint32 i = 0; i < _scheduledLoops.size();) {
		if (_scheduledLoops[i].createdPass >= currentPass) {
			++i;
			continue;
		}

		--_scheduledLoops[i].remainingPasses;
		if (_scheduledLoops[i].remainingPasses > 0) {
			++i;
			continue;
		}

		ScheduledLoop loop = _scheduledLoops[i];
		_scheduledLoops.remove_at(i);
		switch (loop.kindId) {
		case ScheduledLoop::kScene:
			sendToScene(loop.target, loop.message, noArgs);
			break;
		case ScheduledLoop::kFlat:
			sendToFlat(loop.target, loop.message, noArgs);
			break;
		case ScheduledLoop::kStage:
			sendToStage(loop.message, noArgs);
			break;
		case ScheduledLoop::kProp:
			sendToProp(loop.target, loop.message, noArgs);
			break;
		case ScheduledLoop::kShop:
			sendToShop(loop.target, loop.message, noArgs);
			break;
		case ScheduledLoop::kActor:
			sendToActor(loop.target, loop.message, noArgs);
			break;
		default:
			debug(1, "Cyberflix: makeloop kind '%s' unhandled", loop.kind.c_str());
		}
		refreshPropsIfDirty();
	}
	_processingScheduledLoops = false;
}

// sendtoscene(name, message): dispatch the message against [scene script, set
// script, BOOTFILE res2] for the named scene, without changing the currently
// rendered scene (TI.EXE FUN_004311e0/FUN_00431200).
void CyberflixEngine::sendToScene(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!_set || !_set->isOpen()) {
		warning("Cyberflix: sendtoscene('%s') with no set open", scene.c_str());
		return;
	}
	int index = _set->findScene(scene);
	if (index < 0) {
		warning("Cyberflix: set '%s' has no scene named '%s'",
				_set->name().c_str(), scene.c_str());
		return;
	}
	if (!message.empty())
		dispatchSceneMessage((uint32)index, message, args);
}

Value CyberflixEngine::sendToSceneFx(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!_set || !_set->isOpen()) {
		warning("Cyberflix: sendtoscenefx('%s') with no set open", scene.c_str());
		return Value();
	}
	int index = _set->findScene(scene);
	if (index < 0) {
		warning("Cyberflix: set '%s' has no scene named '%s'",
				_set->name().c_str(), scene.c_str());
		return Value();
	}
	Common::SharedPtr<Script> sceneScript = _set->sceneScriptShared((uint32)index);
	Common::SharedPtr<Script> setScript = _set->setScriptShared();
	return dispatchWithScopesValue(sceneScript.get(), setScript.get(),
			_set->sceneName((uint32)index), Common::String(), message, args, "scenefx");
}

// sendtopainting(scene, view, painting, message): dispatch the message over the
// current SET's painting chain. BEDSIT1's poster records have no own script, so
// the set script handles mousedown/setcursor via 0xfbb (target painting name).
void CyberflixEngine::sendToPainting(const Common::String &sceneName, const Common::String &viewName,
		const Common::String &painting, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!_set || !_set->isOpen()) {
		warning("Cyberflix: sendtopainting('%s') with no set open", painting.c_str());
		return;
	}
	int scene = sceneName.empty() ? _setScene : _set->findScene(sceneName);
	if (scene < 0) {
		warning("Cyberflix: sendtopainting('%s'): no scene '%s'",
				painting.c_str(), sceneName.c_str());
		return;
	}
	Common::String view = !viewName.empty() ? viewName : _setView;

	Common::SharedPtr<Script> paintingScript, sceneScript, setScript;
	if (!_set->paintingDispatchScripts((uint32)scene, view, painting,
			paintingScript, sceneScript, setScript)) {
		warning("Cyberflix: sendtopainting('%s'): no view '%s'",
				painting.c_str(), view.c_str());
		return;
	}
	dispatchWithThreeScopesValue(paintingScript.get(), sceneScript.get(), setScript.get(),
			painting, painting, message, args, "painting");
	refreshPropsIfDirty();
}

Value CyberflixEngine::sendToPaintingFx(const Common::String &sceneName,
		const Common::String &viewName, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!_set || !_set->isOpen()) {
		warning("Cyberflix: sendtopaintingfx('%s') with no set open", painting.c_str());
		return Value();
	}
	int scene = sceneName.empty() ? _setScene : _set->findScene(sceneName);
	if (scene < 0) {
		warning("Cyberflix: sendtopaintingfx('%s'): no scene '%s'",
				painting.c_str(), sceneName.c_str());
		return Value();
	}
	Common::String view = !viewName.empty() ? viewName : _setView;

	Common::SharedPtr<Script> paintingScript, sceneScript, setScript;
	if (!_set->paintingDispatchScripts((uint32)scene, view, painting,
			paintingScript, sceneScript, setScript)) {
		warning("Cyberflix: sendtopaintingfx('%s'): no view '%s'",
				painting.c_str(), view.c_str());
		return Value();
	}
	return dispatchWithThreeScopesValue(paintingScript.get(), sceneScript.get(), setScript.get(),
			painting, painting, message, args, "paintingfx");
}

void CyberflixEngine::navigateSet(const Common::String &action) {
	if (!_setVisible || !_set || !_set->isOpen() || _setScene < 0)
		return;
	if (_setTransitionType != kSetTransitionNone)
		return;

	int viewIdx = _set->findView((uint32)_setScene, _setView);
	if (viewIdx < 0)
		viewIdx = _set->viewTagAtAngle((uint32)_setScene, (uint32)_setTable, (uint32)_setAngle);
	if (viewIdx < 0) {
		warning("Cyberflix: cannot navigate set '%s' scene '%s': current view '%s' not found",
				_set->name().c_str(), _set->sceneName((uint32)_setScene).c_str(), _setView.c_str());
		return;
	}

	if (action.equalsIgnoreCase("left") || action.equalsIgnoreCase("right")) {
		const int table = action.equalsIgnoreCase("left") ? 1 : 0;
		int startAngle = _set->angleForView((uint32)_setScene, (uint32)table, viewIdx);
		if (startAngle < 0 || _set->nextTaggedAngle((uint32)_setScene, (uint32)table, startAngle) < 0) {
			warning("Cyberflix: set '%s' scene '%s' has no %s turn from view '%s'",
					_set->name().c_str(), _set->sceneName((uint32)_setScene).c_str(),
					action.c_str(), _setView.c_str());
			return;
		}
		if (!closeCurrentSceneForNavigation())
			return;
		if (!_set->applyPanoramaFrame((uint32)_setScene, (uint32)table, (uint32)startAngle,
				_setFrameSequence)) {
			warning("Cyberflix: set '%s' failed to start %s turn from view '%s'",
					_set->name().c_str(), action.c_str(), _setView.c_str());
			return;
		}
		_setTable = table;
		_setAngle = startAngle;
		_setTransitionType = kSetTransitionTurn;
		displaySetFrame(_setFrameSequence);
		return;
	}

	if (action.equalsIgnoreCase("strait")) {
		uint32 transitionId = _set->forwardTransitionForView((uint32)_setScene, viewIdx);
		if (transitionId == 0)
			return;
		uint32 count = _set->transitionFrameCount(transitionId);
		if (count < 2) {
			warning("Cyberflix: set '%s' transition %u has too few frames (%u)",
					_set->name().c_str(), transitionId, count);
			return;
		}
		if (!closeCurrentSceneForNavigation())
			return;
		if (!_set->applyTransitionFrame(transitionId, 0, _setFrameSequence)) {
			warning("Cyberflix: set '%s' failed to start forward transition %u",
					_set->name().c_str(), transitionId);
			return;
		}
		_setTransitionType = kSetTransitionForward;
		_setTransitionResource = transitionId;
		_setTransitionFrame = 0;
		displaySetFrame(_setFrameSequence);
	}
}

void CyberflixEngine::advanceSetTransition() {
	if (!_setVisible || _setTransitionType == kSetTransitionNone ||
			!_set || !_set->isOpen() || _setScene < 0)
		return;

	if (_setTransitionType == kSetTransitionTurn) {
		uint32 count = _set->angleCount((uint32)_setScene, (uint32)_setTable);
		if (count == 0) {
			_setTransitionType = kSetTransitionNone;
			return;
		}

		int nextAngle = (_setAngle + 1) % (int)count;
		if (!_set->applyPanoramaFrame((uint32)_setScene, (uint32)_setTable, (uint32)nextAngle,
				_setFrameSequence)) {
			_setTransitionType = kSetTransitionNone;
			warning("Cyberflix: failed to advance SET turn transition");
			return;
		}
		_setAngle = nextAngle;
		int viewIdx = _set->viewTagAtAngle((uint32)_setScene, (uint32)_setTable, (uint32)nextAngle);
		if (viewIdx >= 0)
			_setView = _set->viewName((uint32)_setScene, (uint32)viewIdx);
		displaySetFrame(_setFrameSequence);

		if (viewIdx >= 0) {
			_setTransitionType = kSetTransitionNone;
			Common::Array<Value> noArgs;
			dispatchSceneMessage((uint32)_setScene, "openscene", noArgs);
		}
		return;
	}

	if (_setTransitionType == kSetTransitionForward) {
		uint32 count = _set->transitionFrameCount(_setTransitionResource);
		uint32 nextFrame = _setTransitionFrame + 1;
		if (nextFrame >= count) {
			_setTransitionType = kSetTransitionNone;
			_setTransitionResource = 0;
			_setTransitionFrame = 0;
			return;
		}

		if (!_set->applyTransitionFrame(_setTransitionResource, nextFrame, _setFrameSequence)) {
			_setTransitionType = kSetTransitionNone;
			warning("Cyberflix: failed to advance SET forward transition %u", _setTransitionResource);
			return;
		}
		_setTransitionFrame = nextFrame;
		displaySetFrame(_setFrameSequence);

		if (nextFrame == count - 1) {
			uint32 scene = 0;
			Common::String view;
			int angle = 0;
			if (!_set->transitionDestination(_setTransitionResource, scene, view, angle)) {
				warning("Cyberflix: set '%s' transition %u has no resolvable destination",
						_set->name().c_str(), _setTransitionResource);
				_setTransitionType = kSetTransitionNone;
				return;
			}
			_setScene = (int)scene;
			_setTable = 0;
			_setAngle = angle;
			_setView = view;
			_setTransitionType = kSetTransitionNone;
			_setTransitionResource = 0;
			_setTransitionFrame = 0;
			Common::Array<Value> noArgs;
			dispatchSceneMessage((uint32)_setScene, "openscene", noArgs);
		}
	}
}

void CyberflixEngine::renderSetScene(int scene, int table, int angle, const Common::String &view) {
	if (!_set || !_set->isOpen()) {
		warning("Cyberflix: renderSetScene with no set open");
		return;
	}

	if (!_set->renderScene((uint32)scene, (uint32)table, (uint32)angle, _setFrameSequence))
		return;

	_setScene = scene;
	_setTable = table;
	_setAngle = angle;
	if (!view.empty()) {
		_setView = view;
	} else {
		int viewIdx = _set->viewTagAtAngle((uint32)scene, (uint32)table, (uint32)angle);
		if (viewIdx >= 0)
			_setView = _set->viewName((uint32)scene, (uint32)viewIdx);
	}

	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	// As with stage nodes: while the screen palette is black the room is
	// painted invisibly and revealed later by blacktoscreen('set', n).
	if (_setVisible && _set->loadSetPalette(rgb) && !paletteIsBlack())
		programPalette(rgb);

	if (_setVisible)
		displaySetFrame(_setFrameSequence);

	debug(1, "Cyberflix: rendered set '%s' scene %d '%s' angle %d (%ux%u)",
			_set->name().c_str(), scene, _set->sceneName((uint32)scene).c_str(),
			angle, _setFrameSequence.width(), _setFrameSequence.height());
}

void CyberflixEngine::displaySetFrame(const FrameImage &frame) {
	displaySetFramePixels(frame.pixels.begin(), frame.width, frame.height);
}

void CyberflixEngine::displaySetFrame(const FrameSequence &frame) {
	if (frame.empty())
		return;
	displaySetFramePixels(frame.pixels(), frame.width(), frame.height());
}

void CyberflixEngine::displaySetFramePixels(const byte *pixels, uint16 width, uint16 height) {
	if (!_setVisible || !_set || !_set->isOpen())
		return;

	advancePropPoses();
	const FrameImage *stageBg = stageShellFrame();
	Graphics::Surface *screen = _system->lockScreen();
	// Base layer: the stage's UI shell (MAIN.STG node 0 — art-deco frame +
	// inventory bar). The original's compositor keeps it on screen beneath
	// the room: the redraw pass FUN_00442d90 repaints full-screen stage items
	// (clipped to screen rect, FUN_004436d0) before world items, which clip
	// to the set viewport DAT_00486760. Fall back to black with no stage.
	if (stageBg) {
		copyFrameToScreen(*screen, *stageBg, 0, 0);
	} else {
		screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	}
	// The panorama is drawn at the master header's viewport origin (B+0x80/
	// 0x82, {0, 0} in every set), i.e. at the TOP of the screen: FUN_00441f40
	// builds the draw rect {top, left, top+h, left+w} from those fields. The
	// 512x120 strip below (screen height 384 - 0x78, FUN_00449150) shows the
	// stage's inventory bar.
	int x0 = _set->viewLeft();
	int y0 = _set->viewTop();
	copyFramePixelsToScreen(*screen, pixels, width, height, x0, y0);
	// World/SET-space SHOP props (propset + propxyz) are projected through the
	// active panorama camera before the screen-space inventory/UI overlays.
	Set::CameraData cameraData;
	bool haveCamera = false;
	if (_setTransitionType == kSetTransitionForward) {
		haveCamera = _set->transitionCameraData(_setTransitionResource,
				_setTransitionFrame, cameraData);
	} else if (_setScene >= 0) {
		haveCamera = _set->cameraData((uint32)_setScene, (uint32)_setTable,
				(uint32)_setAngle, cameraData);
	}
	if (haveCamera) {
		Shop::WorldCamera camera = makeWorldCamera(cameraData);
		Common::Array<const Shop::Prop *> worldDraw;
		Common::Array<const Shop *> worldShop;
		Common::Array<int16> worldDepths;
		Common::Array<const Cast::Actor *> actorDraw;
		Common::Array<const Cast *> actorCast;
		Common::Array<int16> actorDepths;
		collectWorldProps(worldDraw, worldShop, worldDepths, camera);
		collectWorldActors(actorDraw, actorCast, actorDepths, camera);
		Common::Rect viewport(camera.viewportLeft, camera.viewportTop,
				camera.viewportRight, camera.viewportBottom);
		uint32 propIndex = 0, actorIndex = 0;
		while (propIndex < worldDraw.size() || actorIndex < actorDraw.size()) {
			const bool drawActor = actorIndex < actorDraw.size() &&
					(propIndex >= worldDraw.size() || actorDepths[actorIndex] >= worldDepths[propIndex]);
			CelImage actorCel;
			Common::SharedPtr<CelImage> propCel;
			Common::Rect r;
			int16 depth = 0;
			if (drawActor) {
				if (actorCast[actorIndex]->renderWorldActor(*actorDraw[actorIndex],
						camera, _set->setName(), actorCel, r, depth))
					drawScaledCel(screen, actorCel, r, viewport);
				++actorIndex;
			} else {
				if (worldShop[propIndex]->renderWorldProp(*worldDraw[propIndex], camera,
						_set->setName(), propCel, r, depth))
					drawScaledCel(screen, *propCel, r, viewport);
				++propIndex;
			}
		}
	}
	// Screen-space props (HELP button, life preserver, owned items...) on top
	// of the bar/room. The original's compositor draws screen items (negative
	// depth, clipped to the screen rect FUN_00443250) ordered by depth — larger
	// signed depths paint first, so more-negative items overdraw them (display
	// builder FUN_004434f0, depth from prop record +0x26). World-mode props
	// (angle/scale path) land with set-prop rendering.
	{
		Common::Array<const Shop::Prop *> draw;
		Common::Array<const Shop *> drawShop;
		collectScreenProps(draw, drawShop);
		for (uint32 i = 0; i < draw.size(); ++i) {
			Common::SharedPtr<CelImage> cel;
			Common::Rect r;
			if (!drawShop[i]->renderProp(*draw[i], cel, r))
				continue;
			drawCel(screen, *cel, r, Common::Rect(kScreenWidth, kScreenHeight));
		}
	}
	_propsDirty = false;
	_dirtyRects.clear();
	_system->unlockScreen();
	// SET compositing can be driven many times from scene scripts. Mark the
	// backend upload as pending and let forceupdate()/the main loop present once;
	// otherwise scripts that call forceupdate() repeatedly pay an OpenGL texture
	// upload even when no compositor pass drew new pixels.
	_screenUpdatePending = true;

	// Default arrow until per-view hotspot hit-testing (directional cursors) is
	// implemented. Views (the scene's hotspot lists) are documented in
	// files/decomp/stage-notes.md.
	if (setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
}

bool CyberflixEngine::presentPendingScreenUpdate() {
	if (!_screenUpdatePending)
		return false;
	_system->updateScreen();
	_screenUpdatePending = false;
	return true;
}

Common::Error CyberflixEngine::run() {
	// The original is a 640x480 8-bit palettised WinG title.
	initGraphics(kScreenWidth, kScreenHeight);

	_console = new Console(this);
	setDebugger(_console);

	// Assets live in the DATA subdirectory of the installed game; the intro and
	// other full-screen movies live alongside it in MOVIES.
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	if (getGameType() == GType_Titanic) {
		Common::FSNode cd1Root;
		if (findExtractedCDRoot("Titanic1", cd1Root)) {
			SearchMan.addSubDirectoryMatching(cd1Root, "data");
			SearchMan.addSubDirectoryMatching(cd1Root, "movies");
			_pathSlots[0] = canonicalCDLabel(cd1Root.getName()) + ":";
			_currentCD = canonicalCDLabel(cd1Root.getName());
		}
	} else {
		SearchMan.addSubDirectoryMatching(gameDataDir, "data");
		SearchMan.addSubDirectoryMatching(gameDataDir, "movies");
		if (gameDataDir.getName().equalsIgnoreCase("titanic1") ||
				gameDataDir.getName().equalsIgnoreCase("titanic2")) {
			_pathSlots[0] = gameDataDir.getName() + ":";
			_currentCD = canonicalCDLabel(gameDataDir.getName());
		}
	}

	if (getGameType() == GType_Titanic && !validateTitanicDiscLayout())
		return Common::kNoGameDataFoundError;

	// Clear to black before the boot script paints anything.
	byte palette[3 * 256];
	memset(palette, 0, sizeof(palette));
	_system->getPaletteManager()->setPalette(palette, 0, 256);

	Graphics::Surface *screen = _system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	_system->unlockScreen();
	_system->updateScreen();

	// Drive the boot script. BOOTFILE holds the master header plus two script
	// resources (info tag 0x0FA1): res1 is the boot script (its first
	// definition `boot()` runs engine setup, the CD check, the intro movies
	// and the menu branch; later definitions are event handlers like
	// keydown()), and res2 is the GLOBAL function library (changeset, initall,
	// advanceday, advancetour, ... — see files/decomp/stage-notes.md). Both
	// stay registered on the VM's dispatch scope chain for the lifetime of the
	// session, mirroring the TI.EXE chain [current script, global library]
	// built by FUN_0040ad80.
	Common::File bootFile;
	if (!bootFile.open("BOOTFILE")) {
		warning("Cyberflix: could not locate DATA/BOOTFILE");
		return Common::kNoGameDataFoundError;
	}

	Archive boot;
	if (!boot.open(bootFile.readStream(bootFile.size()), "BOOTFILE")) {
		warning("Cyberflix: BOOTFILE present but failed LPPALPPA validation");
		return Common::kUnknownError;
	}

	for (uint32 i = 0; i < boot.getResourceCount(); ++i) {
		const Archive::Resource &res = boot.getResource(i);
		if (res.empty || res.info != Script::kScriptInfoTag)
			continue;
		Common::SeekableReadStream *scriptStream = boot.createReadStreamForResource(i);
		Common::ScopedPtr<Script> script(new Script());
		bool parsed = scriptStream && script->parse(scriptStream);
		delete scriptStream;
		if (!parsed) {
			warning("Cyberflix: failed to parse BOOTFILE script resource %u", i);
			continue;
		}
		if (!_bootScript)
			_bootScript.reset(script.release());
		else if (!_globalLib)
			_globalLib.reset(script.release());
	}
	if (!_bootScript) {
		warning("Cyberflix: BOOTFILE has no script resource");
		return Common::kUnknownError;
	}

	if (!exciseBootCdCheck(*_bootScript))
		warning("Cyberflix: boot script CD check not found; running unmodified");

	_vm.setHost(this);
	// Searched newest-first: boot res1 handlers shadow the global library,
	// matching the per-dispatch chain order in TI.EXE FUN_0040ad80.
	if (_globalLib)
		_vm.addLibrary(_globalLib.get());
	_vm.addLibrary(_bootScript.get());
	_vm.runProgram(*_bootScript);

	// Main interactive loop, mirroring TI.EXE FUN_0043b040 + the system-target
	// event handler FUN_00438680: every pass delivers an idle tick (case 9
	// dispatches the "idle()" message to the boot script — its handler polls
	// mouse()/hittest() and sends setcursor to whatever is under the cursor),
	// and each WM_LBUTTONDOWN becomes a "mousedown(<packed point>)" boot
	// message (case 0 via FUN_00438e90, which formats the point as one int).
	// The boot handlers route the hits onward (sendtoprop(name, mousedown)...).
	Common::Event event;
	while (!shouldQuit()) {
		while (_eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_LBUTTONDOWN) {
				const int32 packed = ((int32)(int16)event.mouse.x << 16) |
						((int32)event.mouse.y & 0xffff);
				Common::Array<Value> args;
				args.push_back(Value::makeInt(packed));
				bool handled = false;
				_vm.callFunction("mousedown", args, &handled);
				if (!handled)
					warning("Cyberflix: boot script has no mousedown handler");
			} else if (event.type == Common::EVENT_KEYDOWN) {
				if (handleGlobalKey(event))
					continue;
				if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
					continue;
				if (event.kbd.flags & (Common::KBD_CTRL | Common::KBD_ALT))
					continue;
				Common::String key;
				switch (event.kbd.keycode) {
				case Common::KEYCODE_LEFT:
					key = "leftarrow";
					break;
				case Common::KEYCODE_UP:
					key = "uparrow";
					break;
				case Common::KEYCODE_RIGHT:
					key = "rightarrow";
					break;
				case Common::KEYCODE_DOWN:
					key = "downarrow";
					break;
				default:
					if (event.kbd.ascii > 0 && event.kbd.ascii < 256)
						key = Common::String::format("%c", (char)event.kbd.ascii);
					break;
				}
				if (!key.empty()) {
					Common::Array<Value> args;
					args.push_back(Value::makeString(key));
					bool handled = false;
					_vm.callFunction(event.kbdRepeat ? "keyrepeat" : "keydown", args, &handled);
					if (!handled)
						warning("Cyberflix: boot script has no %s handler",
								event.kbdRepeat ? "keyrepeat" : "keydown");
					refreshPropsIfDirty();
				}
			}
		}
		if (processPendingLoad())
			continue;
		bool handled = false;
		_idleForceUpdatePresented = false;
		_vm.callFunction("idle", Common::Array<Value>(), &handled);
		const bool propsDirtyAfterIdle = _propsDirty;
		refreshPropsIfDirty();
		if (!_idleForceUpdatePresented || propsDirtyAfterIdle)
			_system->updateScreen();
		if (_frameRate == 0 && _setTransitionType == kSetTransitionNone)
			_system->delayMillis(10);
	}

	return Common::kNoError;
}

bool CyberflixEngine::exciseBootCdCheck(Script &script) {
	const uint32 n = script.getInstructionCount();

	// The CD presence check compares a path against the "titanic1:" CD volume
	// literal. Locate that literal, then the if-block that encloses it.
	int literal = -1;
	for (uint32 i = 0; i < n; ++i) {
		uint16 op = script.getInstruction(i).opcode;
		if (op == Script::kOpPush3 || op == Script::kOpPush4 || op == Script::kOpPushSym) {
			if (script.getSelfRelString(i).equalsIgnoreCase("titanic1:")) {
				literal = (int)i;
				break;
			}
		}
	}
	if (literal < 0)
		return false;

	// Walk back to the nearest enclosing kOpIf (the literal sits in its
	// condition), balancing any nested if/endif pairs in between.
	int ifIndex = -1;
	int depth = 0;
	for (int i = literal - 1; i >= 0; --i) {
		uint16 op = script.getInstruction((uint32)i).opcode;
		if (op == Script::kOpEndIf) {
			++depth;
		} else if (op == Script::kOpIf) {
			if (depth == 0) {
				ifIndex = i;
				break;
			}
			--depth;
		}
	}
	if (ifIndex < 0)
		return false;

	int endIfIndex = script.findMatchingEndIf((uint32)ifIndex);
	if (endIfIndex < 0)
		return false;

	script.neutralizeRange((uint32)ifIndex, (uint32)endIfIndex);
	debug(0, "Cyberflix: excised boot CD check (instructions %d..%d)",
			ifIndex, endIfIndex);
	return true;
}

uint32 CyberflixEngine::handleMovieHotkeys(const Common::Event &event, bool skippable,
		const Audio::SoundHandle &audioHandle, bool &skip) {
	if (event.type != Common::EVENT_KEYDOWN)
		return 0;

	if (handleGlobalKey(event))
		return 0;

	const Common::KeyCode kc = event.kbd.keycode;
	const bool ctrl = (event.kbd.flags & Common::KBD_CTRL) != 0;

	// SKIP. TI.EXE's WndProc (FUN_00403690) forces VK_ESCAPE into a '.' event
	// carrying the modifier word 0x1fa0; the movie key handler (FUN_0040e430,
	// case 0x2e/'.', 0x51/'Q', 0x71/'q') then aborts playback, but only when the
	// master header flags byte (+0x18) has bit 0 set. Ctrl is what sets 0x1fa0
	// for the letter keys, so the faithful combos are Esc, Ctrl+Q and Ctrl+period.
	if (kc == Common::KEYCODE_ESCAPE ||
			(ctrl && (kc == Common::KEYCODE_q || kc == Common::KEYCODE_PERIOD))) {
		if (skippable)
			skip = true;
		return 0;
	}

	// PAUSE / RESUME. FUN_0040e430 case 0x54/0x74 ('T'/'t', gated by the 0x1fa0
	// modifier) toggles the pause latch DAT_0045ef88 (FUN_0042f930 pauses the
	// audio, FUN_0042f690 resumes). We pause the soundtrack handle and freeze the
	// picture until Ctrl+T again (or quit), returning the elapsed paused time so
	// the caller can shift its wall clock.
	if (ctrl && kc == Common::KEYCODE_t) {
		const uint32 pauseStart = _system->getMillis();
		_mixer->pauseHandle(audioHandle, true);
		bool paused = true;
		Common::Event e2;
		while (paused && !shouldQuit()) {
			while (_eventMan->pollEvent(e2)) {
				if (e2.type == Common::EVENT_KEYDOWN &&
						(e2.kbd.flags & Common::KBD_CTRL) && e2.kbd.keycode == Common::KEYCODE_t)
					paused = false;
				else if (e2.type == Common::EVENT_KEYDOWN &&
						e2.kbd.keycode == Common::KEYCODE_ESCAPE && skippable) {
					skip = true;
					paused = false;
				}
			}
			_system->updateScreen();
			_system->delayMillis(10);
		}
		_mixer->pauseHandle(audioHandle, false);
		return _system->getMillis() - pauseStart;
	}

	// Open the ScummVM debug console (a development aid, not an original
	// shortcut): backquote or Ctrl+D.
	if (kc == Common::KEYCODE_BACKQUOTE || (ctrl && kc == Common::KEYCODE_d))
		_console->attach();

	// NOT IMPLEMENTED (no faithful analog in our pre-mixed, single-stream audio):
	//
	// * Ctrl+0..9 audio channel/level select. FUN_0040e430 case 0x30..0x39 ->
	//   FUN_0042f620(n) -> FUN_0042f630: DAT_00460a54 = (long)n + DAT_00460a38,
	//   then invokes the DirectSound mixer callback (*DAT_00460ab8)(). To
	//   implement: reproduce that mixer object so the index has a target; with a
	//   single pre-mixed stream there is nothing to address.

	return 0;
}

bool CyberflixEngine::handleGlobalKey(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return false;

	const Common::KeyCode kc = event.kbd.keycode;
	bool handled = true;
	switch (kc) {
	case Common::KEYCODE_F1:
		for (int i = 0; i < 3; ++i)
			_paletteGamma[i] *= kPaletteGammaDown;
		break;
	case Common::KEYCODE_F2:
		for (int i = 0; i < 3; ++i)
			_paletteGamma[i] *= kPaletteGammaUp;
		break;
	case Common::KEYCODE_F3:
		_paletteGamma[0] *= kPaletteGammaDown;
		break;
	case Common::KEYCODE_F4:
		_paletteGamma[0] *= kPaletteGammaUp;
		break;
	case Common::KEYCODE_F5:
		_paletteGamma[1] *= kPaletteGammaDown;
		break;
	case Common::KEYCODE_F6:
		_paletteGamma[1] *= kPaletteGammaUp;
		break;
	case Common::KEYCODE_F7:
		_paletteGamma[2] *= kPaletteGammaDown;
		break;
	case Common::KEYCODE_F8:
		_paletteGamma[2] *= kPaletteGammaUp;
		break;
	case Common::KEYCODE_F9:
		for (int i = 0; i < 3; ++i)
			_paletteGamma[i] = kDefaultPaletteGamma;
		break;
	case Common::KEYCODE_F12:
		showAboutDialog();
		return true;
	default:
		handled = false;
		break;
	}

	if (!handled)
		return false;

	for (int i = 0; i < 3; ++i)
		_paletteGamma[i] = CLIP(_paletteGamma[i], kPaletteGammaMin, kPaletteGammaMax);
	_paletteGammaTableDirty = true;

	byte rgb[256 * 3];
	memcpy(rgb, _screenClut, sizeof(rgb));
	programPalette(rgb);
	_system->updateScreen();
	return true;
}

void CyberflixEngine::showAboutDialog() {
	// Faithful reproduction of TI.EXE FUN_00404120's "About" MessageBox (format
	// string @0x00457380, build stamp @0x004574d0). The original also appends
	// live OS/heap/joystick/audio lines; under ScummVM we show the static engine
	// identification (the meaningful, stable part) plus a ScummVM host note.
	GUI::MessageDialog dialog(
			"DreamFactory v4.0\n"
			"(C) Copyright 1993-1996 CyberFlix, Inc.\n"
			"All rights reserved.\n"
			"\n"
			"Windows RT4 engine, DirectX version\n"
			"Compiled Mar 10 1997 at 15:52:38\n"
			"\n"
			"Running under ScummVM");
	dialog.runModal();
}

bool CyberflixEngine::questionDialog(const Common::String &message) {
	GUI::MessageDialog dialog(message, "Yes", "No");
	return dialog.runModal() == GUI::kMessageOK;
}

void CyberflixEngine::requestQuit() {
	Engine::quitGame();
}

} // End of namespace Cyberflix
