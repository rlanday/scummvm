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

#include "audio/mixer.h"

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
#include "cyberflix/audio_helpers.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/archive.h"
#include "cyberflix/console.h"
#include "cyberflix/image.h"
#include "cyberflix/runtime/graphics_helpers.h"
#include "cyberflix/runtime/paths.h"
#include "cyberflix/runtime/set_helpers.h"
#include "cyberflix/script.h"
#include "cyberflix/set.h"
#include "cyberflix/cbx_audio.h"
#include "cyberflix/stage.h"
#include "cyberflix/vm.h"

#include <math.h>

namespace Cyberflix {

CyberflixEngine::CyberflixEngine(OSystem *syst, const CyberflixGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _rnd("cyberflix"), _console(nullptr) {
}

CyberflixEngine::~CyberflixEngine() {
	// _console is owned by the debugger registered with the engine framework.
	// _exe, _cursorCache (SharedPtr values) and _stage free themselves.
}

Common::String CyberflixEngine::currentPuppet() {
	return _puppetRuntime.currentPuppet();
}

void CyberflixEngine::openPuppetFile(const Common::String &name) {
	_puppetRuntime.openPuppetFile(name);
}

void CyberflixEngine::closePuppetFile() {
	_puppetRuntime.closePuppetFile(*this);
}

void CyberflixEngine::sendToPuppet(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	_puppetRuntime.sendToPuppet(*this, puppetName, message, args);
}

Value CyberflixEngine::sendToPuppetFx(const Common::String &puppetName,
		const Common::String &message, const Common::Array<Value> &args) {
	return _puppetRuntime.sendToPuppetFx(*this, puppetName, message, args);
}

void CyberflixEngine::puppetScript(const Common::String &name) {
	_puppetRuntime.puppetScript(name);
}

void CyberflixEngine::puppetClear() {
	_puppetRuntime.puppetClear(*this);
}

void CyberflixEngine::puppetSpeak(const Common::String &name, int mode) {
	_puppetRuntime.puppetSpeak(*this, name, mode);
}

void CyberflixEngine::puppetBevel(const Common::String &name, int mode) {
	_puppetRuntime.puppetBevel(*this, name, mode);
}

void CyberflixEngine::puppetGrab(bool enabled) {
	_puppetRuntime.puppetGrab(enabled);
}

int CyberflixEngine::puppetEvent(int timeout) {
	return _puppetRuntime.puppetEvent(*this, timeout);
}

Common::String CyberflixEngine::puppetBase(const Common::String *newBase) {
	return _puppetRuntime.puppetBase(newBase);
}

bool CyberflixEngine::puppetVisible(const bool *newVisible) {
	return _puppetRuntime.puppetVisible(*this, newVisible);
}

bool CyberflixEngine::renderCurrentPuppetFrame(bool present) {
	return _puppetRuntime.renderCurrentFrame(*this, present);
}

const Graphics::Font *CyberflixEngine::textFont(int size) {
	return _puppetRuntime.textFont(size);
}

int CyberflixEngine::puppetParam(int selector, const int *newValue) {
	return _puppetRuntime.puppetParam(selector, newValue);
}

int CyberflixEngine::countPuppets() {
	return _puppetRuntime.countPuppets();
}

Common::String CyberflixEngine::indexToPuppet(int index) {
	return _puppetRuntime.indexToPuppet(index);
}

void CyberflixEngine::refreshActorStarPositions() {
	_actorRuntime.refreshActorStarPositions(*this);
}

void CyberflixEngine::openCastFile(const Common::String &name) {
	_actorRuntime.openCastFile(*this, name);
}

void CyberflixEngine::closeCastFile(const Common::String &name) {
	_actorRuntime.closeCastFile(*this, name);
}

void CyberflixEngine::sendToCast(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	_actorRuntime.sendToCast(*this, castName, message, args);
}

Value CyberflixEngine::sendToCastFx(const Common::String &castName, const Common::String &message,
		const Common::Array<Value> &args) {
	return _actorRuntime.sendToCastFx(*this, castName, message, args);
}

void CyberflixEngine::sendToActor(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	_actorRuntime.sendToActor(*this, actorName, message, args);
}

Value CyberflixEngine::sendToActorFx(const Common::String &actorName, const Common::String &message,
		const Common::Array<Value> &args) {
	return _actorRuntime.sendToActorFx(*this, actorName, message, args);
}

int CyberflixEngine::countActors() {
	return _actorRuntime.countActors();
}

Common::String CyberflixEngine::indexToActor(int index) {
	return _actorRuntime.indexToActor(index);
}

bool CyberflixEngine::actorVisible(const Common::String &name, const bool *newVisible) {
	return _actorRuntime.actorVisible(*this, name, newVisible);
}

Common::String CyberflixEngine::actorSet(const Common::String &name, const Common::String *newSet) {
	return _actorRuntime.actorSet(*this, name, newSet);
}

Common::String CyberflixEngine::actorStar(const Common::String &name, const Common::String *newStar) {
	return _actorRuntime.actorStar(*this, name, newStar);
}

Common::String CyberflixEngine::actorPose(const Common::String &name, const Common::String *newPose) {
	return _actorRuntime.actorPose(*this, name, newPose);
}

void CyberflixEngine::actorXYZ(const Common::String &name, int x, int y, int z) {
	_actorRuntime.actorXYZ(*this, name, x, y, z);
}

int CyberflixEngine::actorXYZ(const Common::String &name, int selector) {
	return _actorRuntime.actorXYZ(*this, name, selector);
}

int CyberflixEngine::actorDeg(const Common::String &name, const int *newDeg) {
	return _actorRuntime.actorDeg(*this, name, newDeg);
}

int CyberflixEngine::actorValue(const Common::String &name, const int *newValue) {
	return _actorRuntime.actorValue(name, newValue);
}

Common::String CyberflixEngine::actorOwner(const Common::String &name,
		const Common::String *newOwner) {
	return _actorRuntime.actorOwner(name, newOwner);
}

void CyberflixEngine::actorZClip(const Common::String &name, int zClip) {
	_actorRuntime.actorZClip(*this, name, zClip);
}

void CyberflixEngine::actorSpeed(const Common::String &name, int speed) {
	_actorRuntime.actorSpeed(name, speed);
}

void CyberflixEngine::actorScale(const Common::String &name, int scale) {
	_actorRuntime.actorScale(*this, name, scale);
}

void CyberflixEngine::actorTurn(const Common::String &name, int turn) {
	_actorRuntime.actorTurn(name, turn);
}

int CyberflixEngine::starXYZ(const Common::String &name, int selector) {
	return _actorRuntime.starXYZ(*this, name, selector);
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
// ---- Cast/actor subsystem (TI.EXE FUN_0041f1c0 and friends) ---------------
// RE notes: files/decomp/stage-notes.md. The original keeps one global actor
// array (DAT_0046112c/DAT_00461130) across all open casts; lookups and
// countactors/indextoactor therefore span open casts in open order.

void CyberflixEngine::collectWorldActors(Common::Array<const Cast::Actor *> &draw,
		Common::Array<const Cast *> &drawCast, Common::Array<int16> &depths,
		const Shop::WorldCamera &camera) {
	_actorRuntime.collectWorldActors(*this, draw, drawCast, depths, camera);
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
	return _framePacingRuntime.frameRate(newRate);
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

void CyberflixEngine::makeLoop(const Common::String &kind, const Common::String &target,
		const Common::String &message, int delay) {
	_loopRuntime.makeLoop(kind, target, message, delay);
}

void CyberflixEngine::stopLoop(const Common::String &kind, const Common::String &target) {
	_loopRuntime.stopLoop(kind, target);
}

void CyberflixEngine::pauseLoop(const Common::String &kind, bool paused) {
	_loopRuntime.pauseLoop(kind, paused);
}

void CyberflixEngine::makeCricket(const Common::String &name) {
	_loopRuntime.makeCricket(*this, name);
}

void CyberflixEngine::stopCricket(const Common::String &name) {
	_loopRuntime.stopCricket(name);
}

void CyberflixEngine::pauseCricket(const Common::String &kind, bool paused) {
	_loopRuntime.pauseCricket(kind, paused);
}

void CyberflixEngine::processScheduledLoops() {
	_loopRuntime.processScheduledLoops(*this);
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
	if (_puppetRuntime.isVisible()) {
		renderCurrentPuppetFrame(true);
		_screenUpdatePending = false;
		_framePacingRuntime.noteForceUpdatePresented(true);
		presented = true;
	} else {
		advanceSetTransition();
		// Native forceupdate() presents after its compositor pass, but in
		// ScummVM advanceSetTransition() may be a no-op when a scene script calls
		// forceupdate() while no movement/dirty repaint is pending. Only upload
		// to the OpenGL backend if displaySetFramePixels() actually wrote new
		// pixels; this avoids redundant texture updates without changing the
		// script-visible timing of forceupdate(). If no pixels changed, leave
		// the idle-presented flag false so the main loop still does its cheap
		// cursor-only updateScreen() and mouse movement stays responsive.
		presented = presentPendingScreenUpdate();
		_framePacingRuntime.noteForceUpdatePresented(presented);
	}
	if (cursorMoved && !presented) {
		// Native uses the Win32 cursor, which moves independently while script
		// wait loops call forceupdate(). ScummVM's cursor is software-drawn, so
		// after pumping mouse-motion events above we need a cursor-only present
		// if the compositor did not upload any pixels this pass.
		_system->updateScreen();
		_framePacingRuntime.noteForceUpdatePresented(true);
	}
	while (!shouldQuit()) {
		const uint32 delay = _framePacingRuntime.delayMillisUntilDeadline(tick());
		if (delay == 0)
			break;
		_system->delayMillis(delay);
	}
	_framePacingRuntime.noteFrameTick(tick());
	debug(2, "Cyberflix: forceupdate()");
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
			_pathRuntime.setCurrentDiscRootName(cd1Root.getName());
		}
	} else {
		SearchMan.addSubDirectoryMatching(gameDataDir, "data");
		SearchMan.addSubDirectoryMatching(gameDataDir, "movies");
		if (gameDataDir.getName().equalsIgnoreCase("titanic1") ||
				gameDataDir.getName().equalsIgnoreCase("titanic2")) {
			_pathRuntime.setCurrentDiscRootName(gameDataDir.getName());
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
		Common::ScopedPtr<Common::SeekableReadStream> scriptStream(boot.createReadStreamForResource(i));
		Common::ScopedPtr<Script> script(new Script());
		bool parsed = scriptStream && script->parse(scriptStream.get());
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
		_framePacingRuntime.beginIdle();
		_vm.callFunction("idle", Common::Array<Value>(), &handled);
		const bool propsDirtyAfterIdle = _propsDirty;
		refreshPropsIfDirty();
		if (!_framePacingRuntime.forceUpdatePresentedDuringIdle() || propsDirtyAfterIdle)
			_system->updateScreen();
		if (_framePacingRuntime.frameRate() == 0 && _setTransitionType == kSetTransitionNone)
			_system->delayMillis(10);
	}

	return Common::kNoError;
}

} // End of namespace Cyberflix
