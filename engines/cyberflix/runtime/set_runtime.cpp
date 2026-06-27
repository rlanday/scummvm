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

// Temporary diagnostics for the Grand Staircase pop-in investigation; these
// record each compositor pass and the actors included in that pass.
static bool isGrandStaircaseSetName(const Common::String &name) {
	return name.equalsIgnoreCase("gstair1") ||
			name.equalsIgnoreCase("gstair2") ||
			name.equalsIgnoreCase("gstair3");
}

static int countVisibleActorsInCurrentSet(CyberflixEngine &engine) {
	if (!engine.setRuntime().set() || !engine.setRuntime().set()->isOpen())
		return 0;

	const Common::String &setName = engine.setRuntime().set()->setName();
	int count = 0;
	const Common::Array<Common::SharedPtr<Cast> > &casts = engine.actorRuntime().casts();
	for (uint32 c = 0; c < casts.size(); ++c) {
		for (uint32 i = 0; i < casts[c]->actorCount(); ++i) {
			const Cast::Actor &actor = casts[c]->actor(i);
			if (actor.visible && actor.setName.equalsIgnoreCase(setName))
				++count;
		}
	}
	return count;
}

// sendtoscene(name, message): dispatch the message against [scene script, set
// script, BOOTFILE res2] for the named scene, without changing the currently
// rendered scene (TI.EXE FUN_004311e0/FUN_00431200).
void SetRuntime::sendToScene(CyberflixEngine &engine, const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!set() || !set()->isOpen()) {
		warning("Cyberflix: sendtoscene('%s') with no set open", scene.c_str());
		return;
	}
	int index = set()->findScene(scene);
	if (index < 0) {
		warning("Cyberflix: set '%s' has no scene named '%s'",
				set()->name().c_str(), scene.c_str());
		return;
	}
	if (!message.empty())
		engine.dispatchSceneMessage(static_cast<uint32>(index), message, args);
}

Value SetRuntime::sendToSceneFx(CyberflixEngine &engine, const Common::String &scene,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!set() || !set()->isOpen()) {
		warning("Cyberflix: sendtoscenefx('%s') with no set open", scene.c_str());
		return Value();
	}
	int index = set()->findScene(scene);
	if (index < 0) {
		warning("Cyberflix: set '%s' has no scene named '%s'",
				set()->name().c_str(), scene.c_str());
		return Value();
	}
	Common::SharedPtr<Script> sceneScript = set()->sceneScriptShared(static_cast<uint32>(index));
	Common::SharedPtr<Script> setScript = set()->setScriptShared();
	return engine.dispatchWithScopesValue(sceneScript.get(), setScript.get(),
			set()->sceneName(static_cast<uint32>(index)), Common::String(), message, args, "scenefx");
}

// sendtopainting(scene, view, painting, message): dispatch the message over the
// current SET's painting chain. BEDSIT1's poster records have no own script, so
// the set script handles mousedown/setcursor via 0xfbb (target painting name).
void SetRuntime::sendToPainting(CyberflixEngine &engine, const Common::String &sceneName, const Common::String &viewName,
		const Common::String &painting, const Common::String &message,
		const Common::Array<Value> &args) {
	if (!set() || !set()->isOpen()) {
		warning("Cyberflix: sendtopainting('%s') with no set open", painting.c_str());
		return;
	}
	int sceneIdx = sceneName.empty() ? this->scene() : set()->findScene(sceneName);
	if (sceneIdx < 0) {
		warning("Cyberflix: sendtopainting('%s'): no scene '%s'",
				painting.c_str(), sceneName.c_str());
		return;
	}
	Common::String activeView = !viewName.empty() ? viewName : this->view();

	Common::SharedPtr<Script> paintingScript, sceneScript, setScript;
	if (!set()->paintingDispatchScripts(static_cast<uint32>(sceneIdx), activeView, painting,
			paintingScript, sceneScript, setScript)) {
		warning("Cyberflix: sendtopainting('%s'): no view '%s'",
				painting.c_str(), activeView.c_str());
		return;
	}
	engine.dispatchWithThreeScopesValue(paintingScript.get(), sceneScript.get(), setScript.get(),
			painting, painting, message, args, "painting");
	engine.propRuntime().refreshPropsIfDirty(engine);
}

Value SetRuntime::sendToPaintingFx(CyberflixEngine &engine, const Common::String &sceneName,
		const Common::String &viewName, const Common::String &painting,
		const Common::String &message, const Common::Array<Value> &args) {
	if (!set() || !set()->isOpen()) {
		warning("Cyberflix: sendtopaintingfx('%s') with no set open", painting.c_str());
		return Value();
	}
	int sceneIdx = sceneName.empty() ? this->scene() : set()->findScene(sceneName);
	if (sceneIdx < 0) {
		warning("Cyberflix: sendtopaintingfx('%s'): no scene '%s'",
				painting.c_str(), sceneName.c_str());
		return Value();
	}
	Common::String activeView = !viewName.empty() ? viewName : this->view();

	Common::SharedPtr<Script> paintingScript, sceneScript, setScript;
	if (!set()->paintingDispatchScripts(static_cast<uint32>(sceneIdx), activeView, painting,
			paintingScript, sceneScript, setScript)) {
		warning("Cyberflix: sendtopaintingfx('%s'): no view '%s'",
				painting.c_str(), activeView.c_str());
		return Value();
	}
	return engine.dispatchWithThreeScopesValue(paintingScript.get(), sceneScript.get(), setScript.get(),
			painting, painting, message, args, "paintingfx");
}

void SetRuntime::navigateSet(CyberflixEngine &engine, const Common::String &action) {
	if (!visible() || !set() || !set()->isOpen() || scene() < 0)
		return;
	if (transitionType() != kSetTransitionNone)
		return;

	int viewIdx = set()->findView(static_cast<uint32>(scene()), view());
	if (viewIdx < 0)
		viewIdx = set()->viewTagAtAngle(static_cast<uint32>(scene()), static_cast<uint32>(table()), static_cast<uint32>(angle()));
	if (viewIdx < 0) {
		warning("Cyberflix: cannot navigate set '%s' scene '%s': current view '%s' not found",
				set()->name().c_str(), set()->sceneName(static_cast<uint32>(scene())).c_str(), view().c_str());
		return;
	}

	if (action.equalsIgnoreCase("left") || action.equalsIgnoreCase("right")) {
		const int turnTable = action.equalsIgnoreCase("left") ? 1 : 0;
		int startAngle = set()->angleForView(static_cast<uint32>(scene()), static_cast<uint32>(turnTable), viewIdx);
		if (startAngle < 0 || set()->nextTaggedAngle(static_cast<uint32>(scene()), static_cast<uint32>(turnTable), startAngle) < 0) {
			warning("Cyberflix: set '%s' scene '%s' has no %s turn from view '%s'",
					set()->name().c_str(), set()->sceneName(static_cast<uint32>(scene())).c_str(),
					action.c_str(), view().c_str());
			return;
		}
		if (!engine.closeCurrentSceneForNavigation())
			return;
		if (!set()->applyPanoramaFrame(static_cast<uint32>(scene()), static_cast<uint32>(turnTable), static_cast<uint32>(startAngle),
				frameSequence())) {
			warning("Cyberflix: set '%s' failed to start %s turn from view '%s'",
					set()->name().c_str(), action.c_str(), view().c_str());
			return;
		}
		table() = turnTable;
		angle() = startAngle;
		transitionType() = kSetTransitionTurn;
		displaySetFrame(engine, frameSequence());
		return;
	}

	if (action.equalsIgnoreCase("strait")) {
		uint32 transitionId = set()->forwardTransitionForView(static_cast<uint32>(scene()), viewIdx);
		if (transitionId == 0)
			return;
		uint32 count = set()->transitionFrameCount(transitionId);
		if (count < 2) {
			warning("Cyberflix: set '%s' transition %u has too few frames (%u)",
					set()->name().c_str(), transitionId, count);
			return;
		}
		if (!engine.closeCurrentSceneForNavigation())
			return;
		if (!set()->applyTransitionFrame(transitionId, 0, frameSequence())) {
			warning("Cyberflix: set '%s' failed to start forward transition %u",
					set()->name().c_str(), transitionId);
			return;
		}
		transitionType() = kSetTransitionForward;
		transitionResource() = transitionId;
		transitionFrame() = 0;
		displaySetFrame(engine, frameSequence());
	}
}

void SetRuntime::advanceSetTransition(CyberflixEngine &engine) {
	if (!visible() || transitionType() == kSetTransitionNone ||
			!set() || !set()->isOpen() || scene() < 0)
		return;

	if (transitionType() == kSetTransitionTurn) {
		uint32 count = set()->angleCount(static_cast<uint32>(scene()), static_cast<uint32>(table()));
		if (count == 0) {
			transitionType() = kSetTransitionNone;
			return;
		}

		int nextAngle = (angle() + 1) % static_cast<int>(count);
		if (!set()->applyPanoramaFrame(static_cast<uint32>(scene()), static_cast<uint32>(table()), static_cast<uint32>(nextAngle),
				frameSequence())) {
			transitionType() = kSetTransitionNone;
			warning("Cyberflix: failed to advance SET turn transition");
			return;
		}
		angle() = nextAngle;
		int viewIdx = set()->viewTagAtAngle(static_cast<uint32>(scene()), static_cast<uint32>(table()), static_cast<uint32>(nextAngle));
		if (viewIdx >= 0)
			view() = set()->viewName(static_cast<uint32>(scene()), static_cast<uint32>(viewIdx));
		displaySetFrame(engine, frameSequence());

		if (viewIdx >= 0) {
			transitionType() = kSetTransitionNone;
			Common::Array<Value> noArgs;
			engine.dispatchSceneMessage(static_cast<uint32>(scene()), "openscene", noArgs);
		}
		return;
	}

	if (transitionType() == kSetTransitionForward) {
		uint32 count = set()->transitionFrameCount(transitionResource());
		uint32 nextFrame = transitionFrame() + 1;
		if (nextFrame >= count) {
			transitionType() = kSetTransitionNone;
			transitionResource() = 0;
			transitionFrame() = 0;
			return;
		}

		if (!set()->applyTransitionFrame(transitionResource(), nextFrame, frameSequence())) {
			transitionType() = kSetTransitionNone;
			warning("Cyberflix: failed to advance SET forward transition %u", transitionResource());
			return;
		}
		transitionFrame() = nextFrame;
		displaySetFrame(engine, frameSequence());

		if (nextFrame == count - 1) {
			uint32 destinationScene = 0;
			Common::String destinationView;
			int destinationAngle = 0;
			if (!set()->transitionDestination(transitionResource(), destinationScene, destinationView, destinationAngle)) {
				warning("Cyberflix: set '%s' transition %u has no resolvable destination",
						set()->name().c_str(), transitionResource());
				transitionType() = kSetTransitionNone;
				return;
			}
			scene() = static_cast<int>(destinationScene);
			table() = 0;
			angle() = destinationAngle;
			view() = destinationView;
			transitionType() = kSetTransitionNone;
			transitionResource() = 0;
			transitionFrame() = 0;
			Common::Array<Value> noArgs;
			engine.dispatchSceneMessage(static_cast<uint32>(scene()), "openscene", noArgs);
		}
	}
}

void SetRuntime::renderSetScene(CyberflixEngine &engine, int sceneIdx, int tableIdx, int angleIdx, const Common::String &viewName) {
	if (!set() || !set()->isOpen()) {
		warning("Cyberflix: renderSetScene with no set open");
		return;
	}
	const bool diag = isGrandStaircaseSetName(set()->setName());
	const uint32 startMs = diag ? engine._system->getMillis() : 0;
	if (diag)
		warning("Cyberflix: GSTAIR diag: t=%u renderSetScene begin set=%s scene=%d table=%d angle=%d viewArg=%s visibleActors=%d dirty=%d pending=%d paletteBlack=%d",
				startMs, set()->setName().c_str(), sceneIdx, tableIdx, angleIdx,
				viewName.c_str(), countVisibleActorsInCurrentSet(engine),
				engine.propRuntime().dirty() ? 1 : 0, screenUpdatePending() ? 1 : 0,
				engine.paletteIsBlack() ? 1 : 0);

	if (!set()->renderScene(static_cast<uint32>(sceneIdx), static_cast<uint32>(tableIdx), static_cast<uint32>(angleIdx), frameSequence()))
		return;

	scene() = sceneIdx;
	table() = tableIdx;
	angle() = angleIdx;
	if (!viewName.empty()) {
		view() = viewName;
	} else {
		int viewIdx = set()->viewTagAtAngle(static_cast<uint32>(sceneIdx), static_cast<uint32>(tableIdx), static_cast<uint32>(angleIdx));
		if (viewIdx >= 0)
			view() = set()->viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx));
	}

	// If a puppet is visible, native changeset() only prepares the new SET's
	// retained backing image. The next compositor pass chooses the puppet branch
	// instead of drawing the room. ELEV1.PUP depends on this ordering: it calls
	// changeset() before ELEVGS.MOV, and the visible elevator puppet uses the same
	// palette as the movie. Drawing the destination room immediately would expose
	// the inventory bar under the elevator movie palette.
	const bool displaySet = visible() && !engine.puppetRuntime().isVisible();
	if (displaySet) {
		displaySetFrame(engine, frameSequence());
	} else {
		screenUpdatePending() = false;
		if (visible())
			engine.propRuntime().setDirty(true);
	}

	debug(1, "Cyberflix: rendered set '%s' scene %d '%s' angle %d (%ux%u)",
			set()->name().c_str(), sceneIdx, set()->sceneName(static_cast<uint32>(sceneIdx)).c_str(),
			angleIdx, frameSequence().width(), frameSequence().height());
	if (diag) {
		const uint32 nowMs = engine._system->getMillis();
		warning("Cyberflix: GSTAIR diag: t=%u renderSetScene end elapsed=%u activeView=%s visibleActors=%d dirty=%d pending=%d paletteBlack=%d",
				nowMs, nowMs - startMs, view().c_str(), countVisibleActorsInCurrentSet(engine),
				engine.propRuntime().dirty() ? 1 : 0, screenUpdatePending() ? 1 : 0,
				engine.paletteIsBlack() ? 1 : 0);
	}
}

void SetRuntime::displaySetFrame(CyberflixEngine &engine, const FrameImage &frame) {
	displaySetFramePixels(engine, frame.pixels.begin(), frame.width, frame.height);
}

void SetRuntime::displaySetFrame(CyberflixEngine &engine, const FrameSequence &frame) {
	if (frame.empty())
		return;
	displaySetFramePixels(engine, frame.pixels(), frame.width(), frame.height(), &frame);
}

void SetRuntime::displaySetFramePixels(CyberflixEngine &engine, const byte *pixels, uint16 width, uint16 height,
		const FrameSequence *depthFrame) {
	if (!visible() || !set() || !set()->isOpen())
		return;
	const bool diag = isGrandStaircaseSetName(set()->setName());
	const uint32 startMs = diag ? engine._system->getMillis() : 0;
	if (diag)
		warning("Cyberflix: GSTAIR diag: t=%u displaySetFrame begin set=%s scene=%d view=%s size=%ux%u visibleActors=%d dirty=%d pending=%d paletteBlack=%d",
				startMs, set()->setName().c_str(), scene(), view().c_str(), width, height,
				countVisibleActorsInCurrentSet(engine), engine.propRuntime().dirty() ? 1 : 0,
				screenUpdatePending() ? 1 : 0, engine.paletteIsBlack() ? 1 : 0);
	if (engine.puppetRuntime().isVisible()) {
		screenUpdatePending() = false;
		engine.propRuntime().setDirty(true);
		return;
	}

	Palette rgb = {};
	// As with stage nodes: while the screen palette is black the room is painted
	// invisibly and revealed later by blacktoscreen('set', n).
	if (set()->loadSetPalette(rgb) && !engine.paletteIsBlack())
		engine.programPalette(rgb);

	engine.propRuntime().advancePropPoses();
	const FrameImage *stageBg = engine.stageRuntime().stageShellFrame();
	Graphics::Surface *screen = engine._system->lockScreen();
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
	int x0 = set()->viewLeft();
	int y0 = set()->viewTop();
	copyFramePixelsToScreen(*screen, pixels, width, height, x0, y0);
	// World/SET-space SHOP props (propset + propxyz) are projected through the
	// active panorama camera before the screen-space inventory/UI overlays.
	Set::CameraData cameraData;
	bool haveCamera = false;
	if (transitionType() == kSetTransitionForward) {
		haveCamera = set()->transitionCameraData(transitionResource(),
				transitionFrame(), cameraData);
	} else if (scene() >= 0) {
		haveCamera = set()->cameraData(static_cast<uint32>(scene()), static_cast<uint32>(table()),
				static_cast<uint32>(angle()), cameraData);
	}
	if (haveCamera) {
		Shop::WorldCamera camera = makeWorldCamera(cameraData);
		Common::Array<const Shop::Prop *> worldDraw;
		Common::Array<const Shop *> worldShop;
		Common::Array<int16> worldDepths;
		Common::Array<const Cast::Actor *> actorDraw;
		Common::Array<const Cast *> actorCast;
		Common::Array<int16> actorDepths;
		engine.propRuntime().collectWorldProps(engine, worldDraw, worldShop, worldDepths, camera);
		engine.actorRuntime().collectWorldActors(engine, actorDraw, actorCast, actorDepths, camera);
		if (diag)
			warning("Cyberflix: GSTAIR diag: t=%u displaySetFrame collected worldProps=%u worldActors=%u cameraHeading=%d camera=(%d,%d,%d) viewport=(%d,%d,%d,%d)",
					engine._system->getMillis(), worldDraw.size(), actorDraw.size(),
					camera.heading, camera.cameraX, camera.cameraY, camera.cameraZ,
					camera.viewportLeft, camera.viewportTop, camera.viewportRight, camera.viewportBottom);
		Common::Rect viewport(camera.viewportLeft, camera.viewportTop,
				camera.viewportRight, camera.viewportBottom);
		uint32 propIndex = 0, actorIndex = 0;
		while (propIndex < worldDraw.size() || actorIndex < actorDraw.size()) {
			const bool drawActor = actorIndex < actorDraw.size() &&
					(propIndex >= worldDraw.size() || actorDepths[actorIndex] >= worldDepths[propIndex]);
			if (drawActor) {
				Cast::ActorRenderResult rendered = actorCast[actorIndex]->renderWorldActor(*actorDraw[actorIndex],
						camera, set()->setName());
				if (diag)
					warning("Cyberflix: GSTAIR diag: t=%u renderActor[%u/%u] name=%s cast=%s star=%s pose=%s poseIndex=%u visible=%d xyz=(%d,%d,%d) deg=%d scale=%d depth=%d rendered=%d rect=(%d,%d,%d,%d)",
							engine._system->getMillis(), actorIndex + 1, actorDraw.size(),
							actorDraw[actorIndex]->name.c_str(), actorCast[actorIndex]->name().c_str(),
							actorDraw[actorIndex]->sceneName.c_str(), actorDraw[actorIndex]->shapeName.c_str(),
							actorDraw[actorIndex]->poseIndex, actorDraw[actorIndex]->visible ? 1 : 0,
							actorDraw[actorIndex]->x, actorDraw[actorIndex]->y, actorDraw[actorIndex]->z,
							actorDraw[actorIndex]->angle, actorDraw[actorIndex]->scale,
							actorDepths[actorIndex], rendered.valid ? 1 : 0,
							rendered.rect.left, rendered.rect.top, rendered.rect.right, rendered.rect.bottom);
				if (rendered.valid)
					drawScaledCel(screen, rendered.cel, rendered.rect, viewport, depthFrame, rendered.depthBucket);
				++actorIndex;
			} else {
				Shop::PropRenderResult rendered = worldShop[propIndex]->renderWorldProp(*worldDraw[propIndex],
						camera, set()->setName());
				if (rendered.valid)
					drawScaledCel(screen, *rendered.cel, rendered.rect, viewport, depthFrame, rendered.depthBucket);
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
		engine.propRuntime().collectScreenProps(draw, drawShop);
		for (uint32 i = 0; i < draw.size(); ++i) {
			Shop::PropRenderResult rendered = drawShop[i]->renderProp(*draw[i]);
			if (!rendered.valid)
				continue;
			drawCel(screen, *rendered.cel, rendered.rect, Common::Rect(kScreenWidth, kScreenHeight));
		}
	}
	engine.propRuntime().setDirty(false);
	engine.propRuntime().clearDirtyRects();
	engine._system->unlockScreen();
	// SET compositing can be driven many times from scene scripts. Mark the
	// backend upload as pending and let forceupdate()/the main loop present once;
	// otherwise scripts that call forceupdate() repeatedly pay an OpenGL texture
	// upload even when no compositor pass drew new pixels.
	screenUpdatePending() = true;

	// Default arrow until per-view hotspot hit-testing (directional cursors) is
	// implemented. Views are the scene's hotspot lists.
	if (engine.setGameCursor("CURS.ARROW"))
		CursorMan.showMouse(true);
	if (diag) {
		const uint32 nowMs = engine._system->getMillis();
		warning("Cyberflix: GSTAIR diag: t=%u displaySetFrame end elapsed=%u visibleActors=%d dirty=%d pending=%d paletteBlack=%d",
				nowMs, nowMs - startMs, countVisibleActorsInCurrentSet(engine),
				engine.propRuntime().dirty() ? 1 : 0, screenUpdatePending() ? 1 : 0,
				engine.paletteIsBlack() ? 1 : 0);
	}
}

bool SetRuntime::presentPendingScreenUpdate(CyberflixEngine &engine) {
	if (!screenUpdatePending())
		return false;
	if (set() && set()->isOpen() && isGrandStaircaseSetName(set()->setName()))
		warning("Cyberflix: GSTAIR diag: t=%u presentPendingScreenUpdate set=%s scene=%d view=%s paletteBlack=%d",
				engine._system->getMillis(), set()->setName().c_str(), scene(),
				view().c_str(), engine.paletteIsBlack() ? 1 : 0);
	engine._system->updateScreen();
	screenUpdatePending() = false;
	return true;
}

} // End of namespace Cyberflix
