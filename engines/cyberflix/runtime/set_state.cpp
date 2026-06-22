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

#include "cyberflix/cyberflix.h"
#include "cyberflix/resource_helpers.h"
#include "cyberflix/set.h"
#include "cyberflix/stage.h"

namespace Cyberflix {

void SetRuntime::openSetFile(CyberflixEngine &engine, const Common::String &name,
		const Common::String &sceneName, const Common::String &viewName) {
	if (name.empty())
		return;

	int previousHeading = 0;
	if (set() && set()->isOpen() && this->scene() >= 0) {
		Set::CameraData cameraData;
		if (transitionType() == kSetTransitionForward && transitionResource() != 0) {
			if (set()->transitionCameraData(transitionResource(), transitionFrame(), cameraData))
				previousHeading = cameraData.heading;
		} else if (set()->cameraData(static_cast<uint32>(this->scene()), static_cast<uint32>(table()),
				static_cast<uint32>(angle()), cameraData)) {
			previousHeading = cameraData.heading;
		}
	}

	Common::ScopedPtr<Set> newSet(new Set());
	if (!newSet->open(name)) {
		warning("Cyberflix: opensetfile('%s') failed", name.c_str());
		return;
	}
	set().reset(newSet.release());
	this->scene() = -1;
	table() = 0;
	angle() = 0;
	this->view().clear();
	transitionType() = kSetTransitionNone;
	transitionResource() = 0;
	transitionFrame() = 0;
	frameSequence().clear();
	visible() = true;
	debug(1, "Cyberflix: set '%s' open (%u scenes, name '%s', default scene '%s' view '%s')",
			name.c_str(), set()->sceneCount(), set()->setName().c_str(),
			set()->defaultScene().c_str(), set()->defaultView().c_str());
	engine.actorRuntime().refreshActorStarPositions(engine);

	// FUN_004307f0: when no scene/view argument is given, the defaults come
	// from the set's master header (+0xa0e / +0xa1e).
	Common::String useScene = !sceneName.empty() ? sceneName : set()->defaultScene();
	Common::String useView = !viewName.empty() ? viewName : set()->defaultView();

	// The original finishes opensetfile by sending the system messages
	// (FUN_00430fa0): it runs openset() against [set script, BOOTFILE res2],
	// then (if the set did not change) runs "<scene>", openscene() through the
	// sendtoscene executor FUN_004311e0, which paints and dispatches against
	// [scene script, set script, BOOTFILE res2].
	Common::Array<Value> noArgs;
	Common::String openedName = set()->setName();
	engine.dispatchSetMessage("openset", noArgs);

	if (set() && set()->setName() == openedName && !useScene.empty()) {
		int sceneIdx = set()->findScene(useScene);
		if (sceneIdx < 0) {
			if (set()->sceneCount() == 0) {
				warning("Cyberflix: set '%s' has no scenes", set()->name().c_str());
				return;
			}
			debug(1, "Cyberflix: set '%s' scene '%s' not found, using first scene '%s'",
					set()->name().c_str(), useScene.c_str(), set()->sceneName(0).c_str());
			sceneIdx = 0;
		}
		Common::String actualScene = set()->sceneName(static_cast<uint32>(sceneIdx));
		// View select (TI.EXE FUN_00433960 stores the view, FUN_004425e0 aims
		// the camera at the panorama record tagged with the view's index).
		int angle = 0;
		Common::String activeView;
		if (!useView.empty()) {
			int viewIdx = set()->findView(static_cast<uint32>(sceneIdx), useView);
			if (viewIdx < 0) {
				// FUN_00433960 preserves the requested view name, but if the
				// selected scene lacks it, it chooses the view whose authored
				// heading is closest to the previous camera heading before
				// calling FUN_004425e0.
				viewIdx = set()->nearestViewForHeading(static_cast<uint32>(sceneIdx), previousHeading);
				if (viewIdx >= 0)
					debug(1, "Cyberflix: opensetfile view '%s' not found in scene '%s', using nearest view '%s'",
							useView.c_str(), actualScene.c_str(),
							set()->viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx)).c_str());
			}
			int viewAngle = set()->angleForView(static_cast<uint32>(sceneIdx), 0, viewIdx);
			if (viewAngle >= 0) {
				angle = viewAngle;
				activeView = set()->viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx));
			} else {
				warning("Cyberflix: opensetfile view '%s' not found in scene '%s'",
						useView.c_str(), actualScene.c_str());
			}
		}
		renderSetScene(engine, sceneIdx, 0, angle, activeView);
		engine.dispatchSceneMessage(static_cast<uint32>(sceneIdx), "openscene", noArgs);
	}
}

// closesetfile(): send the closing system messages, then drop the open set
// (TI.EXE builtin 0x2f01, core FUN_00430b20: FUN_00431050 first sends
// '"<scene>", closescene()' through the sendtoscene executor and then runs
// 'closeset()' in set scope, before FUN_00430ba0 releases the archive). The
// global closeset() calls putdownsound() which halts the room theme, and it
// switches on currentset(), so the messages must go out while the set is
// still current.
void SetRuntime::closeSetFile(CyberflixEngine &engine) {
	if (set() && set()->isOpen()) {
		Common::Array<Value> noArgs;
		Common::String openedName = set()->setName();
		if (scene() >= 0)
			engine.dispatchSceneMessage(static_cast<uint32>(scene()), "closescene", noArgs);
		if (set() && set()->setName() == openedName)
			engine.dispatchSetMessage("closeset", noArgs);
	}
	set().reset();
	scene() = -1;
	table() = 0;
	angle() = 0;
	view().clear();
	transitionType() = kSetTransitionNone;
	transitionResource() = 0;
	transitionFrame() = 0;
	frameSequence().clear();
	visible() = false;
}

SetRuntime::Snapshot SetRuntime::snapshot() const {
	Snapshot state;
	if (set() && set()->isOpen()) {
		state.fileName = set()->name();
		state.setName = set()->setName();
		state.scene = scene();
		if (scene() >= 0 && static_cast<uint32>(scene()) < set()->sceneCount())
			state.sceneName = set()->sceneName(static_cast<uint32>(scene()));
		state.table = table();
		state.angle = angle();
		state.view = view();
		state.visible = visible();
		state.transitionType = transitionType();
		state.transitionResource = transitionResource();
		state.transitionFrame = transitionFrame();
	}
	return state;
}

bool SetRuntime::restoreSnapshot(const Snapshot &snapshot) {
	reset();
	if (snapshot.fileName.empty())
		return true;

	Common::ScopedPtr<Set> newSet(new Set());
	if (!newSet->open(snapshot.fileName)) {
		warning("Cyberflix: load could not reopen set '%s'", snapshot.fileName.c_str());
		return false;
	}

	set().reset(newSet.release());
	scene() = snapshot.scene;
	if (scene() < 0 || static_cast<uint32>(scene()) >= set()->sceneCount())
		scene() = set()->findScene(snapshot.sceneName);
	table() = snapshot.table;
	if (table() < 0 || table() > 1)
		table() = 0;
	angle() = snapshot.angle;
	if (scene() >= 0) {
		uint32 angleCount = set()->angleCount(static_cast<uint32>(scene()), static_cast<uint32>(table()));
		if (angleCount && static_cast<uint32>(angle()) >= angleCount)
			angle() = 0;
	}
	view() = snapshot.view;
	visible() = snapshot.visible;
	transitionType() = snapshot.transitionType <= kSetTransitionForward ?
			snapshot.transitionType : kSetTransitionNone;
	transitionResource() = snapshot.transitionResource;
	transitionFrame() = snapshot.transitionFrame;
	return true;
}

// currentset(): the open set's EMBEDDED name (master header +0x070, e.g.
// 'bedsit1' -- no '.set'), or 'none' (TI.EXE builtin 0x4e55 returns the set
// record's name field, copied from the header by FUN_004307f0; setupsound,
// themetype and changeset all switch/compare on this form).
Common::String SetRuntime::currentSet() const {
	if (set() && set()->isOpen())
		return set()->setName();
	return "none";
}

// currentview(): DAT_004611dc in TI.EXE (FUN_00431ce0), or "Moving" while a
// panorama transition resource is active.
Common::String SetRuntime::currentView() const {
	if (transitionType() != kSetTransitionNone)
		return "Moving";
	if (set() && set()->isOpen() && !view().empty())
		return view();
	return "none";
}

static bool activeSetCameraData(const SetRuntime &runtime, Set::CameraData &camera) {
	if (!runtime.set() || !runtime.set()->isOpen())
		return false;
	if (runtime.transitionType() == kSetTransitionForward)
		return runtime.set()->transitionCameraData(runtime.transitionResource(), runtime.transitionFrame(), camera);
	if (runtime.scene() < 0)
		return false;
	return runtime.set()->cameraData(static_cast<uint32>(runtime.scene()),
			static_cast<uint32>(runtime.table()), static_cast<uint32>(runtime.angle()), camera);
}

static int currentSetHeading(const SetRuntime &runtime) {
	Set::CameraData camera;
	return activeSetCameraData(runtime, camera) ? camera.heading : 0;
}

static bool chooseSceneView(const SetRuntime &runtime, int sceneIdx, const Common::String &requestedView,
		int previousHeading, bool fallbackToNearest, int &targetAngle, Common::String &targetView,
		const char *operation) {
	targetAngle = 0;
	targetView.clear();
	if (!runtime.set() || !runtime.set()->isOpen() || sceneIdx < 0)
		return false;

	int viewIdx = requestedView.empty() ? -1 :
			runtime.set()->findView(static_cast<uint32>(sceneIdx), requestedView);
	if (viewIdx < 0 && fallbackToNearest) {
		viewIdx = runtime.set()->nearestViewForHeading(static_cast<uint32>(sceneIdx), previousHeading);
		if (viewIdx >= 0 && !requestedView.empty())
			debug(1, "Cyberflix: %s view '%s' not found in scene '%s', using nearest view '%s'",
					operation, requestedView.c_str(),
					runtime.set()->sceneName(static_cast<uint32>(sceneIdx)).c_str(),
					runtime.set()->viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx)).c_str());
	}
	if (viewIdx < 0) {
		if (!requestedView.empty())
			warning("Cyberflix: %s view '%s' not found in scene '%s'",
					operation, requestedView.c_str(),
					runtime.set()->sceneName(static_cast<uint32>(sceneIdx)).c_str());
		return false;
	}

	int viewAngle = runtime.set()->angleForView(static_cast<uint32>(sceneIdx), 0, viewIdx);
	if (viewAngle < 0) {
		warning("Cyberflix: %s view '%s' has no panorama angle in scene '%s'",
				operation,
				runtime.set()->viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx)).c_str(),
				runtime.set()->sceneName(static_cast<uint32>(sceneIdx)).c_str());
		return false;
	}
	targetAngle = viewAngle;
	targetView = runtime.set()->viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx));
	return !targetView.empty();
}

// currentview(name): native dispatch B reaches TI.EXE FUN_00431940. It validates
// the requested view against the current scene (FUN_00433b30), closes the old
// scene (FUN_00430f30), copies DAT_004611dc, normalizes the camera
// (FUN_00433960/FUN_004425e0), then sends openscene via FUN_00430ec0.
Common::String SetRuntime::currentView(CyberflixEngine &engine, const Common::String *target) {
	if (!target || target->empty())
		return currentView();
	if (!set() || !set()->isOpen() || scene() < 0)
		return currentView();
	if (transitionType() != kSetTransitionNone)
		return currentView();
	if (view().equalsIgnoreCase(*target))
		return currentView();

	const int savedScene = scene();
	int targetAngle = 0;
	Common::String targetView;
	if (!chooseSceneView(*this, savedScene, *target, currentSetHeading(*this), false,
			targetAngle, targetView, "currentview"))
		return currentView();
	if (!engine.closeCurrentSceneForNavigation())
		return currentView();
	if (!set() || !set()->isOpen())
		return "none";

	renderSetScene(engine, savedScene, 0, targetAngle, targetView);
	Common::Array<Value> noArgs;
	engine.dispatchSceneMessage(static_cast<uint32>(savedScene), "openscene", noArgs);
	return currentView();
}

static int selectXYZ(int selector, int x, int y, int z) {
	switch (selector) {
	case 1:
		return x;
	case 2:
		return y;
	case 3:
		return z;
	case 4:
		return (static_cast<int32>(static_cast<int16>(x)) << 16) |
				(static_cast<int32>(static_cast<int16>(y)) & 0xffff);
	default:
		return 0;
	}
}

int SetRuntime::currentDeg() const {
	Set::CameraData camera;
	if (!activeSetCameraData(*this, camera))
		return -1;
	return camera.heading;
}

int SetRuntime::cameraXYZ(int selector) const {
	Set::CameraData camera;
	if (!activeSetCameraData(*this, camera))
		return 0;
	return selectXYZ(selector, camera.cameraX, camera.cameraY, camera.cameraZ);
}

int SetRuntime::playerXYZ(int selector) const {
	Set::CameraData camera;
	if (!activeSetCameraData(*this, camera))
		return 0;

	// FUN_00442e90 computes the player point from the camera point by stepping
	// backward along the heading by DAT_00461196. The retail EXE initializes
	// that distance global to zero and no Titanic script writes it yet.
	const int kPlayerCameraDistance = 0;
	const int x = camera.cameraX -
			fixedShift14(nativeTrigSin(camera.heading) * kPlayerCameraDistance);
	const int y = camera.cameraY -
			fixedShift14(nativeTrigCos(camera.heading) * kPlayerCameraDistance);
	return selectXYZ(selector, x, y, camera.cameraZ);
}

// currentscene([arg]): no-arg reads DAT_004611cc. With "left"/"right"/"strait",
// BOOTFILE res2's keydown fallback reaches TI.EXE FUN_00430c70/FUN_00442140 to
// navigate the current set; other strings are scene names to switch to.
Common::String SetRuntime::currentScene(CyberflixEngine &engine, const Common::String *target) {
	if (!set() || !set()->isOpen() || scene() < 0)
		return "none";

	if (target && !target->empty()) {
		if (target->equalsIgnoreCase("left") || target->equalsIgnoreCase("right") ||
				target->equalsIgnoreCase("strait")) {
			navigateSet(engine, *target);
		} else {
			int sceneIdx = set()->findScene(*target);
			if (sceneIdx >= 0) {
				if (!engine.closeCurrentSceneForNavigation())
					return currentScene(engine, nullptr);
				if (!set() || !set()->isOpen())
					return "none";
				int targetAngle = 0;
				Common::String targetView = set()->defaultView();
				int viewIdx = set()->findView(static_cast<uint32>(sceneIdx), targetView);
				int viewAngle = set()->angleForView(static_cast<uint32>(sceneIdx), 0, viewIdx);
				if (viewAngle >= 0)
					targetAngle = viewAngle;
				else
					targetView.clear();
				renderSetScene(engine, sceneIdx, 0, targetAngle, targetView);
				Common::Array<Value> noArgs;
				engine.dispatchSceneMessage(static_cast<uint32>(sceneIdx), "openscene", noArgs);
			} else {
				warning("Cyberflix: currentscene('%s'): no such scene", target->c_str());
			}
		}
	}

	return (set() && set()->isOpen() && scene() >= 0) ?
			set()->sceneName(static_cast<uint32>(scene())) : Common::String("none");
}

int SetRuntime::countPaintings(const Common::String &scene, const Common::String &view) const {
	if (!set() || !set()->isOpen())
		return 0;
	int sceneIdx = set()->findScene(scene);
	return sceneIdx >= 0 ? static_cast<int>(set()->paintingCount(static_cast<uint32>(sceneIdx), view)) : 0;
}

Common::String SetRuntime::indexToPainting(const Common::String &scene,
		const Common::String &view, int index) const {
	if (!set() || !set()->isOpen() || index < 1)
		return Common::String();
	int sceneIdx = set()->findScene(scene);
	return sceneIdx >= 0 ? set()->indexToPainting(static_cast<uint32>(sceneIdx), view, static_cast<uint32>(index)) :
			Common::String();
}

bool SetRuntime::roadAhead(const Common::String &scene, const Common::String &view) const {
	if (!set() || !set()->isOpen())
		return false;
	int sceneIdx = set()->findScene(scene);
	if (sceneIdx < 0)
		return false;
	int viewIdx = set()->findView(static_cast<uint32>(sceneIdx), view);
	return set()->forwardTransitionForView(static_cast<uint32>(sceneIdx), viewIdx) != 0;
}

bool SetRuntime::setVisible(CyberflixEngine &engine, const bool *newVisible) {
	if (!set() || !set()->isOpen())
		return false;
	if (newVisible) {
		bool wasVisible = visible();
		visible() = *newVisible;
		if (visible()) {
			if (!wasVisible && scene() >= 0)
				renderSetScene(engine, scene(), table(), angle(), view());
			else
				engine.propRuntime().setDirty(true);
		} else {
			transitionType() = kSetTransitionNone;
			engine.propRuntime().setDirty(false);
			if (engine.stageRuntime().stage() && engine.stageRuntime().stage()->isOpen())
				engine.stageRuntime().renderStageNode(engine, engine.stageRuntime().node(), false);
		}
	}
	return visible();
}

} // End of namespace Cyberflix
