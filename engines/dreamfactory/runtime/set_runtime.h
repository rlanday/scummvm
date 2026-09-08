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

#ifndef DREAMFACTORY_RUNTIME_SET_RUNTIME_H
#define DREAMFACTORY_RUNTIME_SET_RUNTIME_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/str.h"

#include "dreamfactory/image.h"
#include "dreamfactory/set.h"

namespace DreamFactory {

class DreamFactoryEngine;
struct Value;

enum SetTransitionType {
	kSetTransitionNone,
	kSetTransitionTurn,
	kSetTransitionForward
};

class SetRuntime {
public:
	struct Snapshot {
		Common::String fileName;
		Common::String setName;
		int32 scene = -1;
		Common::String sceneName;
		int32 table = 0;
		int32 angle = 0;
		Common::String view;
		bool visible = false;
		SetTransitionType transitionType = kSetTransitionNone;
		uint32 transitionResource = 0;
		uint32 transitionFrame = 0;
	};

	const Common::ScopedPtr<Set> &set() const { return _set; }
	int scene() const { return _scene; }
	int table() const { return _table; }
	int angle() const { return _angle; }
	const Common::String &view() const { return _view; }
	SetTransitionType transitionType() const { return _transitionType; }
	uint32 transitionResource() const { return _transitionResource; }
	uint32 transitionFrame() const { return _transitionFrame; }
	const FrameSequence &frameSequence() const { return _frameSequence; }
	/** Decode the current scene into the retained background without presenting it. */
	bool decodeCurrentSceneFrame(FrameImage &frame);
	bool visible() const { return _visible; }
	bool screenUpdatePending() const { return _screenUpdatePending; }
	/** A replacement compositor has already presented the pending frame. */
	void acknowledgeScreenUpdate() { _screenUpdatePending = false; }

	void openSetFile(DreamFactoryEngine &engine, const Common::String &name,
			const Common::String &scene = Common::String(),
			const Common::String &view = Common::String());
	void closeSetFile(DreamFactoryEngine &engine);
	Common::String currentSet() const;
	Common::String getCurrentView() const;
	Common::String setCurrentView(DreamFactoryEngine &engine, const Common::String &target);
	int currentDeg() const;
	int cameraXYZ(int selector) const;
	int playerXYZ(int selector) const;
	Common::String getCurrentScene(DreamFactoryEngine &engine) const;
	Common::String setCurrentScene(DreamFactoryEngine &engine, const Common::String &target);
	bool getSetVisible(DreamFactoryEngine &engine) const;
	bool setSetVisible(DreamFactoryEngine &engine, bool visible);

	void sendToScene(DreamFactoryEngine &engine, const Common::String &scene,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToSceneFx(DreamFactoryEngine &engine, const Common::String &scene,
			const Common::String &message, const Common::Array<Value> &args);
	void sendToPainting(DreamFactoryEngine &engine, const Common::String &scene,
			const Common::String &view, const Common::String &painting,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToPaintingFx(DreamFactoryEngine &engine, const Common::String &scene,
			const Common::String &view, const Common::String &painting,
			const Common::String &message, const Common::Array<Value> &args);
	int countPaintings(const Common::String &scene, const Common::String &view) const;
	Common::String indexToPainting(const Common::String &scene,
			const Common::String &view, int index) const;
	bool roadAhead(const Common::String &scene, const Common::String &view) const;

	void navigateSet(DreamFactoryEngine &engine, const Common::String &action);
	void advanceSetTransition(DreamFactoryEngine &engine);
	bool stableSettlePending() const { return _stableSettlePending; }
	bool settleStableSetView(DreamFactoryEngine &engine);
	void updateLastCameraHeading();
	void renderSetScene(DreamFactoryEngine &engine, int scene, int table, int angle,
			const Common::String &view = Common::String());
	void displaySetFrame(DreamFactoryEngine &engine, const FrameImage &frame);
	void displaySetFrame(DreamFactoryEngine &engine, const FrameSequence &frame);
	void displaySetFramePixels(DreamFactoryEngine &engine, const byte *pixels, uint16 width, uint16 height,
			const FrameSequence *depthFrame = nullptr);
	bool presentPendingScreenUpdate(DreamFactoryEngine &engine);
	Snapshot snapshot() const;
	/** Apply saved state using a resource opened successfully during load preparation. */
	void restoreSnapshot(const Snapshot &snapshot, Common::ScopedPtr<Set> &preparedSet);

	void clearNavigation() {
		_scene = -1;
		_table = 0;
		_angle = 0;
		_view.clear();
		_transitionType = kSetTransitionNone;
		_transitionResource = 0;
		_transitionFrame = 0;
		_frameSequence.clear();
		_stableSettlePending = false;
	}

	void reset() {
		_set.reset();
		clearNavigation();
		_lastCameraHeading = 0;
		_visible = false;
		_screenUpdatePending = false;
	}

private:
	Common::ScopedPtr<Set> _set;
	int _scene = -1;
	int _table = 0;
	int _angle = 0;
	Common::String _view;
	SetTransitionType _transitionType = kSetTransitionNone;
	uint32 _transitionResource = 0;
	uint32 _transitionFrame = 0;
	FrameSequence _frameSequence;
	bool _stableSettlePending = false;
	int _lastCameraHeading = 0;
	bool _visible = false;
	bool _screenUpdatePending = false;
};

} // End of namespace DreamFactory

#endif
