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
#include "cyberflix/set.h"
#include "cyberflix/stage.h"

namespace Cyberflix {

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
				_propRuntime.setDirty(true);
		} else {
			_setTransitionType = kSetTransitionNone;
			_propRuntime.setDirty(false);
			if (_stageRuntime.stage() && _stageRuntime.stage()->isOpen())
				renderStageNode(_stageRuntime.node(), false);
		}
	}
	return _setVisible;
}

} // End of namespace Cyberflix
