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

#include "dreamfactory/dreamfactory.h"
#include "dreamfactory/resource_helpers.h"
#include "dreamfactory/set.h"
#include "dreamfactory/stage.h"

namespace DreamFactory {

void SetRuntime::openSetFile(DreamFactoryEngine &engine, const Common::String &name,
		const Common::String &sceneName, const Common::String &viewName) {
	if (name.empty())
		return;

	// TI.EXE keeps DAT_004611b4 as a persistent camera-heading global. The
	// script-level changeset() wrapper may close/replace SET state, but native
	// absent-view fallback still uses the last composited heading rather than 0.
	updateLastCameraHeading();
	int previousHeading = _lastCameraHeading;

	Common::ScopedPtr<Set> newSet(new Set());
	if (!newSet->open(name)) {
		warning("DreamFactory: opensetfile('%s') failed", name.c_str());
		return;
	}
	_set.reset(newSet.release());
	_scene = -1;
	_table = 0;
	_angle = 0;
	_view.clear();
	_transitionType = kSetTransitionNone;
	_transitionResource = 0;
	_transitionFrame = 0;
	_frameSequence.clear();
	_visible = true;
	debug(1, "DreamFactory: set '%s' open (%u scenes, name '%s', default scene '%s' view '%s')",
			name.c_str(), _set->sceneCount(), _set->setName().c_str(),
			_set->defaultScene().c_str(), _set->defaultView().c_str());
	engine.actorRuntime().refreshActorStarPositions(engine);

	// FUN_004307f0: when no scene/view argument is given, the defaults come
	// from the set's master header (+0xa0e / +0xa1e).
	Common::String useScene = !sceneName.empty() ? sceneName : _set->defaultScene();
	Common::String useView = !viewName.empty() ? viewName : _set->defaultView();

	int sceneIdx = -1;
	if (!useScene.empty()) {
		sceneIdx = _set->findScene(useScene);
		if (sceneIdx < 0) {
			if (_set->sceneCount() == 0) {
				warning("DreamFactory: set '%s' has no scenes", _set->name().c_str());
				return;
			}
			debug(1, "DreamFactory: set '%s' scene '%s' not found, using first scene '%s'",
					_set->name().c_str(), useScene.c_str(), _set->sceneName(0).c_str());
			sceneIdx = 0;
		}
		Common::String actualScene = _set->sceneName(static_cast<uint32>(sceneIdx));
		// View select (TI.EXE FUN_00433960 stores the view, FUN_004425e0 aims
		// the camera at the panorama record tagged with the view's index).
		int angle = 0;
		Common::String activeView;
		if (!useView.empty()) {
			int viewIdx = _set->findView(static_cast<uint32>(sceneIdx), useView);
			if (viewIdx < 0) {
				// FUN_00433960 preserves the requested view name, but if the
				// selected scene lacks it, it chooses the view whose authored
				// heading is closest to the previous camera heading before
				// calling FUN_004425e0.
				viewIdx = _set->nearestViewForHeading(static_cast<uint32>(sceneIdx), previousHeading);
				if (viewIdx >= 0)
					debug(1, "DreamFactory: opensetfile view '%s' not found in scene '%s', using nearest view '%s'",
							useView.c_str(), actualScene.c_str(),
							_set->viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx)).c_str());
			}
			// Direct scene opens use the native stable-view renderer
			// (FUN_00433960 -> FUN_004425e0), which resolves named views against
			// panorama table B rather than the right-turn table A.
			int viewAngle = _set->angleForView(static_cast<uint32>(sceneIdx),
					Set::kNativeStablePanoramaTable, viewIdx);
			if (viewAngle >= 0) {
				angle = viewAngle;
				activeView = _set->viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx));
			} else {
				warning("DreamFactory: opensetfile view '%s' not found in scene '%s'",
						useView.c_str(), actualScene.c_str());
			}
		}
		renderSetScene(engine, sceneIdx, Set::kNativeStablePanoramaTable, angle, activeView);
	}

	// The original finishes opensetfile by sending the system messages
	// (FUN_00430fa0) after FUN_00433960 has already selected and painted the
	// current scene/view: it runs openset() against [set script, BOOTFILE res2],
	// then (if the set did not change) runs "<scene>", openscene() through the
	// sendtoscene executor FUN_004311e0.
	Common::Array<Value> noArgs;
	Common::String openedName = _set->setName();
	engine.dispatchSetMessage("openset", noArgs);

	if (_set && _set->setName() == openedName && sceneIdx >= 0)
		engine.dispatchSceneMessage(static_cast<uint32>(sceneIdx), "openscene", noArgs);
}

// closesetfile(): send the closing system messages, then drop the open set
// (TI.EXE builtin 0x2f01, core FUN_00430b20: FUN_00431050 first sends
// '"<scene>", closescene()' through the sendtoscene executor and then runs
// 'closeset()' in set scope, before FUN_00430ba0 releases the archive). The
// global closeset() calls putdownsound() which halts the room theme, and it
// switches on currentset(), so the messages must go out while the set is
// still current.
void SetRuntime::closeSetFile(DreamFactoryEngine &engine) {
	if (_set && _set->isOpen()) {
		Common::Array<Value> noArgs;
		Common::String openedName = _set->setName();
		if (_scene >= 0)
			engine.dispatchSceneMessage(static_cast<uint32>(_scene), "closescene", noArgs);
		if (_set && _set->setName() == openedName)
			engine.dispatchSetMessage("closeset", noArgs);
	}
	_set.reset();
	_scene = -1;
	_table = 0;
	_angle = 0;
	_view.clear();
	_transitionType = kSetTransitionNone;
	_transitionResource = 0;
	_transitionFrame = 0;
	_frameSequence.clear();
	_visible = false;
}

SetRuntime::Snapshot SetRuntime::snapshot() const {
	Snapshot state;
	if (_set && _set->isOpen()) {
		state.fileName = _set->name();
		state.setName = _set->setName();
		state.scene = _scene;
		if (_scene >= 0 && static_cast<uint32>(_scene) < _set->sceneCount())
			state.sceneName = _set->sceneName(static_cast<uint32>(_scene));
		state.table = _table;
		state.angle = _angle;
		state.view = _view;
		state.visible = _visible;
		state.transitionType = _transitionType;
		state.transitionResource = _transitionResource;
		state.transitionFrame = _transitionFrame;
	}
	return state;
}

void SetRuntime::restoreSnapshot(const Snapshot &snapshot, Common::ScopedPtr<Set> &preparedSet) {
	reset();
	_set.reset(preparedSet.release());
	if (!_set)
		return;

	_scene = snapshot.scene;
	if (_scene < 0 || static_cast<uint32>(_scene) >= _set->sceneCount())
		_scene = _set->findScene(snapshot.sceneName);
	_table = snapshot.table;
	if (_table < 0 || _table > 1)
		_table = 0;
	_angle = snapshot.angle;
	if (_scene >= 0) {
		uint32 angleCount = _set->angleCount(static_cast<uint32>(_scene), static_cast<uint32>(_table));
		if (angleCount && static_cast<uint32>(_angle) >= angleCount)
			_angle = 0;
	}
	_view = snapshot.view;
	_visible = snapshot.visible;
	_transitionType = snapshot.transitionType <= kSetTransitionForward ?
			snapshot.transitionType : kSetTransitionNone;
	_transitionResource = snapshot.transitionResource;
	_transitionFrame = snapshot.transitionFrame;
	updateLastCameraHeading();
}

// currentset(): the open set's EMBEDDED name (master header +0x070, e.g.
// 'bedsit1' -- no '.set'), or 'none' (TI.EXE builtin 0x4e55 returns the set
// record's name field, copied from the header by FUN_004307f0; setupsound,
// themetype and changeset all switch/compare on this form).
Common::String SetRuntime::currentSet() const {
	if (_set && _set->isOpen())
		return _set->setName();
	return "none";
}

// currentview(): DAT_004611dc in TI.EXE (FUN_00431ce0), or "Moving" while a
// panorama transition resource is active.
Common::String SetRuntime::getCurrentView() const {
	if (_transitionType != kSetTransitionNone)
		return "Moving";
	if (_set && _set->isOpen() && !_view.empty())
		return _view;
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
			debug(1, "DreamFactory: %s view '%s' not found in scene '%s', using nearest view '%s'",
					operation, requestedView.c_str(),
					runtime.set()->sceneName(static_cast<uint32>(sceneIdx)).c_str(),
					runtime.set()->viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx)).c_str());
	}
	if (viewIdx < 0) {
		if (!requestedView.empty())
			warning("DreamFactory: %s view '%s' not found in scene '%s'",
					operation, requestedView.c_str(),
					runtime.set()->sceneName(static_cast<uint32>(sceneIdx)).c_str());
		return false;
	}

	// currentview()/currentscene() are stable-view changes, so match the same
	// FUN_00433960/FUN_004425e0 table-B path used by opensetfile().
	int viewAngle = runtime.set()->angleForView(static_cast<uint32>(sceneIdx),
			Set::kNativeStablePanoramaTable, viewIdx);
	if (viewAngle < 0) {
		warning("DreamFactory: %s view '%s' has no panorama angle in scene '%s'",
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
Common::String SetRuntime::setCurrentView(DreamFactoryEngine &engine, const Common::String &target) {
	if (target.empty())
		return getCurrentView();
	if (!_set || !_set->isOpen() || _scene < 0)
		return getCurrentView();
	if (_transitionType != kSetTransitionNone)
		return getCurrentView();
	if (_view.equalsIgnoreCase(target))
		return getCurrentView();

	const int savedScene = _scene;
	int targetAngle = 0;
	Common::String targetView;
	if (!chooseSceneView(*this, savedScene, target, currentSetHeading(*this), false,
			targetAngle, targetView, "currentview"))
		return getCurrentView();
	if (!engine.closeCurrentSceneForNavigation())
		return getCurrentView();
	if (!_set || !_set->isOpen())
		return "none";

	renderSetScene(engine, savedScene, Set::kNativeStablePanoramaTable, targetAngle, targetView);
	Common::Array<Value> noArgs;
	engine.dispatchSceneMessage(static_cast<uint32>(savedScene), "openscene", noArgs);
	return getCurrentView();
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
		return packPoint(x, y);
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
	// that distance global to zero and no Titanic script writes it yet. The X
	// offset uses FUN_004432e0 (cos table, DAT_00486750) and the Y offset uses
	// FUN_00443310 (sin table, DAT_00486780); see calcVectX/Y for the table
	// assignment.
	const int kPlayerCameraDistance = 0;
	const int x = camera.cameraX -
			fixedShift14(nativeTrigCos(camera.heading) * kPlayerCameraDistance);
	const int y = camera.cameraY -
			fixedShift14(nativeTrigSin(camera.heading) * kPlayerCameraDistance);
	return selectXYZ(selector, x, y, camera.cameraZ);
}

// currentscene([arg]): no-arg reads DAT_004611cc. With "left"/"right"/"strait",
// BOOTFILE res2's keydown fallback reaches TI.EXE FUN_00430c70/FUN_00442140 to
// navigate the current set; other strings are scene names to switch to.
Common::String SetRuntime::getCurrentScene(DreamFactoryEngine &) const {
	if (!_set || !_set->isOpen() || _scene < 0)
		return "none";

	return _set->sceneName(static_cast<uint32>(_scene));
}

Common::String SetRuntime::setCurrentScene(DreamFactoryEngine &engine, const Common::String &target) {
	if (!_set || !_set->isOpen() || _scene < 0)
		return "none";

	if (!target.empty()) {
		if (target.equalsIgnoreCase("left") || target.equalsIgnoreCase("right") ||
				target.equalsIgnoreCase("strait")) {
			navigateSet(engine, target);
		} else {
			int sceneIdx = _set->findScene(target);
			if (sceneIdx >= 0) {
				if (!engine.closeCurrentSceneForNavigation())
					return getCurrentScene(engine);
				if (!_set || !_set->isOpen())
					return "none";
				int targetAngle = 0;
				Common::String targetView = _set->defaultView();
				int viewIdx = _set->findView(static_cast<uint32>(sceneIdx), targetView);
				int viewAngle = _set->angleForView(static_cast<uint32>(sceneIdx),
						Set::kNativeStablePanoramaTable, viewIdx);
				if (viewAngle >= 0)
					targetAngle = viewAngle;
				else
					targetView.clear();
				renderSetScene(engine, sceneIdx, Set::kNativeStablePanoramaTable, targetAngle, targetView);
				Common::Array<Value> noArgs;
				engine.dispatchSceneMessage(static_cast<uint32>(sceneIdx), "openscene", noArgs);
			} else {
				warning("DreamFactory: currentscene('%s'): no such scene", target.c_str());
			}
		}
	}

	return (_set && _set->isOpen() && _scene >= 0) ? getCurrentScene(engine) : Common::String("none");
}

int SetRuntime::countPaintings(const Common::String &scene, const Common::String &view) const {
	if (!_set || !_set->isOpen())
		return 0;
	int sceneIdx = _set->findScene(scene);
	return sceneIdx >= 0 ? static_cast<int>(_set->paintingCount(static_cast<uint32>(sceneIdx), view)) : 0;
}

Common::String SetRuntime::indexToPainting(const Common::String &scene,
		const Common::String &view, int index) const {
	if (!_set || !_set->isOpen() || index < 1)
		return Common::String();
	int sceneIdx = _set->findScene(scene);
	return sceneIdx >= 0 ? _set->indexToPainting(static_cast<uint32>(sceneIdx), view, static_cast<uint32>(index)) :
			Common::String();
}

bool SetRuntime::roadAhead(const Common::String &scene, const Common::String &view) const {
	if (!_set || !_set->isOpen())
		return false;
	int sceneIdx = _set->findScene(scene);
	if (sceneIdx < 0)
		return false;
	int viewIdx = _set->findView(static_cast<uint32>(sceneIdx), view);
	return _set->forwardTransitionForView(static_cast<uint32>(sceneIdx), viewIdx) != 0;
}

bool SetRuntime::getSetVisible(DreamFactoryEngine &) const {
	if (!_set || !_set->isOpen())
		return false;
	return _visible;
}

bool SetRuntime::setSetVisible(DreamFactoryEngine &engine, bool newVisible) {
	if (!_set || !_set->isOpen())
		return false;
	bool wasVisible = _visible;
	_visible = newVisible;
	if (_visible) {
		if (!wasVisible && _scene >= 0)
			renderSetScene(engine, _scene, _table, _angle, _view);
		else
			engine.propRuntime().setDirty(true);
	} else {
		_transitionType = kSetTransitionNone;
		engine.propRuntime().setDirty(false);
		if (engine.stageRuntime().stage() && engine.stageRuntime().stage()->isOpen())
			engine.stageRuntime().renderStageNode(engine, engine.stageRuntime().node(), false);
	}
	return _visible;
}

} // End of namespace DreamFactory
