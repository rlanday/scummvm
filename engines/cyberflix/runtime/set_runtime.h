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

#ifndef CYBERFLIX_RUNTIME_SET_RUNTIME_H
#define CYBERFLIX_RUNTIME_SET_RUNTIME_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/str.h"

#include "cyberflix/image.h"
#include "cyberflix/set.h"

namespace Cyberflix {

class CyberflixEngine;
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

	Common::ScopedPtr<Set> &set() { return _set; }
	const Common::ScopedPtr<Set> &set() const { return _set; }
	int &scene() { return _scene; }
	int scene() const { return _scene; }
	int &table() { return _table; }
	int table() const { return _table; }
	int &angle() { return _angle; }
	int angle() const { return _angle; }
	Common::String &view() { return _view; }
	const Common::String &view() const { return _view; }
	SetTransitionType &transitionType() { return _transitionType; }
	SetTransitionType transitionType() const { return _transitionType; }
	uint32 &transitionResource() { return _transitionResource; }
	uint32 transitionResource() const { return _transitionResource; }
	uint32 &transitionFrame() { return _transitionFrame; }
	uint32 transitionFrame() const { return _transitionFrame; }
	FrameSequence &frameSequence() { return _frameSequence; }
	const FrameSequence &frameSequence() const { return _frameSequence; }
	bool &visible() { return _visible; }
	bool visible() const { return _visible; }
	bool &screenUpdatePending() { return _screenUpdatePending; }
	bool screenUpdatePending() const { return _screenUpdatePending; }

	void openSetFile(CyberflixEngine &engine, const Common::String &name,
			const Common::String &scene = Common::String(),
			const Common::String &view = Common::String());
	void closeSetFile(CyberflixEngine &engine);
	Common::String currentSet() const;
	Common::String getCurrentView() const;
	Common::String setCurrentView(CyberflixEngine &engine, const Common::String &target);
	int currentDeg() const;
	int cameraXYZ(int selector) const;
	int playerXYZ(int selector) const;
	Common::String getCurrentScene(CyberflixEngine &engine) const;
	Common::String setCurrentScene(CyberflixEngine &engine, const Common::String &target);
	bool getSetVisible(CyberflixEngine &engine) const;
	bool setSetVisible(CyberflixEngine &engine, bool visible);

	void sendToScene(CyberflixEngine &engine, const Common::String &scene,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToSceneFx(CyberflixEngine &engine, const Common::String &scene,
			const Common::String &message, const Common::Array<Value> &args);
	void sendToPainting(CyberflixEngine &engine, const Common::String &scene,
			const Common::String &view, const Common::String &painting,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToPaintingFx(CyberflixEngine &engine, const Common::String &scene,
			const Common::String &view, const Common::String &painting,
			const Common::String &message, const Common::Array<Value> &args);
	int countPaintings(const Common::String &scene, const Common::String &view) const;
	Common::String indexToPainting(const Common::String &scene,
			const Common::String &view, int index) const;
	bool roadAhead(const Common::String &scene, const Common::String &view) const;

	void navigateSet(CyberflixEngine &engine, const Common::String &action);
	void advanceSetTransition(CyberflixEngine &engine);
	bool stableSettlePending() const { return _stableSettlePending; }
	bool settleStableSetView(CyberflixEngine &engine);
	void renderSetScene(CyberflixEngine &engine, int scene, int table, int angle,
			const Common::String &view = Common::String());
	void displaySetFrame(CyberflixEngine &engine, const FrameImage &frame);
	void displaySetFrame(CyberflixEngine &engine, const FrameSequence &frame);
	void displaySetFramePixels(CyberflixEngine &engine, const byte *pixels, uint16 width, uint16 height,
			const FrameSequence *depthFrame = nullptr);
	bool presentPendingScreenUpdate(CyberflixEngine &engine);
	Snapshot snapshot() const;
	bool restoreSnapshot(const Snapshot &snapshot);

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
	bool _visible = false;
	bool _screenUpdatePending = false;
};

} // End of namespace Cyberflix

#endif
