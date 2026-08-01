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

#ifndef CYBERFLIX_RUNTIME_SET_HELPERS_H
#define CYBERFLIX_RUNTIME_SET_HELPERS_H

#include "cyberflix/runtime/set_runtime.h"
#include "cyberflix/set.h"
#include "cyberflix/shop.h"

namespace Cyberflix {

/** Depth returned for world items that are off-camera/unprojectable — the
 *  native far sentinel (actors and props share it). */
enum { kOffCameraDepth = 32000 };

/** Native instance-name record field width (actorinstance/propinstance cap). */
enum { kMaxInstanceNameLength = 15 };

inline Shop::WorldCamera makeWorldCamera(const Set::CameraData &camera) {
	Shop::WorldCamera out;
	out.heading = camera.heading;
	out.cameraX = camera.cameraX;
	out.cameraY = camera.cameraY;
	out.cameraZ = camera.cameraZ;
	out.baseZ = camera.baseZ;
	out.nearPlane = camera.nearPlane;
	out.farPlane = camera.farPlane;
	out.viewportLeft = camera.viewportLeft;
	out.viewportTop = camera.viewportTop;
	out.viewportRight = camera.viewportRight;
	out.viewportBottom = camera.viewportBottom;
	out.centerX = camera.centerX;
	out.centerY = camera.centerY;
	out.focal = camera.focal;
	return out;
}

struct CurrentWorldCameraResult {
	bool valid = false;
	Shop::WorldCamera camera;
};

/**
 * Resolve the world camera that world-space props/actors project through right
 * now: the transition camera while a forward transition is animating, else the
 * current scene/table/angle camera. Invalid when no set is open or the SET has
 * no camera record for the current state.
 */
inline CurrentWorldCameraResult currentWorldCamera(SetRuntime &setRuntime) {
	CurrentWorldCameraResult result;
	if (!setRuntime.set() || !setRuntime.set()->isOpen())
		return result;

	Set::CameraData cameraData;
	if (setRuntime.transitionType() == kSetTransitionForward) {
		result.valid = setRuntime.set()->transitionCameraData(
				setRuntime.transitionResource(), setRuntime.transitionFrame(), cameraData);
	} else if (setRuntime.scene() >= 0) {
		result.valid = setRuntime.set()->cameraData(
				static_cast<uint32>(setRuntime.scene()),
				static_cast<uint32>(setRuntime.table()),
				static_cast<uint32>(setRuntime.angle()), cameraData);
	}
	if (result.valid)
		result.camera = makeWorldCamera(cameraData);
	return result;
}

} // End of namespace Cyberflix

#endif
