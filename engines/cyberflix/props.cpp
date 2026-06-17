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

#include "cyberflix/cyberflix.h"

namespace Cyberflix {

// ---- Shop/prop subsystem (TI.EXE FUN_00428450 and friends) ----------------
// RE notes: files/renderer-notes.md "Shop/prop subsystem". The original keeps
// one global prop array across all open shops; here the by-name lookups and
// countprops/indextoprop span _shops in open order, which preserves the
// global-index semantics (shops are only ever appended).

Shop *CyberflixEngine::findShop(const Common::String &name) {
	Common::SharedPtr<Shop> shop = findShopShared(name);
	return shop.get();
}

Common::SharedPtr<Shop> CyberflixEngine::findShopShared(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _shops.size(); ++i)
		if (_shops[i]->name() == key)
			return _shops[i];
	return Common::SharedPtr<Shop>();
}

Shop::Prop *CyberflixEngine::findProp(const Common::String &name, Shop **shopOut) {
	Shop::Prop *prop = nullptr;
	Common::SharedPtr<Shop> shop = findPropOwnerShared(name, &prop);
	if (shopOut)
		*shopOut = shop.get();
	return prop;
}

Common::SharedPtr<Shop> CyberflixEngine::findPropOwnerShared(const Common::String &name,
		Shop::Prop **propOut) {
	for (uint32 i = 0; i < _shops.size(); ++i) {
		Shop::Prop *prop = _shops[i]->findProp(name);
		if (prop) {
			if (propOut)
				*propOut = prop;
			return _shops[i];
		}
	}
	if (propOut)
		*propOut = nullptr;
	return Common::SharedPtr<Shop>();
}

} // End of namespace Cyberflix
