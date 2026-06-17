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
#include "cyberflix/set.h"

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

void CyberflixEngine::collectScreenProps(Common::Array<const Shop::Prop *> &draw,
		Common::Array<const Shop *> &drawShop) {
	for (uint32 s = 0; s < _shops.size(); ++s) {
		// Native FUN_0042ba40 walks the global SHOP prop array; replacement
		// stages rely on scripts to hide stale props and re-show live overlays.
		for (uint32 i = 0; i < _shops[s]->propCount(); ++i) {
			const Shop::Prop &p = _shops[s]->prop(i);
			if (p.visible && p.mode == 0) {
				draw.push_back(&p);
				drawShop.push_back(_shops[s].get());
			}
		}
	}
	// Native FUN_004434f0 sorts larger signed depths first, then hittest walks
	// the display list backward. This leaves more-negative screen props on top.
	for (uint32 i = 1; i < draw.size(); ++i) {
		const Shop::Prop *p = draw[i];
		const Shop *sh = drawShop[i];
		uint32 j = i;
		for (; j > 0 && draw[j - 1]->depth < p->depth; --j) {
			draw[j] = draw[j - 1];
			drawShop[j] = drawShop[j - 1];
		}
		draw[j] = p;
		drawShop[j] = sh;
	}
}

void CyberflixEngine::advancePropPoses() {
	for (uint32 i = 0; i < _shops.size(); ++i)
		_shops[i]->advancePropPoses();
}

bool CyberflixEngine::hasAnimatedScreenProps() const {
	for (uint32 s = 0; s < _shops.size(); ++s) {
		for (uint32 i = 0; i < _shops[s]->propCount(); ++i) {
			const Shop::Prop &p = _shops[s]->prop(i);
			if (p.visible && p.mode == 0 && p.poseCount > 1)
				return true;
		}
	}
	return false;
}

void CyberflixEngine::collectWorldProps(Common::Array<const Shop::Prop *> &draw,
		Common::Array<const Shop *> &drawShop, Common::Array<int16> &depths,
		const Shop::WorldCamera &camera) {
	if (!_set || !_set->isOpen())
		return;
	const Common::String &setName = _set->setName();
	for (uint32 s = 0; s < _shops.size(); ++s) {
		for (uint32 i = 0; i < _shops[s]->propCount(); ++i) {
			const Shop::Prop &p = _shops[s]->prop(i);
			if (!p.visible || p.mode == 0 || !p.setName.equalsIgnoreCase(setName))
				continue;
			Common::SharedPtr<CelImage> cel;
			Common::Rect rect;
			int16 depth = 0;
			if (!_shops[s]->renderWorldProp(p, camera, setName, cel, rect, depth))
				continue;
			draw.push_back(&p);
			drawShop.push_back(_shops[s].get());
			depths.push_back(depth);
		}
	}

	for (uint32 i = 1; i < draw.size(); ++i) {
		const Shop::Prop *p = draw[i];
		const Shop *sh = drawShop[i];
		int16 depth = depths[i];
		uint32 j = i;
		for (; j > 0 && depths[j - 1] < depth; --j) {
			draw[j] = draw[j - 1];
			drawShop[j] = drawShop[j - 1];
			depths[j] = depths[j - 1];
		}
		draw[j] = p;
		drawShop[j] = sh;
		depths[j] = depth;
	}
}

bool CyberflixEngine::screenPropRect(const Shop &shop, const Shop::Prop &prop, Common::Rect &rect) const {
	if (!prop.visible || prop.mode != 0)
		return false;

	Common::SharedPtr<CelImage> cel;
	if (!shop.renderProp(prop, cel, rect))
		return false;
	rect.clip(Common::Rect(kScreenWidth, kScreenHeight));
	return !rect.isEmpty();
}

void CyberflixEngine::queueDirtyRect(const Common::Rect &rect) {
	Common::Rect clipped = rect;
	clipped.clip(Common::Rect(kScreenWidth, kScreenHeight));
	if (clipped.isEmpty())
		return;

	for (uint32 i = 0; i < _dirtyRects.size(); ++i) {
		if (_dirtyRects[i].intersects(clipped)) {
			_dirtyRects[i].extend(clipped);
			return;
		}
	}
	_dirtyRects.push_back(clipped);
}

void CyberflixEngine::markPropDirty(const Shop &shop, const Shop::Prop &prop, const Common::Rect *oldRect) {
	if (oldRect)
		queueDirtyRect(*oldRect);
	Common::Rect newRect;
	if (screenPropRect(shop, prop, newRect))
		queueDirtyRect(newRect);
	_propsDirty = true;
}

void CyberflixEngine::markShopDirty(const Shop &shop) {
	for (uint32 i = 0; i < shop.propCount(); ++i) {
		Common::Rect rect;
		if (screenPropRect(shop, shop.prop(i), rect))
			queueDirtyRect(rect);
	}
	_propsDirty = true;
}

} // End of namespace Cyberflix
