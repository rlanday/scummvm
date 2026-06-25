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

#ifndef CYBERFLIX_RUNTIME_PROPS_H
#define CYBERFLIX_RUNTIME_PROPS_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/rect.h"
#include "common/str.h"

#include "cyberflix/shop.h"
#include "cyberflix/vm.h"

namespace Cyberflix {

class CyberflixEngine;

class PropRuntime {
public:
	Shop *findShop(const Common::String &name);
	Common::SharedPtr<Shop> findShopShared(const Common::String &name);
	Shop::Prop *findProp(const Common::String &name, Shop **shopOut = nullptr);
	Common::SharedPtr<Shop> findPropOwnerShared(const Common::String &name, Shop::Prop **propOut);

	void collectScreenProps(Common::Array<const Shop::Prop *> &draw,
			Common::Array<const Shop *> &drawShop) const;
	void advancePropPoses();
	bool hasAnimatedScreenProps() const;
	void collectWorldProps(CyberflixEngine &engine, Common::Array<const Shop::Prop *> &draw,
			Common::Array<const Shop *> &drawShop, Common::Array<int16> &depths,
			const Shop::WorldCamera &camera) const;
	bool screenPropRect(const Shop &shop, const Shop::Prop &prop, Common::Rect &rect) const;
	void queueDirtyRect(const Common::Rect &rect);
	void markPropDirty(const Shop &shop, const Shop::Prop &prop, const Common::Rect *oldRect);
	void markShopDirty(const Shop &shop);

	void openShopFile(CyberflixEngine &engine, const Common::String &name);
	void closeShopFile(CyberflixEngine &engine, const Common::String &name);
	void propInstance(const Common::String &source, const Common::String &newName);
	void sendToShop(CyberflixEngine &engine, const Common::String &shopName,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToShopFx(CyberflixEngine &engine, const Common::String &shopName,
			const Common::String &message, const Common::Array<Value> &args);
	void sendToProp(CyberflixEngine &engine, const Common::String &propName,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToPropFx(CyberflixEngine &engine, const Common::String &propName,
			const Common::String &message, const Common::Array<Value> &args);

	bool propVisible(const Common::String &name);
	void propVisible(const Common::String &name, bool visible);
	Common::String propView(const Common::String &name);
	void propView(const Common::String &name, const Common::String &shape);
	int propXY(const Common::String &name, int selector);
	void setPropXY(const Common::String &name, int x, int y);
	void propSet(const Common::String &name, const Common::String &setName);
	void propXYZ(const Common::String &name, int x, int y, int z);
	void propScale(const Common::String &name, int scale);
	void propZClip(const Common::String &name, int dist);
	void propDist(const Common::String &name, int dist);
	int getPropDeg(const Common::String &name);
	int setPropDeg(const Common::String &name, int newDeg);
	Common::String getPropOwner(const Common::String &name);
	Common::String setPropOwner(const Common::String &name, const Common::String &newOwner);
	int getPropValue(const Common::String &name);
	int setPropValue(const Common::String &name, int newValue);
	int countProps() const;
	Common::String indexToProp(int index) const;
	bool pointInProp(const Common::String &name, int32 packedPoint);
	void refreshPropsIfDirty(CyberflixEngine &engine);

	Common::Array<Common::SharedPtr<Shop> > &shops() { return _shops; }
	const Common::Array<Common::SharedPtr<Shop> > &shops() const { return _shops; }
	void clear() { _shops.clear(); _propsDirty = false; _dirtyRects.clear(); }
	bool dirty() const { return _propsDirty; }
	void setDirty(bool dirty) { _propsDirty = dirty; }
	Common::Array<Common::Rect> &dirtyRects() { return _dirtyRects; }
	const Common::Array<Common::Rect> &dirtyRects() const { return _dirtyRects; }
	void clearDirtyRects() { _dirtyRects.clear(); }

private:
	/**
	 * Open shops (DATA/ .SHP files), in openshopfile order. The original keeps
	 * ONE global prop array across all shops (DAT_0046113c/DAT_00461140), so
	 * countprops/indextoprop and the by-name prop lookups span every open shop
	 * here, in open order.
	 */
	Common::Array<Common::SharedPtr<Shop> > _shops;
	bool _propsDirty = false;
	Common::Array<Common::Rect> _dirtyRects;
};

} // End of namespace Cyberflix

#endif
