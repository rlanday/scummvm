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
		} else if (set()->cameraData((uint32)this->scene(), (uint32)table(),
				(uint32)angle(), cameraData)) {
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
	engine.refreshActorStarPositions();

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
		Common::String actualScene = set()->sceneName((uint32)sceneIdx);
		// View select (TI.EXE FUN_00433960 stores the view, FUN_004425e0 aims
		// the camera at the panorama record tagged with the view's index).
		int angle = 0;
		Common::String activeView;
		if (!useView.empty()) {
			int viewIdx = set()->findView((uint32)sceneIdx, useView);
			if (viewIdx < 0) {
				// FUN_00433960 preserves the requested view name, but if the
				// selected scene lacks it, it chooses the view whose authored
				// heading is closest to the previous camera heading before
				// calling FUN_004425e0.
				viewIdx = set()->nearestViewForHeading((uint32)sceneIdx, previousHeading);
				if (viewIdx >= 0)
					debug(1, "Cyberflix: opensetfile view '%s' not found in scene '%s', using nearest view '%s'",
							useView.c_str(), actualScene.c_str(),
							set()->viewName((uint32)sceneIdx, (uint32)viewIdx).c_str());
			}
			int viewAngle = set()->angleForView((uint32)sceneIdx, 0, viewIdx);
			if (viewAngle >= 0) {
				angle = viewAngle;
				activeView = set()->viewName((uint32)sceneIdx, (uint32)viewIdx);
			} else {
				warning("Cyberflix: opensetfile view '%s' not found in scene '%s'",
						useView.c_str(), actualScene.c_str());
			}
		}
		renderSetScene(engine, sceneIdx, 0, angle, activeView);
		engine.dispatchSceneMessage((uint32)sceneIdx, "openscene", noArgs);
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
			engine.dispatchSceneMessage((uint32)scene(), "closescene", noArgs);
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
				int targetAngle = 0;
				Common::String targetView = set()->defaultView();
				int viewIdx = set()->findView((uint32)sceneIdx, targetView);
				int viewAngle = set()->angleForView((uint32)sceneIdx, 0, viewIdx);
				if (viewAngle >= 0)
					targetAngle = viewAngle;
				else
					targetView.clear();
				renderSetScene(engine, sceneIdx, 0, targetAngle, targetView);
			} else {
				warning("Cyberflix: currentscene('%s'): no such scene", target->c_str());
			}
		}
	}

	return (set() && set()->isOpen() && scene() >= 0) ?
			set()->sceneName((uint32)scene()) : Common::String("none");
}

int SetRuntime::countPaintings(const Common::String &scene, const Common::String &view) const {
	if (!set() || !set()->isOpen())
		return 0;
	int sceneIdx = set()->findScene(scene);
	return sceneIdx >= 0 ? (int)set()->paintingCount((uint32)sceneIdx, view) : 0;
}

Common::String SetRuntime::indexToPainting(const Common::String &scene,
		const Common::String &view, int index) const {
	if (!set() || !set()->isOpen() || index < 1)
		return Common::String();
	int sceneIdx = set()->findScene(scene);
	return sceneIdx >= 0 ? set()->indexToPainting((uint32)sceneIdx, view, (uint32)index) :
			Common::String();
}

bool SetRuntime::roadAhead(const Common::String &scene, const Common::String &view) const {
	if (!set() || !set()->isOpen())
		return false;
	int sceneIdx = set()->findScene(scene);
	if (sceneIdx < 0)
		return false;
	int viewIdx = set()->findView((uint32)sceneIdx, view);
	return set()->forwardTransitionForView((uint32)sceneIdx, viewIdx) != 0;
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
