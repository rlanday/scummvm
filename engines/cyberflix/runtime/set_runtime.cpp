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

} // End of namespace Cyberflix
