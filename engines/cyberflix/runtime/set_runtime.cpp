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

#include "common/debug.h"
#include "common/system.h"
#include "common/util.h"

#include "graphics/cursorman.h"
#include "graphics/surface.h"

#include "cyberflix/cast.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/image.h"
#include "cyberflix/runtime/graphics_helpers.h"
#include "cyberflix/runtime/set_helpers.h"
#include "cyberflix/set.h"
#include "cyberflix/shop.h"
#include "cyberflix/stage.h"

namespace Cyberflix {

// sendtoscene(name, message): dispatch the message against [scene script, set
// script, BOOTFILE res2] for the named scene, without changing the currently
// rendered scene (TI.EXE FUN_004311e0/FUN_00431200).
void CyberflixEngine::sendToScene(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen()) {
		warning("Cyberflix: sendtoscene('%s') with no set open", scene.c_str());
		return;
	}
	int index = _setRuntime.set()->findScene(scene);
	if (index < 0) {
		warning("Cyberflix: set '%s' has no scene named '%s'",
				_setRuntime.set()->name().c_str(), scene.c_str());
		return;
	}
	if (!message.empty())
		dispatchSceneMessage((uint32)index, message, args);
}

Value CyberflixEngine::sendToSceneFx(const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen()) {
		warning("Cyberflix: sendtoscenefx('%s') with no set open", scene.c_str());
		return Value();
	}
	int index = _setRuntime.set()->findScene(scene);
	if (index < 0) {
		warning("Cyberflix: set '%s' has no scene named '%s'",
				_setRuntime.set()->name().c_str(), scene.c_str());
		return Value();
	}
	Common::SharedPtr<Script> sceneScript = _setRuntime.set()->sceneScriptShared((uint32)index);
	Common::SharedPtr<Script> setScript = _setRuntime.set()->setScriptShared();
	return dispatchWithScopesValue(sceneScript.get(), setScript.get(),
			_setRuntime.set()->sceneName((uint32)index), Common::String(), message, args, "scenefx");
}

// sendtopainting(scene, view, painting, message): dispatch the message over the
// current SET's painting chain. BEDSIT1's poster records have no own script, so
// the set script handles mousedown/setcursor via 0xfbb (target painting name).
void CyberflixEngine::sendToPainting(const Common::String &sceneName, const Common::String &viewName,
		const Common::String &painting, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen()) {
		warning("Cyberflix: sendtopainting('%s') with no set open", painting.c_str());
		return;
	}
	int scene = sceneName.empty() ? _setRuntime.scene() : _setRuntime.set()->findScene(sceneName);
	if (scene < 0) {
		warning("Cyberflix: sendtopainting('%s'): no scene '%s'",
				painting.c_str(), sceneName.c_str());
		return;
	}
	Common::String view = !viewName.empty() ? viewName : _setRuntime.view();

	Common::SharedPtr<Script> paintingScript, sceneScript, setScript;
	if (!_setRuntime.set()->paintingDispatchScripts((uint32)scene, view, painting,
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
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen()) {
		warning("Cyberflix: sendtopaintingfx('%s') with no set open", painting.c_str());
		return Value();
	}
	int scene = sceneName.empty() ? _setRuntime.scene() : _setRuntime.set()->findScene(sceneName);
	if (scene < 0) {
		warning("Cyberflix: sendtopaintingfx('%s'): no scene '%s'",
				painting.c_str(), sceneName.c_str());
		return Value();
	}
	Common::String view = !viewName.empty() ? viewName : _setRuntime.view();

	Common::SharedPtr<Script> paintingScript, sceneScript, setScript;
	if (!_setRuntime.set()->paintingDispatchScripts((uint32)scene, view, painting,
			paintingScript, sceneScript, setScript)) {
		warning("Cyberflix: sendtopaintingfx('%s'): no view '%s'",
				painting.c_str(), view.c_str());
		return Value();
	}
	return dispatchWithThreeScopesValue(paintingScript.get(), sceneScript.get(), setScript.get(),
			painting, painting, message, args, "paintingfx");
}

void CyberflixEngine::navigateSet(const Common::String &action) {
	if (!_setRuntime.visible() || !_setRuntime.set() || !_setRuntime.set()->isOpen() || _setRuntime.scene() < 0)
		return;
	if (_setRuntime.transitionType() != kSetTransitionNone)
		return;

	int viewIdx = _setRuntime.set()->findView((uint32)_setRuntime.scene(), _setRuntime.view());
	if (viewIdx < 0)
		viewIdx = _setRuntime.set()->viewTagAtAngle((uint32)_setRuntime.scene(), (uint32)_setRuntime.table(), (uint32)_setRuntime.angle());
	if (viewIdx < 0) {
		warning("Cyberflix: cannot navigate set '%s' scene '%s': current view '%s' not found",
				_setRuntime.set()->name().c_str(), _setRuntime.set()->sceneName((uint32)_setRuntime.scene()).c_str(), _setRuntime.view().c_str());
		return;
	}

	if (action.equalsIgnoreCase("left") || action.equalsIgnoreCase("right")) {
		const int table = action.equalsIgnoreCase("left") ? 1 : 0;
		int startAngle = _setRuntime.set()->angleForView((uint32)_setRuntime.scene(), (uint32)table, viewIdx);
		if (startAngle < 0 || _setRuntime.set()->nextTaggedAngle((uint32)_setRuntime.scene(), (uint32)table, startAngle) < 0) {
			warning("Cyberflix: set '%s' scene '%s' has no %s turn from view '%s'",
					_setRuntime.set()->name().c_str(), _setRuntime.set()->sceneName((uint32)_setRuntime.scene()).c_str(),
					action.c_str(), _setRuntime.view().c_str());
			return;
		}
		if (!closeCurrentSceneForNavigation())
			return;
		if (!_setRuntime.set()->applyPanoramaFrame((uint32)_setRuntime.scene(), (uint32)table, (uint32)startAngle,
				_setRuntime.frameSequence())) {
			warning("Cyberflix: set '%s' failed to start %s turn from view '%s'",
					_setRuntime.set()->name().c_str(), action.c_str(), _setRuntime.view().c_str());
			return;
		}
		_setRuntime.table() = table;
		_setRuntime.angle() = startAngle;
		_setRuntime.transitionType() = kSetTransitionTurn;
		displaySetFrame(_setRuntime.frameSequence());
		return;
	}

	if (action.equalsIgnoreCase("strait")) {
		uint32 transitionId = _setRuntime.set()->forwardTransitionForView((uint32)_setRuntime.scene(), viewIdx);
		if (transitionId == 0)
			return;
		uint32 count = _setRuntime.set()->transitionFrameCount(transitionId);
		if (count < 2) {
			warning("Cyberflix: set '%s' transition %u has too few frames (%u)",
					_setRuntime.set()->name().c_str(), transitionId, count);
			return;
		}
		if (!closeCurrentSceneForNavigation())
			return;
		if (!_setRuntime.set()->applyTransitionFrame(transitionId, 0, _setRuntime.frameSequence())) {
			warning("Cyberflix: set '%s' failed to start forward transition %u",
					_setRuntime.set()->name().c_str(), transitionId);
			return;
		}
		_setRuntime.transitionType() = kSetTransitionForward;
		_setRuntime.transitionResource() = transitionId;
		_setRuntime.transitionFrame() = 0;
		displaySetFrame(_setRuntime.frameSequence());
	}
}

void CyberflixEngine::advanceSetTransition() {
	if (!_setRuntime.visible() || _setRuntime.transitionType() == kSetTransitionNone ||
			!_setRuntime.set() || !_setRuntime.set()->isOpen() || _setRuntime.scene() < 0)
		return;

	if (_setRuntime.transitionType() == kSetTransitionTurn) {
		uint32 count = _setRuntime.set()->angleCount((uint32)_setRuntime.scene(), (uint32)_setRuntime.table());
		if (count == 0) {
			_setRuntime.transitionType() = kSetTransitionNone;
			return;
		}

		int nextAngle = (_setRuntime.angle() + 1) % (int)count;
		if (!_setRuntime.set()->applyPanoramaFrame((uint32)_setRuntime.scene(), (uint32)_setRuntime.table(), (uint32)nextAngle,
				_setRuntime.frameSequence())) {
			_setRuntime.transitionType() = kSetTransitionNone;
			warning("Cyberflix: failed to advance SET turn transition");
			return;
		}
		_setRuntime.angle() = nextAngle;
		int viewIdx = _setRuntime.set()->viewTagAtAngle((uint32)_setRuntime.scene(), (uint32)_setRuntime.table(), (uint32)nextAngle);
		if (viewIdx >= 0)
			_setRuntime.view() = _setRuntime.set()->viewName((uint32)_setRuntime.scene(), (uint32)viewIdx);
		displaySetFrame(_setRuntime.frameSequence());

		if (viewIdx >= 0) {
			_setRuntime.transitionType() = kSetTransitionNone;
			Common::Array<Value> noArgs;
			dispatchSceneMessage((uint32)_setRuntime.scene(), "openscene", noArgs);
		}
		return;
	}

	if (_setRuntime.transitionType() == kSetTransitionForward) {
		uint32 count = _setRuntime.set()->transitionFrameCount(_setRuntime.transitionResource());
		uint32 nextFrame = _setRuntime.transitionFrame() + 1;
		if (nextFrame >= count) {
			_setRuntime.transitionType() = kSetTransitionNone;
			_setRuntime.transitionResource() = 0;
			_setRuntime.transitionFrame() = 0;
			return;
		}

		if (!_setRuntime.set()->applyTransitionFrame(_setRuntime.transitionResource(), nextFrame, _setRuntime.frameSequence())) {
			_setRuntime.transitionType() = kSetTransitionNone;
			warning("Cyberflix: failed to advance SET forward transition %u", _setRuntime.transitionResource());
			return;
		}
		_setRuntime.transitionFrame() = nextFrame;
		displaySetFrame(_setRuntime.frameSequence());

		if (nextFrame == count - 1) {
			uint32 scene = 0;
			Common::String view;
			int angle = 0;
			if (!_setRuntime.set()->transitionDestination(_setRuntime.transitionResource(), scene, view, angle)) {
				warning("Cyberflix: set '%s' transition %u has no resolvable destination",
						_setRuntime.set()->name().c_str(), _setRuntime.transitionResource());
				_setRuntime.transitionType() = kSetTransitionNone;
				return;
			}
			_setRuntime.scene() = (int)scene;
			_setRuntime.table() = 0;
			_setRuntime.angle() = angle;
			_setRuntime.view() = view;
			_setRuntime.transitionType() = kSetTransitionNone;
			_setRuntime.transitionResource() = 0;
			_setRuntime.transitionFrame() = 0;
			Common::Array<Value> noArgs;
			dispatchSceneMessage((uint32)_setRuntime.scene(), "openscene", noArgs);
		}
	}
}

void CyberflixEngine::renderSetScene(int scene, int table, int angle, const Common::String &view) {
	if (!_setRuntime.set() || !_setRuntime.set()->isOpen()) {
		warning("Cyberflix: renderSetScene with no set open");
		return;
	}

	if (!_setRuntime.set()->renderScene((uint32)scene, (uint32)table, (uint32)angle, _setRuntime.frameSequence()))
		return;

	_setRuntime.scene() = scene;
	_setRuntime.table() = table;
	_setRuntime.angle() = angle;
	if (!view.empty()) {
		_setRuntime.view() = view;
	} else {
		int viewIdx = _setRuntime.set()->viewTagAtAngle((uint32)scene, (uint32)table, (uint32)angle);
		if (viewIdx >= 0)
			_setRuntime.view() = _setRuntime.set()->viewName((uint32)scene, (uint32)viewIdx);
	}

	byte rgb[256 * 3];
	memset(rgb, 0, sizeof(rgb));
	// As with stage nodes: while the screen palette is black the room is
	// painted invisibly and revealed later by blacktoscreen('set', n).
	if (_setRuntime.visible() && _setRuntime.set()->loadSetPalette(rgb) && !paletteIsBlack())
		programPalette(rgb);

	if (_setRuntime.visible())
		displaySetFrame(_setRuntime.frameSequence());

	debug(1, "Cyberflix: rendered set '%s' scene %d '%s' angle %d (%ux%u)",
			_setRuntime.set()->name().c_str(), scene, _setRuntime.set()->sceneName((uint32)scene).c_str(),
			angle, _setRuntime.frameSequence().width(), _setRuntime.frameSequence().height());
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
	if (!_setRuntime.visible() || !_setRuntime.set() || !_setRuntime.set()->isOpen())
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
	int x0 = _setRuntime.set()->viewLeft();
	int y0 = _setRuntime.set()->viewTop();
	copyFramePixelsToScreen(*screen, pixels, width, height, x0, y0);
	// World/SET-space SHOP props (propset + propxyz) are projected through the
	// active panorama camera before the screen-space inventory/UI overlays.
	Set::CameraData cameraData;
	bool haveCamera = false;
	if (_setRuntime.transitionType() == kSetTransitionForward) {
		haveCamera = _setRuntime.set()->transitionCameraData(_setRuntime.transitionResource(),
				_setRuntime.transitionFrame(), cameraData);
	} else if (_setRuntime.scene() >= 0) {
		haveCamera = _setRuntime.set()->cameraData((uint32)_setRuntime.scene(), (uint32)_setRuntime.table(),
				(uint32)_setRuntime.angle(), cameraData);
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
						camera, _setRuntime.set()->setName(), actorCel, r, depth))
					drawScaledCel(screen, actorCel, r, viewport);
				++actorIndex;
			} else {
				if (worldShop[propIndex]->renderWorldProp(*worldDraw[propIndex], camera,
						_setRuntime.set()->setName(), propCel, r, depth))
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
	_propRuntime.setDirty(false);
	_propRuntime.clearDirtyRects();
	_system->unlockScreen();
	// SET compositing can be driven many times from scene scripts. Mark the
	// backend upload as pending and let forceupdate()/the main loop present once;
	// otherwise scripts that call forceupdate() repeatedly pay an OpenGL texture
	// upload even when no compositor pass drew new pixels.
	_setRuntime.screenUpdatePending() = true;

	// Default arrow until per-view hotspot hit-testing (directional cursors) is
	// implemented. Views (the scene's hotspot lists) are documented in
	// files/decomp/stage-notes.md.
	if (setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
}

bool CyberflixEngine::presentPendingScreenUpdate() {
	if (!_setRuntime.screenUpdatePending())
		return false;
	_system->updateScreen();
	_setRuntime.screenUpdatePending() = false;
	return true;
}

} // End of namespace Cyberflix
