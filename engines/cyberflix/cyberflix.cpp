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
#include "cyberflix/audio/cbx_audio.h"
#include "cyberflix/stage.h"
#include "cyberflix/vm.h"

#include <math.h>

namespace Cyberflix {

CyberflixEngine::CyberflixEngine(OSystem *syst, const CyberflixGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _rnd("cyberflix"), _console(nullptr) {
}

CyberflixEngine::~CyberflixEngine() {
	// _console is owned by the debugger registered with the engine framework.
	// SharedPtr values free themselves.
}

void CyberflixEngine::playMovie(const Common::String &name) {
	_movieRuntime.playMovie(*this, name);
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

Shop *CyberflixEngine::findShop(const Common::String &name) {
	return _propRuntime.findShop(name);
}

Common::SharedPtr<Shop> CyberflixEngine::findShopShared(const Common::String &name) {
	return _propRuntime.findShopShared(name);
}

Shop::Prop *CyberflixEngine::findProp(const Common::String &name, Shop **shopOut) {
	return _propRuntime.findProp(name, shopOut);
}

Common::SharedPtr<Shop> CyberflixEngine::findPropOwnerShared(const Common::String &name,
		Shop::Prop **propOut) {
	return _propRuntime.findPropOwnerShared(name, propOut);
}

void CyberflixEngine::collectScreenProps(Common::Array<const Shop::Prop *> &draw,
		Common::Array<const Shop *> &drawShop) {
	_propRuntime.collectScreenProps(draw, drawShop);
}

void CyberflixEngine::advancePropPoses() {
	_propRuntime.advancePropPoses();
}

bool CyberflixEngine::hasAnimatedScreenProps() const {
	return _propRuntime.hasAnimatedScreenProps();
}

void CyberflixEngine::collectWorldProps(Common::Array<const Shop::Prop *> &draw,
		Common::Array<const Shop *> &drawShop, Common::Array<int16> &depths,
		const Shop::WorldCamera &camera) {
	_propRuntime.collectWorldProps(*this, draw, drawShop, depths, camera);
}

bool CyberflixEngine::screenPropRect(const Shop &shop, const Shop::Prop &prop, Common::Rect &rect) const {
	return _propRuntime.screenPropRect(shop, prop, rect);
}

void CyberflixEngine::queueDirtyRect(const Common::Rect &rect) {
	_propRuntime.queueDirtyRect(rect);
}

void CyberflixEngine::markPropDirty(const Shop &shop, const Shop::Prop &prop, const Common::Rect *oldRect) {
	_propRuntime.markPropDirty(shop, prop, oldRect);
}

void CyberflixEngine::markShopDirty(const Shop &shop) {
	_propRuntime.markShopDirty(shop);
}

void CyberflixEngine::openShopFile(const Common::String &name) {
	_propRuntime.openShopFile(*this, name);
}

void CyberflixEngine::closeShopFile(const Common::String &name) {
	_propRuntime.closeShopFile(*this, name);
}

void CyberflixEngine::propInstance(const Common::String &source, const Common::String &newName) {
	_propRuntime.propInstance(source, newName);
}

void CyberflixEngine::sendToShop(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	_propRuntime.sendToShop(*this, shopName, message, args);
}

Value CyberflixEngine::sendToShopFx(const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	return _propRuntime.sendToShopFx(*this, shopName, message, args);
}

void CyberflixEngine::sendToProp(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	_propRuntime.sendToProp(*this, propName, message, args);
}

Value CyberflixEngine::sendToPropFx(const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	return _propRuntime.sendToPropFx(*this, propName, message, args);
}

bool CyberflixEngine::propVisible(const Common::String &name) {
	return _propRuntime.propVisible(name);
}

void CyberflixEngine::propVisible(const Common::String &name, bool visible) {
	_propRuntime.propVisible(name, visible);
}

Common::String CyberflixEngine::propView(const Common::String &name) {
	return _propRuntime.propView(name);
}

void CyberflixEngine::propView(const Common::String &name, const Common::String &shape) {
	_propRuntime.propView(name, shape);
}

int CyberflixEngine::propXY(const Common::String &name, int selector) {
	return _propRuntime.propXY(name, selector);
}

void CyberflixEngine::setPropXY(const Common::String &name, int x, int y) {
	_propRuntime.setPropXY(name, x, y);
}

void CyberflixEngine::propSet(const Common::String &name, const Common::String &setName) {
	_propRuntime.propSet(name, setName);
}

void CyberflixEngine::propXYZ(const Common::String &name, int x, int y, int z) {
	_propRuntime.propXYZ(name, x, y, z);
}

void CyberflixEngine::propScale(const Common::String &name, int scale) {
	_propRuntime.propScale(name, scale);
}

void CyberflixEngine::propZClip(const Common::String &name, int dist) {
	_propRuntime.propZClip(name, dist);
}

void CyberflixEngine::propDist(const Common::String &name, int dist) {
	_propRuntime.propDist(name, dist);
}

int CyberflixEngine::propDeg(const Common::String &name, const int *newDeg) {
	return _propRuntime.propDeg(name, newDeg);
}

Common::String CyberflixEngine::propOwner(const Common::String &name, const Common::String *newOwner) {
	return _propRuntime.propOwner(name, newOwner);
}

int CyberflixEngine::propValue(const Common::String &name, const int *newValue) {
	return _propRuntime.propValue(name, newValue);
}

int CyberflixEngine::countProps() {
	return _propRuntime.countProps();
}

Common::String CyberflixEngine::indexToProp(int index) {
	return _propRuntime.indexToProp(index);
}

void CyberflixEngine::refreshPropsIfDirty() {
	_propRuntime.refreshPropsIfDirty(*this);
}

void CyberflixEngine::openTrackFile(const Common::String &name) {
	_audioRuntime.openTrackFile(name);
}

void CyberflixEngine::closeTrackFile(const Common::String &name) {
	_audioRuntime.closeTrackFile(name);
}

void CyberflixEngine::playTheme(const Common::String &name) {
	_audioRuntime.playTheme(*this, name);
}

void CyberflixEngine::haltTheme() {
	_audioRuntime.haltTheme(*this);
}

void CyberflixEngine::playSound(const Common::String &name, int mode) {
	_audioRuntime.playSound(*this, name, mode);
}

void CyberflixEngine::playVoice(const Common::String &name) {
	_audioRuntime.playVoice(*this, name);
}

void CyberflixEngine::haltSound(int which) {
	_audioRuntime.haltSound(*this, which);
}

void CyberflixEngine::haltVoice() {
	_audioRuntime.haltVoice(*this);
}

void CyberflixEngine::themeVolume(const Common::String &name, int volume) {
	_audioRuntime.themeVolume(*this, name, volume);
}

int CyberflixEngine::waveVolume(const int *newLevel) {
	return _audioRuntime.waveVolume(*this, newLevel);
}

int CyberflixEngine::soundVolume(const Common::String &name, const int *newVolume) {
	return _audioRuntime.soundVolume(*this, name, newVolume);
}

Common::String CyberflixEngine::currentTheme(int which) {
	return _audioRuntime.currentTheme(*this, which);
}

Common::String CyberflixEngine::currentSound(int which) {
	return _audioRuntime.currentSound(*this, which);
}

Common::String CyberflixEngine::currentVoice() {
	return _audioRuntime.currentVoice(*this);
}

void CyberflixEngine::openSetFile(const Common::String &name,
		const Common::String &scene, const Common::String &view) {
	_setRuntime.openSetFile(*this, name, scene, view);
}

void CyberflixEngine::closeSetFile() {
	_setRuntime.closeSetFile(*this);
}

Common::String CyberflixEngine::currentSet() {
	return _setRuntime.currentSet();
}

Common::String CyberflixEngine::currentView() {
	return _setRuntime.currentView();
}

Common::String CyberflixEngine::currentScene(const Common::String *target) {
	return _setRuntime.currentScene(*this, target);
}

bool CyberflixEngine::setVisible(const bool *newVisible) {
	return _setRuntime.setVisible(*this, newVisible);
}

void CyberflixEngine::sendToScene(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	_setRuntime.sendToScene(*this, scene, message, args);
}

Value CyberflixEngine::sendToSceneFx(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	return _setRuntime.sendToSceneFx(*this, scene, message, args);
}

void CyberflixEngine::sendToPainting(const Common::String &scene,
		const Common::String &view, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	_setRuntime.sendToPainting(*this, scene, view, painting, message, args);
}

Value CyberflixEngine::sendToPaintingFx(const Common::String &scene,
		const Common::String &view, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	return _setRuntime.sendToPaintingFx(*this, scene, view, painting, message, args);
}

int CyberflixEngine::countPaintings(const Common::String &scene, const Common::String &view) {
	return _setRuntime.countPaintings(scene, view);
}

Common::String CyberflixEngine::indexToPainting(const Common::String &scene,
		const Common::String &view, int index) {
	return _setRuntime.indexToPainting(scene, view, index);
}

bool CyberflixEngine::roadAhead(const Common::String &scene, const Common::String &view) {
	return _setRuntime.roadAhead(scene, view);
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

void CyberflixEngine::openStageFile(const Common::String &name) {
	_stageRuntime.openStageFile(*this, name);
}

void CyberflixEngine::closeStageFile() {
	_stageRuntime.closeStageFile(*this);
}

void CyberflixEngine::gotoFlat(const Value &flat) {
	_stageRuntime.gotoFlat(*this, flat);
}

Common::String CyberflixEngine::currentStage() {
	return _stageRuntime.currentStage();
}

bool CyberflixEngine::stageVisible(const bool *newVisible) {
	return _stageRuntime.stageVisible(newVisible);
}

Common::String CyberflixEngine::currentFlat() {
	return _stageRuntime.currentFlat();
}

void CyberflixEngine::sendToStage(const Common::String &message, const Common::Array<Value> &args) {
	_stageRuntime.sendToStage(*this, message, args);
}

Value CyberflixEngine::sendToStageFx(const Common::String &message, const Common::Array<Value> &args) {
	return _stageRuntime.sendToStageFx(*this, message, args);
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

void CyberflixEngine::sendToFlat(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	_stageRuntime.sendToFlat(*this, flat, message, args);
}

Value CyberflixEngine::sendToFlatFx(const Common::String &flat, const Common::String &message,
		const Common::Array<Value> &args) {
	return _stageRuntime.sendToFlatFx(*this, flat, message, args);
}

void CyberflixEngine::sendToButton(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	_stageRuntime.sendToButton(*this, flat, button, message, args);
}

Value CyberflixEngine::sendToButtonFx(const Common::String &flat, const Common::String &button,
		const Common::String &message, const Common::Array<Value> &args) {
	return _stageRuntime.sendToButtonFx(*this, flat, button, message, args);
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

	if (_setRuntime.visible() && _setRuntime.set() && _setRuntime.set()->isOpen() && _setRuntime.scene() >= 0 &&
			_setRuntime.transitionType() == kSetTransitionNone) {
		Set::CameraData cameraData;
		if (_setRuntime.set()->cameraData((uint32)_setRuntime.scene(), (uint32)_setRuntime.table(),
				(uint32)_setRuntime.angle(), cameraData)) {
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
							_setRuntime.set()->setName(), actorCel, r, depth))
						continue;
					cel = &actorCel;
					name = actorDraw[idx]->name;
				} else {
					uint32 idx = itemIndex[(uint)i];
					if (!worldShop[idx]->renderWorldProp(*worldDraw[idx], camera,
							_setRuntime.set()->setName(), propCel, r, depth))
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

	if (_stageRuntime.visible() && isReplacementStage(_stageRuntime.stage())) {
		Common::String button = _stageRuntime.stage()->hitTestButton((uint32)_stageRuntime.node(), x, y);
		if (!button.empty()) {
			_hitKind = "button";
			return button;
		}
		_hitKind = "flat";
		return _stageRuntime.stage()->nodeName((uint32)_stageRuntime.node());
	}

	if (_setRuntime.visible() && _setRuntime.set() && _setRuntime.set()->isOpen() && _setRuntime.scene() >= 0) {
		const int16 vl = _setRuntime.set()->viewLeft(), vt = _setRuntime.set()->viewTop();
		if (x >= vl && x < vl + (int)_setRuntime.set()->width() && y >= vt && y < vt + (int)_setRuntime.set()->height()) {
			if (_setRuntime.transitionType() == kSetTransitionNone) {
				Common::String painting = _setRuntime.set()->hitTestPainting((uint32)_setRuntime.scene(), _setRuntime.view(), x, y);
				if (!painting.empty()) {
					_hitKind = "painting";
					return painting;
				}
			}
			_hitKind = "scene";
			return _setRuntime.set()->sceneName((uint32)_setRuntime.scene());
		}
	}

	if (_stageRuntime.visible() && _stageRuntime.stage() && _stageRuntime.stage()->isOpen()) {
		Common::String button = _stageRuntime.stage()->hitTestButton((uint32)_stageRuntime.node(), x, y);
		if (!button.empty()) {
			_hitKind = "button";
			return button;
		}
		_hitKind = "flat";
		return _stageRuntime.stage()->nodeName((uint32)_stageRuntime.node());
	}

	_hitKind = "None";
	return Common::String();
}

bool CyberflixEngine::pointInButton(const Common::String &flat,
		const Common::String &button, int32 packedPoint) {
	return _stageRuntime.pointInButton(flat, button, packedPoint);
}

bool CyberflixEngine::pointInPainting(const Common::String &scene,
		const Common::String &view, const Common::String &painting, int32 packedPoint) {
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen())
		return false;
	int sceneIdx = _setRuntime.set()->findScene(scene);
	if (sceneIdx < 0)
		return false;
	const int16 x = (int16)(packedPoint >> 16);
	const int16 y = (int16)(packedPoint & 0xffff);
	bool hit = _setRuntime.set()->pointInPainting((uint32)sceneIdx, view, painting, x, y);
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
	if (!_propRuntime.dirty() && isReplacementStage(_stageRuntime.stage()) && hasAnimatedScreenProps())
		_propRuntime.setDirty(true);
	refreshPropsIfDirty();
	if (_puppetRuntime.isVisible()) {
		renderCurrentPuppetFrame(true);
		_setRuntime.screenUpdatePending() = false;
		_framePacingRuntime.noteForceUpdatePresented(true);
		presented = true;
	} else {
		setRuntime().advanceSetTransition(*this);
		// Native forceupdate() presents after its compositor pass, but in
		// ScummVM advanceSetTransition() may be a no-op when a scene script calls
		// forceupdate() while no movement/dirty repaint is pending. Only upload
		// to the OpenGL backend if displaySetFramePixels() actually wrote new
		// pixels; this avoids redundant texture updates without changing the
		// script-visible timing of forceupdate(). If no pixels changed, leave
		// the idle-presented flag false so the main loop still does its cheap
		// cursor-only updateScreen() and mouse movement stays responsive.
		presented = setRuntime().presentPendingScreenUpdate(*this);
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
		const bool propsDirtyAfterIdle = _propRuntime.dirty();
		refreshPropsIfDirty();
		if (!_framePacingRuntime.forceUpdatePresentedDuringIdle() || propsDirtyAfterIdle)
			_system->updateScreen();
		if (_framePacingRuntime.frameRate() == 0 && _setRuntime.transitionType() == kSetTransitionNone)
			_system->delayMillis(10);
	}

	return Common::kNoError;
}

} // End of namespace Cyberflix
