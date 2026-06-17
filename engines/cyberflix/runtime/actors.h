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

#ifndef CYBERFLIX_RUNTIME_ACTORS_H
#define CYBERFLIX_RUNTIME_ACTORS_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/str.h"

#include "cyberflix/cast.h"

namespace Cyberflix {

class CyberflixEngine;

class ActorRuntime {
public:
	struct ActorRef {
		Common::SharedPtr<Cast> cast;
		Common::SharedPtr<Cast::Actor> actor;
	};

	Common::SharedPtr<Cast> findCastShared(const Common::String &name) const;
	ActorRef findActorRef(const Common::String &name) const;
	bool resolveActorStar(CyberflixEngine &engine, Cast::Actor &actor);
	void refreshActorStarPositions(CyberflixEngine &engine);

	Common::Array<Common::SharedPtr<Cast> > &casts() { return _casts; }
	const Common::Array<Common::SharedPtr<Cast> > &casts() const { return _casts; }

private:
	/** Open cast files and the global actor list they contribute. */
	Common::Array<Common::SharedPtr<Cast> > _casts;
};

} // End of namespace Cyberflix

#endif
