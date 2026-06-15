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
#include "graphics/palette.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"
#include "graphics/wincursor.h"

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

// The runtime accesses every resource through a "record+8" data pointer (the
// info tag), which is four bytes before the payload that Archive exposes via
// dataOffset (== record+12). All master-header/table field offsets below are
// expressed in that record+8 frame, so we subtract 4 from dataOffset to get the
// engine's view. See files/decomp/movie-playback.md.
static const byte *engineBase(const Common::Array<byte> &fileData, const Archive::Resource &res) {
	if (res.empty || res.dataOffset < 4 || res.dataOffset > fileData.size())
		return nullptr;
	return fileData.begin() + res.dataOffset - 4;
}

// Read a Pascal string (1-byte length prefix) into a Common::String, bounded by
// the end of the file buffer.
static Common::String readPascalString(const byte *p, const Common::Array<byte> &fileData) {
	if (!p || p < fileData.begin() || p >= fileData.end())
		return Common::String();
	uint len = *p;
	const byte *s = p + 1;
	if (s + len > fileData.end())
		len = (uint)(fileData.end() - s);
	return Common::String((const char *)s, len);
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

static bool resolveCyberflixPathDir(const Common::String &path, Common::FSNode &out) {
	Common::Array<Common::String> components = splitCyberflixPath(path);
	if (components.empty())
		return false;

	Common::Array<Common::Array<Common::String> > patterns;
	patterns.push_back(components);
	if (components.size() > 1) {
		Common::Array<Common::String> tail;
		for (uint i = 1; i < components.size(); ++i)
			tail.push_back(components[i]);
		patterns.push_back(tail);
	}
	Common::Array<Common::String> finalComponent;
	finalComponent.push_back(components[components.size() - 1]);
	patterns.push_back(finalComponent);

	const Common::FSNode gameDir(ConfMan.getPath("path"));
	Common::FSNode roots[2] = { gameDir, gameDir.getParent() };
	for (uint r = 0; r < ARRAYSIZE(roots); ++r) {
		for (uint p = 0; p < patterns.size(); ++p) {
			if (findCaselessPathDir(roots[r], patterns[p], out))
				return true;
		}
	}
	return false;
}

// Sample-add an 8-bit unsigned mono SFX buffer into the music track at the given
// sample offset, extending the track with silence (0x80) if needed and clamping.
static void mixSfx(Common::Array<byte> &track, const Common::Array<byte> &sfx, uint32 atSample) {
	if (sfx.empty())
		return;
	uint32 end = atSample + sfx.size();
	while (track.size() < end)
		track.push_back(0x80);
	for (uint32 i = 0; i < sfx.size(); ++i) {
		int v = ((int)track[atSample + i] - 0x80) + ((int)sfx[i] - 0x80);
		v = CLIP(v, -128, 127);
		track[atSample + i] = (byte)(v + 0x80);
	}
}

static void playMovieFrameSfx(Audio::Mixer *mixer, Common::Array<Audio::SoundHandle> &handles,
		const Common::Array<byte> &pcm) {
	if (!mixer || pcm.empty())
		return;

	byte *buf = (byte *)malloc(pcm.size());
	if (!buf)
		return;
	memcpy(buf, pcm.begin(), pcm.size());

	Audio::SoundHandle handle;
	Audio::SeekableAudioStream *stream = Audio::makeRawStream(
			buf, pcm.size(), kAudioSampleRate, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
	if (!stream) {
		free(buf);
		return;
	}
	mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, stream);
	handles.push_back(handle);
}

// A clickable region on an interactive movie frame. The original player reads
// the count at event chunk +0x442 and 0x40-byte records at +0x446;
// FUN_0040d710 hit-tests the rect against the click point and runs the action.
// Field offsets within the record:
//   +0x00 u16 action (1=END, 2=GOTO target, 6=NEXT, 7=PREV),
//   +0x02 byte flags (bit0 => also require a per-pixel mask hit on click;
//         bit1 => hover-cursor eligible, see FUN_0040e5b0),
//   +0x08 QuickDraw rect {top, left, bottom, right} as int16: FUN_0041ac60
//         tests the packed point's low short (y) against rect[0]/rect[2] and
//         its high short (x) against rect[1]/rect[3]. (Verified in data:
//         PLAYMODE's GAME button rect {232,208,277,324} is 116 wide, 45 tall.)
//   +0x30 Pascal string = GOTO target frame name (for action 2).
struct MovieButton {
	uint16 action;
	byte flags;
	int16 left, top, right, bottom;
	Common::String target;
	bool contains(int x, int y) const {
		return x >= left && x < right && y >= top && y < bottom;
	}
};

// Resolve a frame name (as used by GOTO buttons) to its index in the per-frame
// table, mirroring FUN_0040e050. Returns -1 if not found.
static int resolveFrameName(const Common::Array<Common::String> &names, const Common::String &target) {
	for (uint i = 0; i < names.size(); ++i)
		if (names[i].equalsIgnoreCase(target))
			return (int)i;
	return -1;
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

// sendtostage(message(...)): deliver a message call to the stage's script
// scope chain. Mirrors TI.EXE FUN_0040ad80, which dispatches the unevaluated
// message against [stage script, BOOTFILE res2].
void CyberflixEngine::sendToStage(const Common::String &message, const Common::Array<Value> &args) {
	if (!_stage || !_stage->isOpen()) {
		warning("Cyberflix: sendtostage('%s') with no stage open", message.c_str());
		return;
	}
	Common::SharedPtr<Stage> dispatchStage = _stage;
	Common::Array<const Script *> scopes;
	scopes.push_back(dispatchStage->stageScript());
	dispatchWithScopeChain(scopes, dispatchStage->name(), Common::String(), message, args, "stage");
	refreshPropsIfDirty();
}

// sendtoboot(message(...)): dispatch against [BOOTFILE res1, BOOTFILE res2].
// CTL.STG's QUIT button reaches BOOTFILE res1 menuselect("quit") through this
// path. Mirrors TI.EXE FUN_00439080 -> FUN_004390a0.
void CyberflixEngine::sendToBoot(const Common::String &message, const Common::Array<Value> &args) {
	if (!_bootScript) {
		warning("Cyberflix: sendtoboot('%s') before BOOTFILE loaded", message.c_str());
		return;
	}
	Common::Array<const Script *> scopes;
	scopes.push_back(_bootScript.get());
	dispatchWithScopeChain(scopes, "bootfile", Common::String(), message, args, "boot");
	refreshPropsIfDirty();
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
	Common::Array<const Script *> scopes;
	scopes.push_back(dispatchStage->nodeScript((uint32)node));
	scopes.push_back(dispatchStage->stageScript());
	dispatchWithScopeChain(scopes, flatName, flatName, message, args, "flat");
	refreshPropsIfDirty();
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

void CyberflixEngine::renderStageNode(int node) {
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

	Graphics::Surface *screen = _system->lockScreen();
	screen->fillRect(Common::Rect(0, 0, kScreenWidth, kScreenHeight), 0);
	// Stage nodes are full-screen items: the compositor FUN_004436d0 clips
	// them against the whole screen rect DAT_00460d58, not the set viewport,
	// so they paint from the top-left corner (MAIN.STG is 512x384).
	for (int y = 0; y < frame.height; ++y) {
		for (int x = 0; x < frame.width; ++x) {
			if (x < kScreenWidth && y < kScreenHeight)
				*((byte *)screen->getBasePtr(x, y)) = frame.pixels[(uint)y * frame.width + x];
		}
	}
	// CTL.STG and other flats can place screen-space SHOP props over the stage
	// with propxy()/propvisible(); the native compositor draws those display
	// items after the stage backing buffer.
	Common::Array<const Shop::Prop *> draw;
	Common::Array<const Shop *> drawShop;
	collectScreenProps(draw, drawShop);
	for (uint32 i = 0; i < draw.size(); ++i) {
		CelImage cel;
		Common::Rect r;
		if (!drawShop[i]->renderProp(*draw[i], cel, r))
			continue;
		for (int y = 0; y < cel.height; ++y) {
			for (int x = 0; x < cel.width; ++x) {
				int sx = r.left + x, sy = r.top + y;
				if (sx >= 0 && sy >= 0 && sx < kScreenWidth && sy < kScreenHeight &&
						cel.isOpaque(x, y))
					*((byte *)screen->getBasePtr(sx, sy)) = cel.pixels[(uint)y * cel.width + x];
			}
		}
	}
	_system->unlockScreen();

	// Show the default arrow over the rendered node until per-node hotspot
	// hit-testing (directional cursors) is implemented.
	if (setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
	_dirtyRects.clear();
	_propsDirty = false;
	_system->updateScreen();

	debug(1, "Cyberflix: rendered stage '%s' node %d (%ux%u)",
			_stage->name().c_str(), node, frame.width, frame.height);
}

void CyberflixEngine::repaintDirtyStageRects() {
	if (!_stage || !_stage->isOpen() || _dirtyRects.empty())
		return;

	FrameImage frame;
	if (!_stage->renderNode((uint32)_stageNode, frame)) {
		renderStageNode(_stageNode);
		return;
	}

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
			CelImage cel;
			Common::Rect propRect;
			if (!drawShop[i]->renderProp(*draw[i], cel, propRect))
				continue;
			if (!dirty.intersects(propRect))
				continue;
			Common::Rect paint = dirty.findIntersectingRect(propRect);
			for (int y = paint.top; y < paint.bottom; ++y) {
				for (int x = paint.left; x < paint.right; ++x) {
					if (cel.isOpaque(x - propRect.left, y - propRect.top))
						*((byte *)screen->getBasePtr(x, y)) =
								cel.pixels[(uint)(y - propRect.top) * cel.width + (x - propRect.left)];
				}
			}
		}
	}
	_system->unlockScreen();

	if (setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
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
				renderStageNode(_stageNode);
		}
	}
	return _setVisible;
}

Common::String CyberflixEngine::currentPuppet() {
	return "none";
}

// ---- Shop/prop subsystem (TI.EXE FUN_00428450 and friends) ----------------
// RE notes: files/renderer-notes.md "Shop/prop subsystem". The original keeps
// one global prop array across all open shops; here the by-name lookups and
// countprops/indextoprop span _shops in open order, which preserves the
// global-index semantics (shops are only ever appended).

Shop *CyberflixEngine::findShop(const Common::String &name) {
	Common::SharedPtr<Shop> shop = findShopShared(name);
	return shop.get();
}

Common::SharedPtr<Shop> CyberflixEngine::findShopShared(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _shops.size(); ++i)
		if (_shops[i]->name() == key)
			return _shops[i];
	return Common::SharedPtr<Shop>();
}

Shop::Prop *CyberflixEngine::findProp(const Common::String &name, Shop **shopOut) {
	Shop::Prop *prop = nullptr;
	Common::SharedPtr<Shop> shop = findPropOwnerShared(name, &prop);
	if (shopOut)
		*shopOut = shop.get();
	return prop;
}

Common::SharedPtr<Shop> CyberflixEngine::findPropOwnerShared(const Common::String &name,
		Shop::Prop **propOut) {
	for (uint32 i = 0; i < _shops.size(); ++i) {
		Shop::Prop *prop = _shops[i]->findProp(name);
		if (prop) {
			if (propOut)
				*propOut = prop;
			return _shops[i];
		}
	}
	if (propOut)
		*propOut = nullptr;
	return Common::SharedPtr<Shop>();
}

void CyberflixEngine::collectScreenProps(Common::Array<const Shop::Prop *> &draw,
		Common::Array<const Shop *> &drawShop) {
	for (uint32 s = 0; s < _shops.size(); ++s) {
		// BOOTFILE transtoflat() hides HOUSE.SHP's main-stage interface props
		// before opening closeup stages; do not let stale state hit-test through.
		if (_stage && _stage->isOpen() && !_stage->name().equalsIgnoreCase("main.stg") &&
				!_stage->name().equalsIgnoreCase("ctl.stg") &&
				_shops[s]->name().equalsIgnoreCase("house.shp"))
			continue;
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

bool CyberflixEngine::screenPropRect(const Shop &shop, const Shop::Prop &prop, Common::Rect &rect) const {
	if (!prop.visible || prop.mode != 0)
		return false;
	if (_stage && _stage->isOpen() && !_stage->name().equalsIgnoreCase("main.stg") &&
			!_stage->name().equalsIgnoreCase("ctl.stg") &&
			shop.name().equalsIgnoreCase("house.shp"))
		return false;

	CelImage cel;
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
		CelImage cel;
		Common::Rect r;
		if (!drawShop[i]->renderProp(*draw[i], cel, r))
			continue;
		if (x < r.left || x >= r.right || y < r.top || y >= r.bottom)
			continue;
		if (!cel.isOpaque(x - r.left, y - r.top))
			continue;
		_hitKind = "prop";
		return draw[i]->name;
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
// The 0xfba/0xfbb context atoms are saved and restored around the call.
void CyberflixEngine::dispatchWithScopes(const Script *scope1, const Script *scope2,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args) {
	Common::Array<const Script *> scopes;
	if (scope1)
		scopes.push_back(scope1);
	if (scope2)
		scopes.push_back(scope2);
	dispatchWithScopeChain(scopes, self, targetProp, message, args, "shop/prop");
}

void CyberflixEngine::dispatchWithScopeChain(const Common::Array<const Script *> &scopes,
		const Common::String &self, const Common::String &targetProp,
		const Common::String &message, const Common::Array<Value> &args,
		const char *debugContext) {
	Common::String prevSelf = _vm.contextSelf();
	Common::String prevProp = _vm.contextProp();
	Common::Array<const Script *> chain;
	if (_globalLib)
		chain.push_back(_globalLib.get()); // "System: " tail, searched last
	for (int i = (int)scopes.size() - 1; i >= 0; --i)
		if (scopes[(uint32)i])
			chain.push_back(scopes[(uint32)i]);
	Common::Array<const Script *> prevChain = _vm.swapLibraries(chain);
	_vm.setDispatchContext(self, targetProp);

	bool handled = false;
	_vm.callFunction(message, args, &handled);
	if (!handled)
		debug(1, "Cyberflix: %s message '%s' unhandled", debugContext, message.c_str());

	_vm.setDispatchContext(prevSelf, prevProp);
	_vm.swapLibraries(prevChain);
}

void CyberflixEngine::dispatchSetMessage(const Common::String &message, const Common::Array<Value> &args) {
	if (!_set || !_set->isOpen() || message.empty())
		return;
	Common::Array<Common::SharedPtr<Script> > keepAlive;
	keepAlive.push_back(_set->setScriptShared());
	Common::Array<const Script *> scopes;
	scopes.push_back(keepAlive[0].get());
	dispatchWithScopeChain(scopes, _set->setName(), Common::String(), message, args, "set");
}

void CyberflixEngine::dispatchSceneMessage(uint32 scene, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!_set || !_set->isOpen() || message.empty())
		return;
	Common::Array<Common::SharedPtr<Script> > keepAlive;
	keepAlive.push_back(_set->sceneScriptShared(scene));
	keepAlive.push_back(_set->setScriptShared());
	Common::Array<const Script *> scopes;
	scopes.push_back(keepAlive[0].get());
	scopes.push_back(keepAlive[1].get());
	dispatchWithScopeChain(scopes, _set->sceneName(scene), Common::String(), message, args, "scene");
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
	if (prop->shapeName != key) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
		prop->shapeName = key;
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
	if (!_setVisible || isReplacementStage(_stage)) {
		if (_stage && _stage->isOpen()) {
			if (!_dirtyRects.empty())
				repaintDirtyStageRects();
			else
				renderStageNode(_stageNode);
		}
		_dirtyRects.clear();
		_propsDirty = false;
		return;
	}
	if (_set && _set->isOpen() && _setScene >= 0)
		renderSetScene(_setScene, _setAngle);
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

CyberflixEngine::ThemeTrack *CyberflixEngine::findTrack(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint i = 0; i < _tracks.size(); ++i)
		if (_tracks[i]->name == key)
			return _tracks[i].get();
	return nullptr;
}

const CyberflixEngine::ThemeTrack::Cue *CyberflixEngine::findSfxCue(const Common::String &name,
		ThemeTrack **trackOut) {
	for (uint i = 0; i < _tracks.size(); ++i) {
		for (uint j = 0; j < _tracks[i]->sfxCues.size(); ++j) {
			if (_tracks[i]->sfxCues[j].name.equalsIgnoreCase(name)) {
				if (trackOut)
					*trackOut = _tracks[i].get();
				return &_tracks[i]->sfxCues[j];
			}
		}
	}
	return nullptr;
}

CyberflixEngine::ThemeTrack::Cue *CyberflixEngine::findMutableSfxCue(const Common::String &name,
		ThemeTrack **trackOut) {
	for (uint i = 0; i < _tracks.size(); ++i) {
		for (uint j = 0; j < _tracks[i]->sfxCues.size(); ++j) {
			if (_tracks[i]->sfxCues[j].name.equalsIgnoreCase(name)) {
				if (trackOut)
					*trackOut = _tracks[i].get();
				return &_tracks[i]->sfxCues[j];
			}
		}
	}
	return nullptr;
}

byte CyberflixEngine::effectiveAudioVolume(int baseVolume) const {
	return (byte)CLIP(baseVolume, 0, 255) * CLIP(_waveVolumeLevel, 0, 9) / 9;
}

void CyberflixEngine::applyLiveAudioVolumes() {
	if (!_themeTrackName.empty() && _mixer->isSoundHandleActive(_themeHandle)) {
		ThemeTrack *track = findTrack(_themeTrackName);
		_mixer->setChannelVolume(_themeHandle,
				effectiveAudioVolume(track ? track->volume : 255));
	}

	for (uint i = 0; i < ARRAYSIZE(_soundSlots); ++i) {
		if (_mixer->isSoundHandleActive(_soundSlots[i].handle)) {
			const ThemeTrack::Cue *cue = findSfxCue(_soundSlots[i].cueName);
			_mixer->setChannelVolume(_soundSlots[i].handle,
					effectiveAudioVolume(cue ? cue->volume : 255));
		}
	}
	if (_mixer->isSoundHandleActive(_voiceSlot.handle)) {
		const ThemeTrack::Cue *cue = findSfxCue(_voiceSlot.cueName);
		_mixer->setChannelVolume(_voiceSlot.handle,
				effectiveAudioVolume(cue ? cue->volume : 255));
	}
}

// opentrackfile('name.trk'): load and parse a track file, appending it to the
// open-track list (TI.EXE FUN_00411be0 -> parser FUN_00411cc0, list
// DAT_0046114c), including its theme and SFX cue directories.
//
// .TRK payload fields are read from the "record+8" base (the info dword is
// part of the master header there, unlike the MOV record+12 view): res0
// master header B: theme-table res id u32 @B+0x1c, pascal track name @B+0x24.
// Theme table T: loop index u32 @T+0, playlist length u16 @T+4, playlist
// u16[] @T+6 (1-based cue indices in play order), cue count u32 @T+0x10a, cue
// records @T+0x10e stride 0x1a { u32 ?, u32 resId @+4, pascal name @+0xa }.
// SFX table S: count u32 @S+4, records @S+8 stride 0x1a
// { flags @+0, u32 resId @+4, pascal name @+0xa }.
// See files/audio-re-notes.md.
void CyberflixEngine::openTrackFile(const Common::String &name) {
	if (name.empty())
		return;

	Common::SharedPtr<ThemeTrack> track(new ThemeTrack());
	track->sourceName = name;
	track->sourceName.toLowercase();
	track->name = name;
	track->name.toLowercase();

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open track file '%s'", name.c_str());
		return;
	}
	uint32 size = (uint32)file.size();
	track->fileData.resize(size);
	if (file.read(track->fileData.begin(), size) != size) {
		warning("Cyberflix: could not read track file '%s'", name.c_str());
		return;
	}
	file.close();

	Archive archive;
	if (!archive.open(new Common::MemoryReadStream(track->fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid track container", name.c_str());
		return;
	}

	const byte *master = archive.getResourceCount()
			? engineBase(track->fileData, archive.getResource(0)) : nullptr;
	if (!master || master + 0x28 > track->fileData.end()) {
		warning("Cyberflix: track '%s' has no master header", name.c_str());
		return;
	}
	Common::String logicalName = readPascalString(master + 0x24, track->fileData);
	if (!logicalName.empty()) {
		track->name = logicalName;
		track->name.toLowercase();
	}
	uint32 themeTableId = READ_LE_UINT32(master + 0x1c);
	uint32 sfxTableId = READ_LE_UINT32(master + 0x20);
	const byte *tt = (themeTableId < archive.getResourceCount())
			? engineBase(track->fileData, archive.getResource(themeTableId)) : nullptr;
	if (!tt || tt + 0x10e > track->fileData.end()) {
		warning("Cyberflix: track '%s' has no theme table", name.c_str());
		return;
	}

	track->loopIdx = READ_LE_UINT32(tt);
	uint16 playlistLen = READ_LE_UINT16(tt + 4);
	for (uint i = 0; i < playlistLen && tt + 6 + 2 * i + 2 <= track->fileData.end(); ++i)
		track->playlist.push_back(READ_LE_UINT16(tt + 6 + 2 * i));
	// FUN_00411cc0 clamps the loop target into the playlist.
	if (!track->playlist.empty() && track->loopIdx >= track->playlist.size())
		track->loopIdx = track->playlist.size() - 1;

	uint32 themeCueCount = READ_LE_UINT32(tt + 0x10a);
	for (uint32 i = 0; i < themeCueCount; ++i) {
		const byte *rec = tt + 0x10e + 0x1a * i;
		if (rec + 0x1a > track->fileData.end())
			break;
		ThemeTrack::Cue cue;
		uint32 resId = READ_LE_UINT32(rec + 4);
		cue.resId = resId;
		cue.name = readPascalString(rec + 0xa, track->fileData);
		if (resId < archive.getResourceCount() && !archive.getResource(resId).empty) {
			cue.dataOffset = archive.getResource(resId).dataOffset;
			cue.length = archive.getResource(resId).length;
		}
		track->cues.push_back(cue);
	}

	const byte *st = (sfxTableId < archive.getResourceCount())
			? engineBase(track->fileData, archive.getResource(sfxTableId)) : nullptr;
	if (st && st + 8 <= track->fileData.end()) {
		uint32 sfxCueCount = READ_LE_UINT32(st + 4);
		for (uint32 i = 0; i < sfxCueCount; ++i) {
			const byte *rec = st + 8 + 0x1a * i;
			if (rec + 0x1a > track->fileData.end())
				break;
			ThemeTrack::Cue cue;
			cue.flags = rec[0];
			cue.resId = READ_LE_UINT32(rec + 4);
			cue.name = readPascalString(rec + 0xa, track->fileData);
			if (cue.resId < archive.getResourceCount() && !archive.getResource(cue.resId).empty) {
				cue.dataOffset = archive.getResource(cue.resId).dataOffset;
				cue.length = archive.getResource(cue.resId).length;
			}
			track->sfxCues.push_back(cue);
		}
	}

	_tracks.push_back(track);
	debug(1, "Cyberflix: track '%s' open as '%s' (%u theme cues, %u sfx cues, playlist %u, loop @%u)",
			name.c_str(), track->name.c_str(), (uint32)track->cues.size(),
			(uint32)track->sfxCues.size(), (uint32)track->playlist.size(), track->loopIdx);
}

// closetrackfile('name.trk'): remove the named track from the open list
// (TI.EXE FUN_00412070; it does not stop a theme already streaming, and
// neither do we -- the PCM was decoded up front).
void CyberflixEngine::closeTrackFile(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint i = 0; i < _tracks.size(); ++i) {
		if (_tracks[i]->name == key) {
			_tracks.remove_at(i);
			return;
		}
	}
}

// playtheme('name.trk'): start the track's theme playlist on the theme
// channel, replacing whatever is playing (TI.EXE FUN_00412250 ->
// FUN_0042f930/FUN_0042f960). The original streams the cue chain via the
// servicer thread: playlist entries in order, the last one's next-pointer
// aimed back at playlist[loopIdx], so cues before the loop index play once
// and the tail loops forever. We decode the same two regions to PCM and play
// them as intro + looped streams on a queuing stream.
void CyberflixEngine::playTheme(const Common::String &name) {
	ThemeTrack *track = findTrack(name);
	if (!track) {
		warning("Cyberflix: playtheme('%s'): track not open", name.c_str());
		return;
	}

	_mixer->stopHandle(_themeHandle);
	_themeTrackName.clear();
	_themeSpans.clear();
	_themeIntroSamples = _themeLoopSamples = 0;
	_themeStartSample = 0;
	if (track->playlist.empty())
		return;

	// Decode the intro (playlist[0..loopIdx-1]) and loop (playlist[loopIdx..])
	// regions, recording each cue's start for currenttheme(1).
	Common::Array<byte> intro, loop;
	for (uint i = 0; i < track->playlist.size(); ++i) {
		bool inLoop = (i >= track->loopIdx);
		Common::Array<byte> &out = inLoop ? loop : intro;
		uint16 cueIdx = track->playlist[i]; // 1-based
		if (cueIdx < 1 || cueIdx > track->cues.size())
			continue;
		const ThemeTrack::Cue &cue = track->cues[cueIdx - 1];
		ThemeCueSpan span;
		span.startSample = (inLoop ? _themeIntroSamples : 0) + out.size();
		span.name = cue.name;
		_themeSpans.push_back(span);
		if (cue.length && cue.dataOffset + cue.length <= track->fileData.size())
			decodeCbxAudio(track->fileData.begin() + cue.dataOffset, cue.length, out);
		if (!inLoop)
			_themeIntroSamples = intro.size();
	}
	_themeLoopSamples = loop.size();
	if (intro.empty() && loop.empty())
		return;

	Audio::QueuingAudioStream *queue = Audio::makeQueuingAudioStream(kAudioSampleRate, false);
	if (!intro.empty()) {
		byte *buf = (byte *)malloc(intro.size());
		memcpy(buf, intro.begin(), intro.size());
		queue->queueBuffer(buf, intro.size(), DisposeAfterUse::YES, Audio::FLAG_UNSIGNED);
	}
	if (!loop.empty()) {
		byte *buf = (byte *)malloc(loop.size());
		memcpy(buf, loop.begin(), loop.size());
		Audio::SeekableAudioStream *loopStream = Audio::makeRawStream(
				buf, loop.size(), kAudioSampleRate, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
		queue->queueAudioStream(new Audio::LoopingAudioStream(loopStream, 0), DisposeAfterUse::YES);
	}
	queue->finish();

	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_themeHandle, queue);
	_mixer->setChannelVolume(_themeHandle, effectiveAudioVolume(track->volume));
	_themeTrackName = track->name;
	debug(1, "Cyberflix: playtheme '%s' (intro %u + loop %u samples, vol %d)",
			name.c_str(), _themeIntroSamples, _themeLoopSamples, track->volume);
}

// halttheme(): stop the theme channel (TI.EXE FUN_00412410 -> FUN_0042f690).
void CyberflixEngine::haltTheme() {
	_mixer->stopHandle(_themeHandle);
	_themeTrackName.clear();
	_themeSpans.clear();
	_themeIntroSamples = _themeLoopSamples = 0;
	_themeStartSample = 0;
}

bool CyberflixEngine::playSoundCue(const Common::String &name, Audio::SoundHandle &handle,
		Common::String &currentCue, uint32 &currentResId) {
	ThemeTrack *track = nullptr;
	const ThemeTrack::Cue *cue = findSfxCue(name, &track);
	if (!cue || !track || cue->length == 0 || cue->dataOffset + cue->length > track->fileData.size()) {
		warning("Cyberflix: sound cue '%s' not found", name.c_str());
		return false;
	}

	Common::Array<byte> pcm;
	decodeCbxAudio(track->fileData.begin() + cue->dataOffset, cue->length, pcm);
	if (pcm.empty())
		return false;

	byte *buf = (byte *)malloc(pcm.size());
	memcpy(buf, pcm.begin(), pcm.size());
	Audio::SeekableAudioStream *stream = Audio::makeRawStream(
			buf, pcm.size(), kAudioSampleRate, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
	_mixer->stopHandle(handle);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, stream);
	_mixer->setChannelVolume(handle, effectiveAudioVolume(cue->volume));
	currentCue = cue->name;
	currentResId = cue->resId;
	return true;
}

// singlesound/multiplesound/dualsound/bothsound: play a named SFX cue on the
// two normal sound slots. Slot selection follows FUN_0042fa80/FUN_0042fb20/
// FUN_0042fbc0/FUN_0042fc30, using the cue resource id as the native priority
// key when both slots are occupied.
void CyberflixEngine::playSound(const Common::String &name, int mode) {
	const ThemeTrack::Cue *cue = findSfxCue(name);
	if (!cue) {
		warning("Cyberflix: sound cue '%s' not found", name.c_str());
		return;
	}

	bool active0 = _mixer->isSoundHandleActive(_soundSlots[0].handle);
	bool active1 = _mixer->isSoundHandleActive(_soundSlots[1].handle);
	if (!active0) {
		_soundSlots[0].cueName.clear();
		_soundSlots[0].resId = 0;
	}
	if (!active1) {
		_soundSlots[1].cueName.clear();
		_soundSlots[1].resId = 0;
	}

	auto playSlot = [&](int slot) {
		playSoundCue(name, _soundSlots[slot].handle, _soundSlots[slot].cueName, _soundSlots[slot].resId);
	};

	switch (mode) {
	case 0: // singlesound
		if ((active0 && _soundSlots[0].resId == cue->resId) ||
				(active1 && _soundSlots[1].resId == cue->resId))
			return;
		if (!active0)
			playSlot(0);
		else if (!active1)
			playSlot(1);
		else if (_soundSlots[0].resId < cue->resId)
			playSlot(0);
		else if (_soundSlots[1].resId < cue->resId)
			playSlot(1);
		break;
	case 1: // multiplesound
		if (active0 && _soundSlots[0].resId == cue->resId)
			playSlot(0);
		else if (active1 && _soundSlots[1].resId == cue->resId)
			playSlot(1);
		else if (!active0)
			playSlot(0);
		else if (!active1)
			playSlot(1);
		else if (_soundSlots[0].resId < _soundSlots[1].resId) {
			if (_soundSlots[0].resId < cue->resId)
				playSlot(0);
		} else if (_soundSlots[1].resId < cue->resId) {
			playSlot(1);
		}
		break;
	case 2: // dualsound
		if (!active0 || _soundSlots[0].resId < cue->resId)
			playSlot(0);
		if (!active1 || _soundSlots[1].resId < cue->resId)
			playSlot(1);
		break;
	case 3: // bothsound
		playSlot(0);
		playSlot(1);
		break;
	}
}

// voicesound(name): play a named SFX cue on the dedicated voice slot.
void CyberflixEngine::playVoice(const Common::String &name) {
	playSoundCue(name, _voiceSlot.handle, _voiceSlot.cueName, _voiceSlot.resId);
}

// haltsound(which): which==1 stops slot 1, 2 stops slot 2, 3 stops both.
void CyberflixEngine::haltSound(int which) {
	if (which < 1 || which > 3) {
		warning("Cyberflix: haltsound(%d): invalid slot", which);
		return;
	}
	if (which == 1 || which == 3) {
		_mixer->stopHandle(_soundSlots[0].handle);
		_soundSlots[0].cueName.clear();
		_soundSlots[0].resId = 0;
	}
	if (which == 2 || which == 3) {
		_mixer->stopHandle(_soundSlots[1].handle);
		_soundSlots[1].cueName.clear();
		_soundSlots[1].resId = 0;
	}
}

void CyberflixEngine::haltVoice() {
	_mixer->stopHandle(_voiceSlot.handle);
	_voiceSlot.cueName.clear();
	_voiceSlot.resId = 0;
}

// themevol('name.trk', 0-255): set the volume of every cue of the named track
// and apply it live to a playing cue (TI.EXE FUN_004125c0 -> FUN_004300c0 ->
// IDirectSoundBuffer::SetVolume). The 0-255 scale matches the mixer's.
void CyberflixEngine::themeVolume(const Common::String &name, int volume) {
	ThemeTrack *track = findTrack(name);
	if (track)
		track->volume = CLIP(volume, 0, 255);
	Common::String key = name;
	key.toLowercase();
	if (key == _themeTrackName && _mixer->isSoundHandleActive(_themeHandle))
		_mixer->setChannelVolume(_themeHandle, effectiveAudioVolume(volume));
}

int CyberflixEngine::waveVolume(const int *newLevel) {
	if (newLevel) {
		_waveVolumeLevel = CLIP(*newLevel, 0, 9);
		applyLiveAudioVolumes();
	}
	return _waveVolumeLevel;
}

int CyberflixEngine::soundVolume(const Common::String &name, const int *newVolume) {
	ThemeTrack::Cue *cue = findMutableSfxCue(name);
	if (!cue) {
		ThemeTrack *track = findTrack(name);
		if (track) {
			if (newVolume)
				for (uint i = 0; i < track->sfxCues.size(); ++i)
					track->sfxCues[i].volume = CLIP(*newVolume, 0, 255);
			applyLiveAudioVolumes();
			return track->sfxCues.empty() ? track->volume : track->sfxCues[0].volume;
		}
		warning("Cyberflix: soundvol('%s'): cue/track not found", name.c_str());
		return 0;
	}

	if (newVolume) {
		cue->volume = CLIP(*newVolume, 0, 255);
		for (uint i = 0; i < ARRAYSIZE(_soundSlots); ++i)
			if (_soundSlots[i].cueName.equalsIgnoreCase(cue->name) &&
					_mixer->isSoundHandleActive(_soundSlots[i].handle))
				_mixer->setChannelVolume(_soundSlots[i].handle,
						effectiveAudioVolume(cue->volume));
		if (_voiceSlot.cueName.equalsIgnoreCase(cue->name) &&
				_mixer->isSoundHandleActive(_voiceSlot.handle))
			_mixer->setChannelVolume(_voiceSlot.handle,
					effectiveAudioVolume(cue->volume));
	}
	return cue->volume;
}

// currenttheme(which): which==1 -> the name of the cue now playing on the
// theme channel, which==2 -> its track file's name; 'none' when silent
// (TI.EXE FUN_00412f20). We map the channel's elapsed time onto the decoded
// cue spans, folding positions past the intro into the loop region.
Common::String CyberflixEngine::currentTheme(int which) {
	if (_themeTrackName.empty() || !_mixer->isSoundHandleActive(_themeHandle))
		return "none";
	if (which == 2)
		return _themeTrackName;
	// 8-bit mono at kAudioSampleRate: one sample per byte.
	uint32 sample = _themeStartSample + (uint32)((uint64)_mixer->getSoundElapsedTime(_themeHandle) *
			kAudioSampleRate / 1000);
	if (sample >= _themeIntroSamples && _themeLoopSamples)
		sample = _themeIntroSamples + (sample - _themeIntroSamples) % _themeLoopSamples;
	Common::String cueName = "none";
	for (uint i = 0; i < _themeSpans.size(); ++i) {
		if (_themeSpans[i].startSample <= sample)
			cueName = _themeSpans[i].name;
		else
			break;
	}
	return cueName;
}

// currentsound(which): query the two normal SFX slots. which==1/2 returns that
// slot; which==3 returns the active slot with the higher native cue resource id.
Common::String CyberflixEngine::currentSound(int which) {
	bool active0 = _mixer->isSoundHandleActive(_soundSlots[0].handle);
	bool active1 = _mixer->isSoundHandleActive(_soundSlots[1].handle);
	if (!active0) {
		_soundSlots[0].cueName.clear();
		_soundSlots[0].resId = 0;
	}
	if (!active1) {
		_soundSlots[1].cueName.clear();
		_soundSlots[1].resId = 0;
	}
	if (which == 1)
		return active0 && !_soundSlots[0].cueName.empty() ? _soundSlots[0].cueName : "None";
	if (which == 2)
		return active1 && !_soundSlots[1].cueName.empty() ? _soundSlots[1].cueName : "None";
	if (which == 3) {
		if (active0 && active1)
			return _soundSlots[1].resId < _soundSlots[0].resId ?
					_soundSlots[0].cueName : _soundSlots[1].cueName;
		if (active0 && !_soundSlots[0].cueName.empty())
			return _soundSlots[0].cueName;
		if (active1 && !_soundSlots[1].cueName.empty())
			return _soundSlots[1].cueName;
	}
	return "None";
}

Common::String CyberflixEngine::currentVoice() {
	if (_mixer->isSoundHandleActive(_voiceSlot.handle) && !_voiceSlot.cueName.empty())
		return _voiceSlot.cueName;
	_voiceSlot.cueName.clear();
	_voiceSlot.resId = 0;
	return "None";
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
// the built-in names "black"/"current", and "set"/"stage" which alias the
// palette embedded in the currently open file of that kind ("puppet" lands
// with the puppet subsystem). Named cluts registered by scripts land later.
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
void CyberflixEngine::programPalette(const byte (&rgb)[256 * 3]) {
	memcpy(_screenClut, rgb, sizeof(_screenClut));

	byte gammaTable[3][256];
	for (int c = 0; c < 3; ++c) {
		for (int i = 0; i < 256; ++i)
			gammaTable[c][i] = (byte)(pow(i / 255.0, _paletteGamma[c]) * 255.0); // trunc, like __ftol
	}

	byte hw[256 * 3];
	for (int i = 0; i < 256; ++i) {
		hw[i * 3 + 0] = gammaTable[0][_screenClut[i * 3 + 0]];
		hw[i * 3 + 1] = gammaTable[1][_screenClut[i * 3 + 1]];
		hw[i * 3 + 2] = gammaTable[2][_screenClut[i * 3 + 2]];
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

void CyberflixEngine::forceUpdate() {
	// forceupdate() (TI.EXE 0x2f14 -> FUN_00446910 -> FUN_00423a60): rebuild the
	// display list from LIVE prop visibility, step active SET transitions through
	// FUN_004420b0, composite, and present.
	refreshPropsIfDirty();
	advanceSetTransition();
	_system->updateScreen();
	debug(1, "Cyberflix: forceupdate()");
}

void CyberflixEngine::message(const Common::String &text) {
	debug(1, "Cyberflix message: %s", text.c_str());
}

void CyberflixEngine::flushEvents() {
	_eventMan->purgeMouseEvents();
	_eventMan->purgeKeyboardEvents();
}

void CyberflixEngine::drawString(const Common::String &text, int32 packedPoint, int color, int size) {
	const Graphics::Font *font = size >= 12
			? FontMan.getFontByUsage(Graphics::FontManager::kGUIFont)
			: FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);
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
	for (int s = 1; s <= steps && !shouldQuit(); ++s) {
		byte cur[256 * 3];
		for (int i = 0; i < 256 * 3; ++i)
			cur[i] = (byte)(from[i] + ((int)to[i] - (int)from[i]) * s / steps);
		programPalette(cur);
		_system->updateScreen();
		// One step per 60 Hz tick of the original's scaled timer.
		uint32 deadline = startMs + (uint32)((uint64)s * 1000 / 60);
		uint32 now = _system->getMillis();
		if (now < deadline)
			_system->delayMillis(deadline - now);
		Common::Event event;
		while (_eventMan->pollEvent(event))
			; // keep the window live; fades are not skippable in the original
	}
	programPalette(to);
	_system->updateScreen();
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
	if (_setVisible && _set && _set->isOpen() && _setScene >= 0) {
		if (_setTransitionType != kSetTransitionNone)
			advanceSetTransition();
		else
			renderSetScene(_setScene, _setTable, _setAngle, _setView);
	} else if (_stage && _stage->isOpen()) {
		renderStageNode(_stageNode);
		_propsDirty = false;
	} else {
		_system->updateScreen();
	}

	debug(1, "Cyberflix: visualeffect(%#x, %d)", effect, duration);
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
	loop.kind = kind;
	loop.kind.toLowercase();
	loop.target = target;
	loop.message = message;
	const uint32 delayMs = delay <= 0 ? 0 :
			(delay < 1000 ? (uint32)((uint64)delay * 1000 / 60) : (uint32)delay);
	loop.dueMillis = _system->getMillis() + delayMs;
	_scheduledLoops.push_back(loop);
	debug(2, "Cyberflix: makeloop('%s', '%s', '%s', %d) due in %u ms",
			kind.c_str(), target.c_str(), message.c_str(), delay, delayMs);
}

void CyberflixEngine::stopLoop(const Common::String &kind, const Common::String &target) {
	if (kind.empty())
		return;
	const bool all = kind.equalsIgnoreCase("all");
	for (int i = (int)_scheduledLoops.size() - 1; i >= 0; --i) {
		const ScheduledLoop &loop = _scheduledLoops[(uint32)i];
		if (all || (loop.kind.equalsIgnoreCase(kind) &&
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
	if (_loopsPaused || _scheduledLoops.empty())
		return;

	const uint32 now = _system->getMillis();
	for (uint32 i = 0; i < _scheduledLoops.size();) {
		if ((int32)(now - _scheduledLoops[i].dueMillis) < 0) {
			++i;
			continue;
		}

		ScheduledLoop loop = _scheduledLoops[i];
		_scheduledLoops.remove_at(i);
		Common::Array<Value> noArgs;
		if (loop.kind.equalsIgnoreCase("scene")) {
			sendToScene(loop.target, loop.message, noArgs);
		} else if (loop.kind.equalsIgnoreCase("flat")) {
			sendToFlat(loop.target, loop.message, noArgs);
		} else if (loop.kind.equalsIgnoreCase("stage")) {
			sendToStage(loop.message, noArgs);
		} else if (loop.kind.equalsIgnoreCase("prop")) {
			sendToProp(loop.target, loop.message, noArgs);
		} else if (loop.kind.equalsIgnoreCase("shop")) {
			sendToShop(loop.target, loop.message, noArgs);
		} else {
			debug(1, "Cyberflix: makeloop kind '%s' unhandled", loop.kind.c_str());
		}
		refreshPropsIfDirty();
	}
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
	if (_set->findView((uint32)scene, view) < 0) {
		warning("Cyberflix: sendtopainting('%s'): no view '%s'",
				painting.c_str(), view.c_str());
		return;
	}

	Common::Array<Common::SharedPtr<Script> > keepAlive;
	keepAlive.push_back(_set->paintingScriptShared((uint32)scene, view, painting));
	keepAlive.push_back(_set->sceneScriptShared((uint32)scene));
	keepAlive.push_back(_set->setScriptShared());
	Common::Array<const Script *> scopes;
	scopes.push_back(keepAlive[0].get());
	scopes.push_back(keepAlive[1].get());
	scopes.push_back(keepAlive[2].get());
	dispatchWithScopeChain(scopes, painting, painting, message, args, "painting");
	refreshPropsIfDirty();
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
		FrameImage frame;
		if (!_set->applyPanoramaFrame((uint32)_setScene, (uint32)table, (uint32)startAngle,
				_setFrameSequence, frame)) {
			warning("Cyberflix: set '%s' failed to start %s turn from view '%s'",
					_set->name().c_str(), action.c_str(), _setView.c_str());
			return;
		}
		_setTable = table;
		_setAngle = startAngle;
		_setTransitionType = kSetTransitionTurn;
		displaySetFrame(frame);
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
		FrameImage frame;
		if (!_set->applyTransitionFrame(transitionId, 0, _setFrameSequence, frame)) {
			warning("Cyberflix: set '%s' failed to start forward transition %u",
					_set->name().c_str(), transitionId);
			return;
		}
		_setTransitionType = kSetTransitionForward;
		_setTransitionResource = transitionId;
		_setTransitionFrame = 0;
		displaySetFrame(frame);
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
		FrameImage frame;
		if (!_set->applyPanoramaFrame((uint32)_setScene, (uint32)_setTable, (uint32)nextAngle,
				_setFrameSequence, frame)) {
			_setTransitionType = kSetTransitionNone;
			warning("Cyberflix: failed to advance SET turn transition");
			return;
		}
		_setAngle = nextAngle;
		int viewIdx = _set->viewTagAtAngle((uint32)_setScene, (uint32)_setTable, (uint32)nextAngle);
		if (viewIdx >= 0)
			_setView = _set->viewName((uint32)_setScene, (uint32)viewIdx);
		displaySetFrame(frame);

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

		FrameImage frame;
		if (!_set->applyTransitionFrame(_setTransitionResource, nextFrame, _setFrameSequence, frame)) {
			_setTransitionType = kSetTransitionNone;
			warning("Cyberflix: failed to advance SET forward transition %u", _setTransitionResource);
			return;
		}
		_setTransitionFrame = nextFrame;
		displaySetFrame(frame);

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

	FrameImage frame;
	if (!_set->renderScene((uint32)scene, (uint32)table, (uint32)angle, _setFrameSequence, frame))
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
		displaySetFrame(frame);

	debug(1, "Cyberflix: rendered set '%s' scene %d '%s' angle %d (%ux%u)",
			_set->name().c_str(), scene, _set->sceneName((uint32)scene).c_str(),
			angle, frame.width, frame.height);
}

void CyberflixEngine::displaySetFrame(const FrameImage &frame) {
	if (!_setVisible || !_set || !_set->isOpen())
		return;

	Graphics::Surface *screen = _system->lockScreen();
	// Base layer: the stage's UI shell (MAIN.STG node 0 — art-deco frame +
	// inventory bar). The original's compositor keeps it on screen beneath
	// the room: the redraw pass FUN_00442d90 repaints full-screen stage items
	// (clipped to screen rect, FUN_004436d0) before world items, which clip
	// to the set viewport DAT_00486760. Fall back to black with no stage.
	FrameImage stageBg;
	if (_stage && _stage->isOpen() && _stage->renderNode(0, stageBg)) {
		for (int y = 0; y < stageBg.height && y < kScreenHeight; ++y)
			for (int x = 0; x < stageBg.width && x < kScreenWidth; ++x)
				*((byte *)screen->getBasePtr(x, y)) = stageBg.pixels[(uint)y * stageBg.width + x];
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
	for (int y = 0; y < frame.height; ++y) {
		for (int x = 0; x < frame.width; ++x) {
			int sx = x0 + x, sy = y0 + y;
			if (sx >= 0 && sy >= 0 && sx < kScreenWidth && sy < kScreenHeight)
				*((byte *)screen->getBasePtr(sx, sy)) = frame.pixels[(uint)y * frame.width + x];
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
			CelImage cel;
			Common::Rect r;
			if (!drawShop[i]->renderProp(*draw[i], cel, r))
				continue;
			for (int y = 0; y < cel.height; ++y) {
				for (int x = 0; x < cel.width; ++x) {
					int sx = r.left + x, sy = r.top + y;
					if (sx >= 0 && sy >= 0 && sx < kScreenWidth && sy < kScreenHeight &&
							cel.isOpaque(x, y))
						*((byte *)screen->getBasePtr(sx, sy)) = cel.pixels[(uint)y * cel.width + x];
				}
			}
		}
	}
	_propsDirty = false;
	_dirtyRects.clear();
	_system->unlockScreen();

	// Default arrow until per-view hotspot hit-testing (directional cursors) is
	// implemented. Views (the scene's hotspot lists) are documented in
	// files/decomp/stage-notes.md.
	if (setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
	_system->updateScreen();
}

Common::Error CyberflixEngine::run() {
	// The original is a 640x480 8-bit palettised WinG title.
	initGraphics(kScreenWidth, kScreenHeight);

	_console = new Console(this);
	setDebugger(_console);

	// Assets live in the DATA subdirectory of the installed game; the intro and
	// other full-screen movies live alongside it in MOVIES.
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, "data");
	SearchMan.addSubDirectoryMatching(gameDataDir, "movies");
	if (gameDataDir.getName().equalsIgnoreCase("titanic1") ||
			gameDataDir.getName().equalsIgnoreCase("titanic2"))
		_pathSlots[0] = gameDataDir.getName() + ":";

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
		processScheduledLoops();
		bool handled = false;
		_vm.callFunction("idle", Common::Array<Value>(), &handled);
		refreshPropsIfDirty();
		_system->updateScreen();
		if (_setTransitionType == kSetTransitionNone)
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

void CyberflixEngine::playMovie(const Common::String &name) {
	if (name.empty())
		return;

	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open movie '%s'", name.c_str());
		return;
	}

	uint32 size = (uint32)file.size();
	// fileData is declared before archive so it outlives it: the archive owns a
	// stream that points into this buffer, and the movie loop below reads
	// payloads through raw pointers into it.
	Common::Array<byte> fileData(size);
	if (file.read(fileData.begin(), size) != size) {
		warning("Cyberflix: could not read movie '%s'", name.c_str());
		return;
	}
	file.close();

	Archive archive;
	if (!archive.open(new Common::MemoryReadStream(fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid movie container", name.c_str());
		return;
	}

	// Full-screen frames are the resources whose info word's high half is 0x0200;
	// that word doubles as the frame's {uint16 H, uint16 W} header (W is always
	// 512, H is the low half: 264 for LOGO video, 384 for the PLAYMODE menu), so
	// the decoder source begins four bytes before the payload (see image.h). This
	// linear scan is only a fallback frame order; when the movie has a master
	// header we drive playback from its authoritative per-frame table below.
	Common::Array<uint32> frames;
	for (uint32 i = 0; i < archive.getResourceCount(); ++i) {
		const Archive::Resource &res = archive.getResource(i);
		if (!res.empty && (res.info >> 16) == kFrameInfoHigh && res.dataOffset >= 4)
			frames.push_back(i);
	}

	// Build the soundtrack. A linear movie's master header (info==0x40000)
	// references two cue tables: a MUSIC table (the continuous score, played from
	// t=0) and an SFX/event table (named one-shots triggered by individual video
	// frames). For linear movies with music, decode the MUSIC cues into one track
	// and mix frame-triggered SFX into it at their frame start. For interactive
	// movies or movies with no music track, keep those SFX separate and play them
	// live when their frame is reached. This preserves LOGO.MOV's sample-locked
	// gunshots while allowing BEDCARDS.MOV's silent interactive stopwatch frames
	// to fire their voice cues. See files/decomp/movie-playback.md.
	//
	// NB: do NOT concatenate the SFX resources onto the music track. Doing so
	// lengthened the track (so frames played too slowly) and made the effects
	// sound at their concatenation offset instead of their trigger frame.
	Common::Array<byte> pcmBuf;
	// Cumulative start time (ms) of each video frame; last entry is the total
	// duration. Built from the per-frame event chunks below. Empty => no usable
	// master header, in which case the frame loop falls back to a fixed cadence.
	Common::Array<uint32> frameStartMs;
	// Per-frame table, captured from the master header: the video resource id to
	// composite (event record +0xc) and the navigation command of its event
	// chunk (event chunk +0: 6 = NEXT, 1 = HOLD/wait). When populated this is the
	// authoritative playback order; the kFrameInfoHigh scan above is the fallback.
	Common::Array<uint32> pfVideoRes;
	Common::Array<uint16> pfNavCmd;
	// Per-frame name (event record +0x1a, Pascal) used to resolve GOTO targets,
	// and the per-frame interactive button table (empty => non-interactive frame
	// that obeys its nav command; non-empty => the player holds the frame and
	// waits for a click, as the main menu does).
	Common::Array<Common::String> pfName;
	Common::Array<Common::String> pfNavTarget;
	Common::Array<Common::Array<MovieButton> > pfButtons;
	// Decoded SFX named by each frame's event chunk. Linear movies with a music
	// buffer premix these at frame start; interactive or otherwise-silent movies
	// play them live when the frame is reached.
	Common::Array<Common::Array<byte> > pfFrameSfx;
	// Per-frame hold duration in ms (event chunk +2 in scaled timer units,
	// floored by masterHdr[+0x1c]). Used to pace interactive movies frame by
	// frame (the menu and its pressed-button frames), independent of the audio
	// timeline that paces linear movies.
	Common::Array<uint32> pfHoldMs;
	// Per-frame draw command (event chunk +0xc; TI.EXE FUN_0040eef0 switch).
	// 0x10 = plain blit; 0x11 = blit + palette fade to black across the hold;
	// 0x12 = blit (palette black) + palette fade in. These author the menu
	// fade-out (PLAYMODE 'GAME 2' 0x11) and the movie fade-ins (frame 0 0x12).
	Common::Array<uint16> pfDrawOp;

	// Whether the movie may be skipped by the user. The original input handler
	// (TI.EXE FUN_0040e430) only honours the '.'/'Q'/'q' skip keys when the
	// master header flags byte (+0x18) has bit 0 set.
	bool movieSkippable = false;
	// Hover cursor: while waiting on an interactive frame the original polls
	// FUN_0040e5b0 each event-loop pass, which swaps the cursor to "CURS131"
	// over any button whose flag bit 0x2 is set and back to "CURS.ARROW"
	// otherwise. Disabled wholesale by master header flag +0x18 bit 0x10.
	bool movieHoverCursor = true;

	// Action-cue frame indices, resolved from the master header's two cue-name
	// fields at +0x40/+0x50 (TI.EXE FUN_0040ca80 resolves them via FUN_0040e050
	// before the frame loop). Reaching cue N during playback sets bit N of the
	// action-frame mask that the script builtin actionframe(N) tests.
	int actionCue1 = -1, actionCue2 = -1;
	_actionFrameMask = 0; // playmovie clears the mask (TI.EXE FUN_00446f80)

	// Screen position of the movie. The original never centers: each draw op
	// blits at the master header's QuickDraw rect {top,left,bottom,right}
	// @+0x86c offset by the s16 origin @+0x24 (x) / +0x26 (y) (FUN_0040eef0 ->
	// FUN_00410660 -> FUN_0041ad40). LOGO.MOV's rect is {0,0,264,512}: the
	// logo plays at the TOP of the screen. See files/decomp/movie-playback.md.
	int movieX = 0, movieY = 0;

	int masterIdx = -1;
	for (uint32 i = 0; i < archive.getResourceCount(); ++i) {
		if (!archive.getResource(i).empty && archive.getResource(i).info == kMasterHeaderInfoTag) {
			masterIdx = (int)i;
			break;
		}
	}
	if (masterIdx < 0) {
		warning("Cyberflix: movie '%s' has no master header; playing without audio", name.c_str());
	} else {
		const byte *hdr = engineBase(fileData, archive.getResource(masterIdx));
		// Guard the per-frame table extent before trusting the header layout.
		if (hdr && hdr + 0x87c <= fileData.end()) {
			movieSkippable = (hdr[0x18] & 1) != 0;
			movieHoverCursor = (hdr[0x18] & 0x10) == 0;
			// Dest position: rect {t,l} @+0x86c plus origin @+0x24/+0x26.
			movieX = (int16)READ_LE_UINT16(hdr + 0x86e) + (int16)READ_LE_UINT16(hdr + 0x24);
			movieY = (int16)READ_LE_UINT16(hdr + 0x86c) + (int16)READ_LE_UINT16(hdr + 0x26);
			uint32 musicTableIdx = READ_LE_UINT32(hdr + 0x64); // masterHdr[0x19]
			uint32 sfxTableIdx   = READ_LE_UINT32(hdr + 0x60); // masterHdr[0x18]
			uint32 pfCount       = READ_LE_UINT32(hdr + 0x878);
			const byte *pfTable  = hdr + 0x87c; // per-frame records, stride 0x2a
			// Minimum per-frame hold, in the scaled timer's units (FUN_00405130
			// returns timeGetTime * 0.06, so 1 unit == 1000/60 ms). For LOGO this
			// floor is 3 units == 50 ms == 20 fps.
			uint32 frameFloorUnits = READ_LE_UINT32(hdr + 0x1c);
			if (frameFloorUnits == 0)
				frameFloorUnits = 3;

			// 1. MUSIC track: decode each music-table cue's 22050 Hz resource in
			//    order. (Skip non-22050 cues such as the silent 11025 Hz pad.)
			if (musicTableIdx < archive.getResourceCount()) {
				const byte *mt = engineBase(fileData, archive.getResource(musicTableIdx));
				if (mt && mt + 0x10e <= fileData.end()) {
					uint32 mc = READ_LE_UINT32(mt + 0x10a);
					for (uint32 e = 0; e < mc; ++e) {
						const byte *ent = mt + 0x10e + e * 0x1a;
						if (ent + 0x1a > fileData.end())
							break;
						uint32 rid = READ_LE_UINT32(ent + 4);
						if (rid >= archive.getResourceCount())
							continue;
						const Archive::Resource &r = archive.getResource(rid);
						if (r.empty || r.info != kAudioResourceInfoTag || r.dataOffset < 4)
							continue;
						const byte *payload = fileData.begin() + r.dataOffset;
						if (READ_LE_UINT32(payload + 0x18) != kAudioRate22050)
							continue;
						decodeCbxAudio(payload, r.length, pcmBuf);
					}
				}
			}

			// 2. Per-frame timeline + SFX. Walk the per-frame table: each frame's
			//    event chunk (info==0x6 resource at record[+0x10]) gives its hold
			//    duration at engine offset +2 (floored by frameFloorUnits) and an
			//    optional cue NAME at +0x12. We accumulate the real start time of
			//    every frame, and for each named cue we look it up in the SFX
			//    table and MIX that effect into the music track at the frame's
			//    time (sample-add). The two LOGO frames that hold 333/500 ms make
			//    the video timeline (~16.6 s) slightly longer than the music
			//    (~15.9 s); the trailing fade plays over silence, as in the
			//    original.
			const byte *st = (sfxTableIdx < archive.getResourceCount())
					? engineBase(fileData, archive.getResource(sfxTableIdx)) : nullptr;
			uint32 sfxCount = (st && st + 8 <= fileData.end()) ? READ_LE_UINT32(st + 4) : 0;
			uint32 cumMs = 0;
			for (uint32 f = 0; f < pfCount; ++f) {
				const byte *rec = pfTable + f * 0x2a;
				if (rec + 0x2a > fileData.end())
					break;
				const byte *eb = nullptr;
				uint32 eventId = READ_LE_UINT32(rec + 0x10);
				uint32 ebLen = 0;
				if (eventId < archive.getResourceCount()) {
					eb = engineBase(fileData, archive.getResource(eventId));
					ebLen = archive.getResource(eventId).length;
				}

				frameStartMs.push_back(cumMs);
				pfVideoRes.push_back(READ_LE_UINT32(rec + 0xc));
				pfNavCmd.push_back((eb && eb + 2 <= fileData.end())
						? READ_LE_UINT16(eb) : 6 /* default NEXT */);
				pfDrawOp.push_back((eb && eb + 0xe <= fileData.end())
						? READ_LE_UINT16(eb + 0xc) : 0x10 /* plain blit */);
				pfName.push_back(readPascalString(rec + 0x1a, fileData));
				// FUN_0040d710 command 2 resolves a Pascal target in the event
				// chunk. The decompile's +0x19 is word-indexed; in the raw
				// record+8 frame used here it is byte offset +0x32.
				pfNavTarget.push_back((eb && eb + 0x33 <= fileData.end())
						? readPascalString(eb + 0x32, fileData) : Common::String());

				// Interactive buttons: raw event chunks store a u32 count at
				// +0x442 and 0x40-byte button records at +0x446. Ghidra's
				// FUN_0040d710 decompile reports +0x221 because of a widened
				// pointer type; FUN_0040e5b0 and HELP*.MOV raw dumps verify the
				// byte offsets.
				Common::Array<MovieButton> buttons;
				uint32 btnCount = (eb && ebLen >= 0x446) ? READ_LE_UINT32(eb + 0x442) : 0;
				if (eb && ebLen > 0x446)
					btnCount = MIN<uint32>(btnCount, (ebLen - 0x446) / 0x40);
				for (uint32 b = 0; b < btnCount; ++b) {
					const byte *br = eb + 0x446 + b * 0x40;
					if (br + 0x40 > fileData.end())
						break;
					MovieButton mb;
					mb.action = READ_LE_UINT16(br);
					mb.flags  = br[2];
					// QuickDraw rect order {t, l, b, r} (see MovieButton).
					mb.top    = (int16)READ_LE_UINT16(br + 8);
					mb.left   = (int16)READ_LE_UINT16(br + 10);
					mb.bottom = (int16)READ_LE_UINT16(br + 12);
					mb.right  = (int16)READ_LE_UINT16(br + 14);
					mb.target = readPascalString(br + 0x30, fileData);
					buttons.push_back(mb);
				}
				pfButtons.push_back(buttons);

				Common::Array<byte> frameSfx;
				if (eb) {
					Common::String cue = readPascalString(eb + 0x12, fileData);
					if (!cue.empty()) {
						uint32 sfxResId = (uint32)-1;
						for (uint32 e = 0; e < sfxCount; ++e) {
							const byte *ent = st + 8 + e * 0x2a;
							if (ent + 0x2a > fileData.end())
								break;
							if (readPascalString(ent + 0xa, fileData) == cue) {
								sfxResId = READ_LE_UINT32(ent + 4);
								break;
							}
						}
						if (sfxResId < archive.getResourceCount()) {
							const Archive::Resource &sr = archive.getResource(sfxResId);
							if (!sr.empty && sr.info == kAudioResourceInfoTag && sr.dataOffset >= 4) {
								decodeCbxAudio(fileData.begin() + sr.dataOffset, sr.length, frameSfx);
							}
						}
					}
				}
				pfFrameSfx.push_back(frameSfx);

				// Advance the timeline by this frame's hold (scaled units -> ms).
				uint32 units = frameFloorUnits;
				if (eb && eb + 6 <= fileData.end()) {
					uint32 d = READ_LE_UINT32(eb + 2);
					if (d > units)
						units = d;
				}
				uint32 holdMs = (uint32)((uint64)units * 1000 / 60);
				pfHoldMs.push_back(holdMs);
				cumMs += holdMs;
			}
			frameStartMs.push_back(cumMs); // total movie duration

			// 3. Action-cue frames: resolve the master header's cue names
			//    against the per-frame name column (TI.EXE iVar12/iVar9 in
			//    FUN_0040ca80). Missing names resolve to -1 (never matched).
			actionCue1 = resolveFrameName(pfName, readPascalString(hdr + 0x40, fileData));
			actionCue2 = resolveFrameName(pfName, readPascalString(hdr + 0x50, fileData));
		}
	}

	// A movie is interactive if any frame carries buttons (the main menu,
	// BEDCARDS, BEDCAB, ...). Such movies loop their soundtrack while they wait
	// for the user; linear movies (the logo) play their track once.
	bool hasInteractive = false;
	for (uint i = 0; i < pfButtons.size(); ++i)
		if (!pfButtons[i].empty()) {
			hasInteractive = true;
			break;
		}
	const bool playFrameSfxLive = hasInteractive || pcmBuf.empty();
	uint32 frameSfxBytes = 0;
	for (uint i = 0; i < pfFrameSfx.size(); ++i)
		frameSfxBytes += pfFrameSfx[i].size();
	if (!playFrameSfxLive) {
		for (uint i = 0; i < pfFrameSfx.size(); ++i) {
			if (pfFrameSfx[i].empty())
				continue;
			uint32 atMs = (i < frameStartMs.size()) ? frameStartMs[i] : 0;
			uint32 atSample = (uint32)((uint64)atMs * kAudioSampleRate / 1000);
			mixSfx(pcmBuf, pfFrameSfx[i], atSample);
		}
	}

	byte *pcm = nullptr;
	uint32 pcmLen = pcmBuf.size();
	if (pcmLen) {
		pcm = (byte *)malloc(pcmLen);
		if (pcm)
			memcpy(pcm, pcmBuf.begin(), pcmLen);
		else
			pcmLen = 0;
	}

	// The movie palette is NOT programmed up front: the original keeps a
	// palette-dirty flag (DAT_0045ee90) and the per-frame draw command decides
	// — op 0x12 fades it in from black, op 0x11 fades out to black, any other
	// op snaps it on its first presented frame (FUN_0040eef0 preamble). This
	// keeps the menu's authored fade-out (clut left black) intact across the
	// movie boundary instead of flashing the palette on at movie start.
	byte moviePal[256 * 3];
	memset(moviePal, 0, sizeof(moviePal));
	bool haveMoviePal = loadPalette(fileData.begin(), size, moviePal);
	bool moviePalApplied = false;

	// Composite frames in order into a persistent surface (frames are
	// inter-coded) and present them on the movie's own timeline. Esc skips a
	// movie flagged skippable (header +0x18 bit 0); quit always stops.
	//
	// There is NO stored frames-per-second field. Each frame carries its own
	// hold time in its event chunk (offset +2, floored by masterHdr[+0x1c]),
	// expressed in the scaled-timer units returned by TI.EXE FUN_00405130
	// (timeGetTime * 0.06, i.e. 1 unit == 1000/60 ms). We precomputed the
	// cumulative start time of every frame into frameStartMs above, so the video
	// is paced by that timeline against a wall clock - NOT slaved to the audio.
	// The soundtrack (music with SFX mixed in at their frame times) is fired on
	// the mixer and runs concurrently; both advance in real time so they stay in
	// step. The video timeline can be slightly longer than the music (the two
	// long-hold LOGO frames push it to ~16.6 s vs ~15.9 s of music), so the final
	// fade is no longer cut short. With no usable timeline we fall back to a
	// fixed cadence.
	FrameSequence seq;
	const uint32 kFallbackFrameDelayMs = 66; // ~15 fps when there is no frame timeline
	bool skip = false;
	Common::Event event;

	// The original movie player shows the Windows arrow cursor while an
	// interactive frame (the menu) is up and hides it during linear playback
	// (FUN_004051b0/FUN_00405210, gated by movie flag bit 0x10). Mirror that:
	// the arrow is decoded on demand from the user's TI.EXE.
	if (hasInteractive && setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
	else
		CursorMan.showMouse(false);

	Audio::SoundHandle audioHandle;
	Common::Array<Audio::SoundHandle> frameSfxHandles;
	if (pcm && pcmLen) {
		Audio::SeekableAudioStream *stream = Audio::makeRawStream(
				pcm, pcmLen, kAudioSampleRate, Audio::FLAG_UNSIGNED,
				DisposeAfterUse::YES);
		if (hasInteractive) {
			Audio::AudioStream *loop = new Audio::LoopingAudioStream(stream, 0);
			_mixer->playStream(Audio::Mixer::kSFXSoundType, &audioHandle, loop);
		} else {
			_mixer->playStream(Audio::Mixer::kSFXSoundType, &audioHandle, stream);
		}
	} else {
		free(pcm);
		pcm = nullptr;
	}

	debug(0, "Cyberflix: movie '%s' frames=%u audioBytes=%u frameSfxBytes=%u audioMs=%u",
			name.c_str(), pfVideoRes.empty() ? frames.size() : pfVideoRes.size(), pcmLen, frameSfxBytes,
			pcm ? (uint32)((uint64)pcmLen * 1000 / kAudioSampleRate) : 0);

	// Composite frames in order into a persistent surface (frames are
	// inter-coded) and present them. Esc skips a skippable movie; quit stops.
	//
	// There is NO stored frames-per-second field. Each frame carries its own
	// hold time in its event chunk (offset +2, floored by masterHdr[+0x1c]),
	// expressed in the scaled-timer units returned by TI.EXE FUN_00405130
	// (timeGetTime * 0.06, i.e. 1 unit == 1000/60 ms). We precompute the
	// cumulative start time of every frame into frameStartMs above.
	//
	// SYNC: linear-movie SFX (e.g. LOGO.MOV's gunshots) are mixed into the
	// soundtrack at their exact frame time, so they are locked to the music
	// sample-for-sample. To keep the *picture* locked to those sounds even when
	// frame decoding/blit lags, we clock the video off the real audio position
	// (the mixer's elapsed time) rather than a free-running wall clock, and DROP
	// the present of any frame whose slot has already passed (still decoding it,
	// since frames are inter-coded). This mirrors the original's adaptive
	// frame-drop in FUN_0040e8b0. Once the music ends (the video timeline can run
	// ~0.75 s longer than the music, e.g. LOGO's trailing fade) we continue on the
	// wall clock so the fade still plays out.
	const bool usePF = !pfVideoRes.empty();
	const uint32 frameCount = usePF ? pfVideoRes.size() : frames.size();
	if (frameCount == 0)
		warning("Cyberflix: movie '%s' has no frames to show", name.c_str());

	uint32 wallStartMs = _system->getMillis();
	uint32 fi = 0;
	while (fi < frameCount && !shouldQuit() && !skip) {
		uint32 resIdx = usePF ? pfVideoRes[fi] : frames[fi];
		if (resIdx >= archive.getResourceCount())
			break;
		const Archive::Resource &res = archive.getResource(resIdx);
		if (res.empty || res.dataOffset < 4 ||
				seq.applyFrame(fileData.begin() + res.dataOffset - 4, res.length + 4) == 0) {
			warning("Cyberflix: movie '%s' frame %u failed to decode", name.c_str(), fi);
			break;
		}

		const bool interactive = usePF && fi < pfButtons.size() && !pfButtons[fi].empty();

		// Record action-cue hits for the actionframe() builtin. The original
		// ORs the bits after decoding every frame it iterates (FUN_0043b800
		// call sites 0x0040d19a/0x0040d1af), clicked-to frames included.
		if ((int)fi == actionCue1)
			_actionFrameMask |= 1;
		if ((int)fi == actionCue2)
			_actionFrameMask |= 2;
		if (playFrameSfxLive && fi < pfFrameSfx.size())
			playMovieFrameSfx(_mixer, frameSfxHandles, pfFrameSfx[fi]);

		// Current playback clock: real audio position while the track plays,
		// else elapsed wall time (covers the post-music fade and silent movies).
		uint32 nowMs = (pcm && _mixer->isSoundHandleActive(audioHandle))
				? _mixer->getSoundElapsedTime(audioHandle)
				: (_system->getMillis() - wallStartMs);
		uint32 frameEndMs = (fi + 1 < frameStartMs.size())
				? frameStartMs[fi + 1] : (fi + 1) * kFallbackFrameDelayMs;

		// Drop the present of a late linear frame to let the picture catch up to
		// the audio; always present in interactive movies. The original player
		// blits every frame (FUN_0040e8b0) before running the nav/button
		// interpreter (FUN_0040d710), so the pressed-button ("squished") frames
		// reached by a click are always shown. Frame-drop is a sync aid for the
		// long linear movies (the logo) only.
		bool present = hasInteractive || nowMs < frameEndMs || fi + 1 >= frameCount;

		const byte *pixels = seq.pixels();
		int w = seq.width(), h = seq.height();
		// Movie rect origin from the master header (no centering in the
		// original; FUN_00410660 + FUN_0041ad40): (0,0) for all known movies,
		// so 264-high frames play at the top of the screen.
		int x0 = movieX;
		int y0 = movieY;
		// This frame's draw command (FUN_0040eef0): 0x11/0x12 are the palette
		// fade-out/fade-in frames; anything else is a plain blit that snaps
		// the movie palette on if it is not up yet (the original's
		// palette-dirty preamble in FUN_0040eef0).
		uint16 drawOp = (usePF && fi < pfDrawOp.size()) ? pfDrawOp[fi] : 0x10;
		bool fadedThisFrame = false;
		if (present) {
			if (haveMoviePal && !moviePalApplied && drawOp != 0x11 && drawOp != 0x12) {
				programPalette(moviePal);
				moviePalApplied = true;
			}
			Graphics::Surface *screen = _system->lockScreen();
			for (int y = 0; y < h; ++y) {
				int sy = y0 + y;
				if (sy < 0 || sy >= kScreenHeight)
					continue;
				for (int x = 0; x < w; ++x) {
					int sx = x0 + x;
					if (sx >= 0 && sx < kScreenWidth)
						*((byte *)screen->getBasePtr(sx, sy)) = pixels[(uint)y * w + x];
				}
			}
			_system->unlockScreen();
			_system->updateScreen();

			// Palette fade across this frame's authored hold time, one step per
			// 60 Hz tick (FUN_00410120 / FUN_004101a0): 0x12 = reveal the frame
			// from black, 0x11 = fade the frame out, leaving the palette black
			// for whatever follows (the menu -> room -> movie chain relies on it).
			if (haveMoviePal && (drawOp == 0x11 || drawOp == 0x12)) {
				uint32 holdMs = (usePF && fi < pfHoldMs.size()) ? pfHoldMs[fi]
						: kFallbackFrameDelayMs;
				int steps = (int)(holdMs * 60 / 1000);
				byte black[256 * 3];
				memset(black, 0, sizeof(black));
				if (drawOp == 0x12) {
					fadePaletteSteps(black, moviePal, steps);
					moviePalApplied = true;
				} else {
					fadePaletteSteps(moviePal, black, steps);
					moviePalApplied = false;
				}
				fadedThisFrame = true;
			}
		}

		if (interactive) {
			// Interactive frame (the main menu): the original player suppresses
			// the frame's nav command and waits on the button table
			// (FUN_0040d710). Hold here, looping the soundtrack, until the user
			// clicks a button or quits. A click inside a button rect runs its
			// action: GOTO jumps to the named frame, NEXT/PREV step, END (and any
			// click on an action-1 button) returns from the movie.
			int32 nextFi = -1;
			// Hover cursor state: -1 unknown, 0 arrow, 1 hand ("CURS131").
			int hoverState = -1;
			while (nextFi < 0 && !shouldQuit() && !skip) {
				// FUN_0040e5b0: every poll, point-in-rect the mouse against the
				// frame's hover-eligible buttons (flag bit 0x2; plain rect test,
				// no pixel mask) and show "CURS131" over one, "CURS.ARROW"
				// otherwise.
				if (movieHoverCursor) {
					Common::Point m = _eventMan->getMousePos();
					int hover = 0;
					for (uint b = 0; b < pfButtons[fi].size(); ++b) {
						const MovieButton &mb = pfButtons[fi][b];
						if ((mb.flags & 0x2) && mb.contains(m.x - x0, m.y - y0)) {
							hover = 1;
							break;
						}
					}
					if (hover != hoverState) {
						setGameCursor(hover ? "CURS131" : "CURS.ARROW");
						hoverState = hover;
					}
				}
				while (_eventMan->pollEvent(event)) {
					handleMovieHotkeys(event, movieSkippable, audioHandle, skip);
					if (event.type == Common::EVENT_LBUTTONDOWN) {
						// TI.EXE FUN_0040e230 waits for event code 1
						// (WM_LBUTTONDOWN), then reads the current mouse point
						// from the movie port instead of using the queued event
						// payload. Keep click hit-testing consistent with the
						// hover helper above, which also polls the current point.
						Common::Point m = _eventMan->getMousePos();
						int fx = m.x - x0;
						int fy = m.y - y0;
						for (uint b = 0; b < pfButtons[fi].size(); ++b) {
							const MovieButton &mb = pfButtons[fi][b];
							if (!mb.contains(fx, fy))
								continue;
							if (mb.action == 2) { // GOTO target frame
								int idx = resolveFrameName(pfName, mb.target);
								nextFi = (idx >= 0) ? idx : (int32)fi;
							} else if (mb.action == 6) { // NEXT
								nextFi = (fi + 1 < frameCount) ? (int32)(fi + 1) : (int32)fi;
							} else if (mb.action == 7) { // PREV
								nextFi = (fi > 0) ? (int32)(fi - 1) : 0;
							} else { // END / unsupported -> leave the movie
								skip = true;
							}
							debug(1, "Cyberflix: movie '%s' button frame %u '%s' click (%d,%d) action %u target '%s' -> %d",
									name.c_str(), fi,
									(fi < pfName.size()) ? pfName[fi].c_str() : "",
									fx, fy, mb.action, mb.target.c_str(), (int)nextFi);
							break;
						}
					}
				}
				if (nextFi < 0 && !skip) {
					// Composite the cursor at its new position and keep the
					// window live. ScummVM draws the mouse during updateScreen,
					// so without this the cursor would appear frozen.
					_console->onFrame();
					_system->updateScreen();
					_system->delayMillis(10);
				}
			}
			if (nextFi >= 0)
				fi = (uint32)nextFi;
			continue;
		}

		uint16 nav = usePF ? pfNavCmd[fi] : 6;

		// Interactive movies (the menu and its pressed-button frames) are paced
		// frame by frame off a local wall clock by each frame's own authored
		// hold, NOT the global audio timeline (a click jumps around the frame
		// table, so cumulative audio time is meaningless here). This is what
		// makes the "squished" pressed-button frame visible for its hold before
		// the menu returns.
		if (hasInteractive) {
			// A 0x11/0x12 fade already spent this frame's hold on the palette
			// ramp (the original spreads the fade across the frame duration).
			uint32 holdMs = fadedThisFrame ? 0
					: ((fi < pfHoldMs.size()) ? pfHoldMs[fi] : kFallbackFrameDelayMs);
			uint32 holdStart = _system->getMillis();
			while (!shouldQuit() && !skip) {
				while (_eventMan->pollEvent(event))
					holdStart += handleMovieHotkeys(event, movieSkippable, audioHandle, skip);
				if (_system->getMillis() - holdStart >= holdMs)
					break;
				_system->updateScreen();
				_system->delayMillis(5);
			}
			if (nav == 1)
				break; // END: leave this (pressed) frame on screen and return
			if (nav == 2 && fi < pfNavTarget.size()) {
				int idx = resolveFrameName(pfName, pfNavTarget[fi]);
				fi = (idx >= 0) ? (uint32)idx : fi + 1;
			} else if (nav == 7) {
				fi = fi > 0 ? fi - 1 : 0;
			} else {
				++fi;
			}
			continue;
		}

		if (nav == 1) {
			// END: nav cmd 1 is the last-frame marker. The original player
			// (FUN_0040ca80: when FUN_0040d710 returns 1 -> goto cleanup) shows
			// this frame and then RETURNS from playback, leaving it on screen.
			break;
		}

		// Wait until this frame's authored end time on the playback clock, then
		// run the same nav command interpreter path as FUN_0040d710.
		for (;;) {
			while (_eventMan->pollEvent(event))
				wallStartMs += handleMovieHotkeys(event, movieSkippable, audioHandle, skip);
			if (shouldQuit() || skip)
				break;
			uint32 t = (pcm && _mixer->isSoundHandleActive(audioHandle))
					? _mixer->getSoundElapsedTime(audioHandle)
					: (_system->getMillis() - wallStartMs);
			if (t >= frameEndMs)
				break;
			_system->delayMillis(5);
		}
		if (nav == 2 && fi < pfNavTarget.size()) {
			int idx = resolveFrameName(pfName, pfNavTarget[fi]);
			fi = (idx >= 0) ? (uint32)idx : fi + 1;
		} else if (nav == 7) {
			fi = fi > 0 ? fi - 1 : 0;
		} else {
			++fi;
		}
	}

	_mixer->stopHandle(audioHandle);
	for (uint i = 0; i < frameSfxHandles.size(); ++i)
		_mixer->stopHandle(frameSfxHandles[i]);
	_eventMan->purgeKeyboardEvents();

	// Leave the cursor hidden when we hand control back; the next interactive
	// movie/node re-shows it.
	CursorMan.showMouse(false);
}

} // End of namespace Cyberflix
