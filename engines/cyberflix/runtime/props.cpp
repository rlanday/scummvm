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
#include "cyberflix/runtime/set_helpers.h"

namespace Cyberflix {

static bool isReplacementStageForProps(const Common::SharedPtr<Stage> &stage) {
	return stage && stage->isOpen() && !stage->name().equalsIgnoreCase("main.stg");
}

// ---- Shop/prop subsystem (TI.EXE FUN_00428450 and friends) ----------------
// The original keeps
// one global prop array across all open shops; here the by-name lookups and
// countprops/indextoprop span _shops in open order, which preserves the
// global-index semantics (shops are only ever appended).

Shop *PropRuntime::findShop(const Common::String &name) {
	Common::SharedPtr<Shop> shop = findShopShared(name);
	return shop.get();
}

Common::SharedPtr<Shop> PropRuntime::findShopShared(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _shops.size(); ++i)
		if (_shops[i]->name() == key)
			return _shops[i];
	return Common::SharedPtr<Shop>();
}

Shop::Prop *PropRuntime::findProp(const Common::String &name, Shop **shopOut) {
	Shop::Prop *prop = nullptr;
	Common::SharedPtr<Shop> shop = findPropOwnerShared(name, &prop);
	if (shopOut)
		*shopOut = shop.get();
	return prop;
}

Common::SharedPtr<Shop> PropRuntime::findPropOwnerShared(const Common::String &name,
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

void PropRuntime::collectScreenProps(Common::Array<const Shop::Prop *> &draw,
		Common::Array<const Shop *> &drawShop) const {
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

void PropRuntime::advancePropPoses() {
	for (uint32 i = 0; i < _shops.size(); ++i)
		_shops[i]->advancePropPoses();
}

void PropRuntime::advancePropPosesAndMarkDirtyRects() {
	Common::Array<Common::Rect> oldRects;
	Common::Array<bool> hadOldRects;
	Common::Array<const Shop::Prop *> props;
	Common::Array<const Shop *> shops;
	for (uint32 s = 0; s < _shops.size(); ++s) {
		for (uint32 i = 0; i < _shops[s]->propCount(); ++i) {
			const Shop::Prop &prop = _shops[s]->prop(i);
			if (!prop.visible || prop.mode != 0 || prop.poseCount <= 1)
				continue;
			Common::Rect oldRect;
			bool hadOldRect = screenPropRect(*_shops[s], prop, oldRect);
			oldRects.push_back(oldRect);
			hadOldRects.push_back(hadOldRect);
			props.push_back(&prop);
			shops.push_back(_shops[s].get());
		}
	}

	advancePropPoses();

	for (uint32 i = 0; i < oldRects.size(); ++i) {
		if (hadOldRects[i])
			queueDirtyRect(oldRects[i]);
		Common::Rect newRect;
		if (screenPropRect(*shops[i], *props[i], newRect))
			queueDirtyRect(newRect);
	}
}

bool PropRuntime::hasAnimatedScreenProps() const {
	for (uint32 s = 0; s < _shops.size(); ++s) {
		for (uint32 i = 0; i < _shops[s]->propCount(); ++i) {
			const Shop::Prop &p = _shops[s]->prop(i);
			if (p.visible && p.mode == 0 && p.poseCount > 1)
				return true;
		}
	}
	return false;
}

void PropRuntime::collectWorldProps(CyberflixEngine &engine, Common::Array<const Shop::Prop *> &draw,
		Common::Array<const Shop *> &drawShop, Common::Array<int16> &depths,
		const Shop::WorldCamera &camera) const {
	if (!engine._setRuntime.set() || !engine._setRuntime.set()->isOpen())
		return;
	const Common::String &setName = engine._setRuntime.set()->setName();
	for (uint32 s = 0; s < _shops.size(); ++s) {
		for (uint32 i = 0; i < _shops[s]->propCount(); ++i) {
			const Shop::Prop &p = _shops[s]->prop(i);
			if (!p.visible || p.mode == 0 || !p.setName.equalsIgnoreCase(setName))
				continue;
			Shop::PropRenderResult rendered = _shops[s]->renderWorldProp(p, camera, setName);
			if (!rendered.valid)
				continue;
			draw.push_back(&p);
			drawShop.push_back(_shops[s].get());
			depths.push_back(rendered.depth);
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

bool PropRuntime::screenPropRect(const Shop &shop, const Shop::Prop &prop, Common::Rect &rect) const {
	if (!prop.visible || prop.mode != 0)
		return false;

	Shop::PropRenderResult rendered = shop.renderProp(prop);
	if (!rendered.valid)
		return false;
	rect = rendered.rect;
	rect.clip(Common::Rect(kScreenWidth, kScreenHeight));
	return !rect.isEmpty();
}

void PropRuntime::queueDirtyRect(const Common::Rect &rect) {
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

void PropRuntime::markPropDirty(const Shop &shop, const Shop::Prop &prop, const Common::Rect *oldRect) {
	if (oldRect)
		queueDirtyRect(*oldRect);
	Common::Rect newRect;
	if (screenPropRect(shop, prop, newRect))
		queueDirtyRect(newRect);
	_propsDirty = true;
}

void PropRuntime::markShopDirty(const Shop &shop) {
	for (uint32 i = 0; i < shop.propCount(); ++i) {
		Common::Rect rect;
		if (screenPropRect(shop, shop.prop(i), rect))
			queueDirtyRect(rect);
	}
	_propsDirty = true;
}

void PropRuntime::openShopFile(CyberflixEngine &engine, const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	if (findShop(key)) {
		debug(1, "Cyberflix: shop '%s' already open", key.c_str());
		return;
	}

	Common::SharedPtr<Shop> shop(new Shop());
	if (!shop->open(key))
		return;
	_shops.push_back(shop);

	// Post-parse dispatch (FUN_0042a680): sendtoshop("<shop>", openshop())
	// then, for each prop of THIS shop, sendtoprop("<prop>", openprop())
	// (dispatch strings 0x457ec8 / 0x457eb8).
	engine.dispatchWithScopes(shop->shopScript(), nullptr, key, Common::String(),
			"openshop", Common::Array<Value>());
	for (uint32 i = 0; i < shop->propCount(); ++i) {
		Shop::Prop &prop = shop->prop(i);
		engine.dispatchWithScopes(prop.script.get(), shop->shopScript(), prop.name, prop.name,
				"openprop", Common::Array<Value>());
	}
	refreshPropsIfDirty(engine);
}

void PropRuntime::closeShopFile(CyberflixEngine &engine, const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint32 i = 0; i < _shops.size(); ++i) {
		if (_shops[i]->name() == key) {
			debug(1, "Cyberflix: shop '%s' closed", key.c_str());
			// Native closeshopfile (FUN_0042a7e0) tears down each prop through
			// FUN_0042aa00 before deleting the shop's prop records. That helper
			// stops makeloop("prop", <prop>, ...) callbacks via FUN_00423bf0.
			// PHOTO.SHP first exposed this: photobag schedules delayed open/close
			// animation callbacks, and a stale callback after closephoto() tried
			// to dispatch to a prop that no longer existed.
			for (uint32 p = 0; p < _shops[i]->propCount(); ++p)
				engine._loopRuntime.stopLoop("prop", _shops[i]->prop(p).name);
			markShopDirty(*_shops[i]);
			_shops.remove_at(i);
			refreshPropsIfDirty(engine);
			return;
		}
	}
	debug(1, "Cyberflix: closeshopfile('%s'): shop not open", key.c_str());
}

void PropRuntime::propInstance(const Common::String &source, const Common::String &newName) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(source, &shop);
	if (!prop || !shop) {
		warning("Cyberflix: propinstance('%s', '%s'): source prop not found",
				source.c_str(), newName.c_str());
		return;
	}
	if (newName.size() > 15) {
		warning("Cyberflix: propinstance('%s', '%s'): name too long",
				source.c_str(), newName.c_str());
		return;
	}
	if (findProp(newName))
		return;

	if (shop->addPropInstance(*prop, newName)) {
		debug(1, "Cyberflix: propinstance('%s', '%s')", source.c_str(), newName.c_str());
		_propsDirty = true;
	}
}

void PropRuntime::sendToShop(CyberflixEngine &engine, const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoshop('%s') -> %s(%u args)", shopName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Shop> shop = findShopShared(shopName);
	if (!shop) {
		warning("Cyberflix: sendtoshop('%s'): shop not open", shopName.c_str());
		return;
	}
	engine.dispatchWithScopes(shop->shopScript(), nullptr, shop->name(), Common::String(),
			message, args);
	refreshPropsIfDirty(engine);
}

Value PropRuntime::sendToShopFx(CyberflixEngine &engine, const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoshopfx('%s') -> %s(%u args)", shopName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Shop> shop = findShopShared(shopName);
	if (!shop) {
		warning("Cyberflix: sendtoshopfx('%s'): shop not open", shopName.c_str());
		return Value();
	}

	Common::Array<const Script *> scopes;
	scopes.push_back(shop->shopScript());
	return engine.dispatchWithScopeChainValue(scopes, shop->name(), Common::String(),
			message, args, "shopfx");
}

void PropRuntime::sendToProp(CyberflixEngine &engine, const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtoprop('%s') -> %s(%u args)", propName.c_str(),
			message.c_str(), args.size());
	Shop::Prop *prop = nullptr;
	Common::SharedPtr<Shop> shopOwner = findPropOwnerShared(propName, &prop);
	if (!prop) {
		warning("Cyberflix: sendtoprop('%s'): no such prop", propName.c_str());
		return;
	}
	engine.dispatchWithScopes(prop->script.get(), shopOwner->shopScript(), prop->name, prop->name,
			message, args);
	refreshPropsIfDirty(engine);
}

Value PropRuntime::sendToPropFx(CyberflixEngine &engine, const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "Cyberflix: sendtopropfx('%s') -> %s(%u args)", propName.c_str(),
			message.c_str(), args.size());
	Shop::Prop *prop = nullptr;
	Common::SharedPtr<Shop> shopOwner = findPropOwnerShared(propName, &prop);
	if (!prop) {
		warning("Cyberflix: sendtopropfx('%s'): no such prop", propName.c_str());
		return Value();
	}
	return engine.dispatchWithScopesValue(prop->script.get(), shopOwner->shopScript(),
			prop->name, prop->name, message, args, "propfx");
}

static bool shouldLogInterfaceProp(const Common::String &name) {
	return name.equalsIgnoreCase("life") ||
			name.equalsIgnoreCase("watch") ||
			name.equalsIgnoreCase("bag") ||
			name.equalsIgnoreCase("map") ||
			name.equalsIgnoreCase("lid") ||
			name.equalsIgnoreCase("light") ||
			name.equalsIgnoreCase("invenhelp");
}

static bool shouldLogEnigmaProp(const Common::String &name) {
	return name.equalsIgnoreCase("dial1") ||
			name.equalsIgnoreCase("dial2") ||
			name.equalsIgnoreCase("dial3") ||
			name.equalsIgnoreCase("dial4") ||
			name.equalsIgnoreCase("zeitgram") ||
			name.equalsIgnoreCase("enigsw") ||
			name.equalsIgnoreCase("enigswlight") ||
			name.equalsIgnoreCase("enigwirer") ||
			name.equalsIgnoreCase("enigwireg") ||
			name.equalsIgnoreCase("enigdecode") ||
			name.equalsIgnoreCase("enigmess") ||
			name.equalsIgnoreCase("reset");
}

bool PropRuntime::propVisible(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propvisible('%s'): no such prop", name.c_str());
		return false;
	}
	if (shouldLogInterfaceProp(name) || shouldLogEnigmaProp(name))
		debug(1, "Cyberflix: propvisible('%s') -> %s", name.c_str(),
				prop->visible ? "true" : "false");
	return prop->visible;
}

void PropRuntime::propVisible(const Common::String &name, bool visible) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propvisible('%s'): no such prop", name.c_str());
		return;
	}
	if (shouldLogInterfaceProp(name) || shouldLogEnigmaProp(name))
		debug(1, "Cyberflix: propvisible('%s', %s) old=%s", name.c_str(),
				visible ? "true" : "false", prop->visible ? "true" : "false");
	if (prop->visible != visible) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
		prop->visible = visible;
		markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
	}
}

Common::String PropRuntime::propView(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propview('%s'): no such prop", name.c_str());
		return Common::String();
	}
	if (shouldLogInterfaceProp(name) || shouldLogEnigmaProp(name))
		debug(1, "Cyberflix: propview('%s') -> '%s'", name.c_str(),
				prop->shapeName.c_str());
	return prop->shapeName;
}

void PropRuntime::propView(const Common::String &name, const Common::String &shape) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propview('%s'): no such prop", name.c_str());
		return;
	}
	// FUN_004293a0 validates the shape against the prop master (FUN_0042c0c0)
	// and leaves the prop on the shape's LAST pose (+0x20 = poseCount - 1).
	Shop::ShapePoseResult pose = shop->shapePoseCount(*prop, shape);
	if (!pose.valid) {
		warning("Cyberflix: propview('%s'): no shape '%s'", name.c_str(), shape.c_str());
		return;
	}
	Common::String key = shape;
	key.toLowercase();
	if (shouldLogInterfaceProp(name) || shouldLogEnigmaProp(name))
		debug(1, "Cyberflix: propview('%s', '%s') old='%s'", name.c_str(),
				key.c_str(), prop->shapeName.c_str());
	uint16 newPoseIndex = pose.poseCount ? pose.poseCount - 1 : 0;
	if (prop->shapeName != key || prop->poseCount != pose.poseCount || prop->poseIndex != newPoseIndex) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
		prop->shapeName = key;
		prop->poseCount = pose.poseCount;
		prop->poseIndex = newPoseIndex;
		markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
	}
}

int PropRuntime::propXY(const Common::String &name, int selector) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propxy('%s', %d): no such prop", name.c_str(), selector);
		return 0;
	}
	switch (selector) {
	case 1:
		return prop->x;
	case 2:
		return prop->y;
	case 3:
		return (static_cast<int32>(prop->x) << 16) | (static_cast<int32>(prop->y) & 0xffff);
	default:
		warning("Cyberflix: propxy('%s', %d): bad selector", name.c_str(), selector);
		return 0;
	}
}

void PropRuntime::setPropXY(const Common::String &name, int x, int y) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propxy('%s'): no such prop", name.c_str());
		return;
	}
	// FUN_0042a370: screen-space placement — mode = 0, depth = -1 when the
	// prop was world-space (>= 0), anchor = (x, y) (record +0x16/+0x14).
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->mode = 0;
	if (prop->depth >= 0)
		prop->depth = -1;
	prop->x = static_cast<int16>(x);
	prop->y = static_cast<int16>(y);
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

void PropRuntime::propSet(const Common::String &name, const Common::String &setName) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propset('%s'): no such prop", name.c_str());
		return;
	}
	Common::String key = setName;
	key.toLowercase();
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propset('%s', '%s') mode %u -> 1", name.c_str(),
				key.c_str(), prop->mode);
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->setName = key;
	prop->mode = 1; // FUN_00428c20 writes record +0x12 = 1 for SET placement.
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

void PropRuntime::propXYZ(const Common::String &name, int x, int y, int z) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propxyz('%s'): no such prop", name.c_str());
		return;
	}
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propxyz('%s', %d, %d, %d) mode %u -> 1",
				name.c_str(), x, y, z, prop->mode);
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->mode = 1; // FUN_0042a140: world/SET-space placement.
	prop->x = static_cast<int16>(x);
	prop->y = static_cast<int16>(y);
	prop->z = static_cast<int16>(z);
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

void PropRuntime::propScale(const Common::String &name, int scale) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propscale('%s'): no such prop", name.c_str());
		return;
	}
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->scale = scale < 0 ? 0 : scale;
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

void PropRuntime::propZClip(const Common::String &name, int dist) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propzclip('%s'): no such prop", name.c_str());
		return;
	}
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
	prop->zClip = dist;
	markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
}

int PropRuntime::getPropDist(CyberflixEngine &engine, const Common::String &name) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propdist('%s'): no such prop", name.c_str());
		return 0;
	}

	if (prop->mode == 0)
		return prop->depth;

	SetRuntime &setRuntime = engine.setRuntime();
	if (!setRuntime.set() || !setRuntime.set()->isOpen())
		return 32000;

	bool haveCamera = false;
	Set::CameraData cameraData;
	if (setRuntime.transitionType() == kSetTransitionForward) {
		haveCamera = setRuntime.set()->transitionCameraData(
				setRuntime.transitionResource(), setRuntime.transitionFrame(), cameraData);
	} else if (setRuntime.scene() >= 0) {
		haveCamera = setRuntime.set()->cameraData(static_cast<uint32>(setRuntime.scene()),
				static_cast<uint32>(setRuntime.table()), static_cast<uint32>(setRuntime.angle()), cameraData);
	}
	if (!haveCamera)
		return 32000;

	Shop::WorldCamera camera;
	camera = makeWorldCamera(cameraData);

	Shop::PropRenderResult rendered = shop->renderWorldProp(*prop, camera,
			setRuntime.set()->setName());
	if (!rendered.valid)
		return 32000;
	return rendered.depth;
}

void PropRuntime::propDist(const Common::String &name, int dist) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propdist('%s'): no such prop", name.c_str());
		return;
	}
	// FUN_004295c0: only applied to screen-space props with a negative value.
	if (prop->mode == 0 && dist < 0) {
		debug(1, "Cyberflix: propdist('%s', %d) depth %d -> %d",
				name.c_str(), dist, prop->depth, dist);
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
		prop->depth = static_cast<int16>(dist);
		markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
	}
}

int PropRuntime::getPropDeg(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propdeg('%s'): no such prop", name.c_str());
		return 0;
	}
	if (shouldLogEnigmaProp(name))
		debug(1, "Cyberflix: propdeg('%s') -> %d", name.c_str(), prop->angle);
	return prop->angle;
}

int PropRuntime::setPropDeg(const Common::String &name, int newDeg) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop) {
		warning("Cyberflix: propdeg('%s'): no such prop", name.c_str());
		return 0;
	}
	if (shouldLogEnigmaProp(name))
		debug(1, "Cyberflix: propdeg('%s', %d) old=%d",
				name.c_str(), newDeg & 0xff, prop->angle);
	if (prop->angle != static_cast<int16>(newDeg & 0xff)) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*shop, *prop, oldRect);
		prop->angle = static_cast<int16>(newDeg & 0xff);
		markPropDirty(*shop, *prop, hadOldRect ? &oldRect : nullptr);
	}
	return prop->angle;
}

Common::String PropRuntime::getPropOwner(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propowner('%s'): no such prop", name.c_str());
		return Common::String();
	}
	return prop->owner;
}

Common::String PropRuntime::setPropOwner(const Common::String &name, const Common::String &newOwner) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propowner('%s'): no such prop", name.c_str());
		return Common::String();
	}
	prop->owner = newOwner; // FUN_00428d40: copy into record +0x8c
	if (shouldLogInterfaceProp(name))
		debug(1, "Cyberflix: propowner('%s'%s%s%s) -> '%s'", name.c_str(),
				", '", newOwner.c_str(), "'",
				prop->owner.c_str());
	return prop->owner;
}

int PropRuntime::getPropValue(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propvalue('%s'): no such prop", name.c_str());
		return 0;
	}
	return prop->value;
}

int PropRuntime::setPropValue(const Common::String &name, int newValue) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("Cyberflix: propvalue('%s'): no such prop", name.c_str());
		return 0;
	}
	prop->value = newValue; // FUN_00428e00: copy int to record +0x46
	return prop->value;
}

int PropRuntime::countProps() const {
	int total = 0;
	for (uint32 i = 0; i < _shops.size(); ++i)
		total += static_cast<int>(_shops[i]->propCount());
	return total;
}

Common::String PropRuntime::indexToProp(int index) const {
	// 1-based index into the global prop array (FUN_0042b550).
	int i = index - 1;
	for (uint32 s = 0; s < _shops.size(); ++s) {
		if (i >= 0 && i < static_cast<int>(_shops[s]->propCount()))
			return _shops[s]->prop(static_cast<uint32>(i)).name;
		i -= static_cast<int>(_shops[s]->propCount());
	}
	return Common::String();
}

bool PropRuntime::pointInProp(const Common::String &name, int32 packedPoint) {
	Shop *shop = nullptr;
	Shop::Prop *prop = findProp(name, &shop);
	if (!prop || !shop) {
		warning("Cyberflix: pointinprop('%s'): no such prop", name.c_str());
		return false;
	}
	if (!prop->visible || prop->mode != 0)
		return false;

	Shop::PropRenderResult rendered = shop->renderProp(*prop);
	if (!rendered.valid || !rendered.cel)
		return false;

	const int16 x = static_cast<int16>(packedPoint >> 16);
	const int16 y = static_cast<int16>(packedPoint & 0xffff);
	if (x < rendered.rect.left || x >= rendered.rect.right ||
			y < rendered.rect.top || y >= rendered.rect.bottom)
		return false;

	const int celX = x - rendered.rect.left;
	const int celY = y - rendered.rect.top;
	if (celX < 0 || celY < 0 ||
			celX >= rendered.cel->width || celY >= rendered.cel->height)
		return false;
	return rendered.cel->isOpaque(celX, celY);
}

void PropRuntime::refreshPropsIfDirty(CyberflixEngine &engine) {
	// The original recomposites the display list every tick; this engine
	// renders on demand, so repaint the current room after a dispatch that
	// changed prop state. While no scene is up yet (boot-time initprops) the
	// props are picked up by the next renderSetScene.
	if (!_propsDirty)
		return;
	// Native loop callbacks run before the single display pass; repainting
	// inside each callback would advance animated prop poses too often.
	if (engine.loopRuntime().processingScheduledLoops())
		return;
	if (engine._puppetRuntime.isVisible()) {
		// Native sendtoprop/sendtoshop dispatch does not repaint immediately.
		// The next forceupdate() takes the puppet compositor branch, so keep
		// SET prop dirtiness queued until the puppet is hidden or closed.
		return;
	}
	const bool replacementStage = isReplacementStageForProps(engine._stageRuntime.stage());
	if (!engine._setRuntime.visible() || replacementStage) {
		if (engine._stageRuntime.stage() && engine._stageRuntime.stage()->isOpen()) {
			if (!_dirtyRects.empty() || (replacementStage && hasAnimatedScreenProps())) {
				engine.stageRuntime().repaintDirtyStageRects(engine);
			} else {
				// Prop refresh without dirty bounds is still a compositor repaint,
				// not navigation to a new flat, so keep the current script cursor.
				engine.stageRuntime().renderStageNode(engine, engine.stageRuntime().node(), false);
			}
		}
		_dirtyRects.clear();
		_propsDirty = false;
		return;
	}
	SetRuntime &setRuntime = engine.setRuntime();
	if (setRuntime.set() && setRuntime.set()->isOpen() && setRuntime.scene() >= 0) {
		// ScummVM-only optimization matching the native backing-surface model:
		// prop mutations do not change the SET background, and the current
		// decoded/transitioned background is already retained in _setRuntime.frameSequence().
		// Recompose that surface with live props instead of re-decoding the same
		// compressed panorama/transition frame for every sendtoprop() refresh.
		if (!setRuntime.frameSequence().empty())
			setRuntime.displaySetFrame(engine, setRuntime.frameSequence());
		else
			setRuntime.renderSetScene(engine, setRuntime.scene(), setRuntime.table(), setRuntime.angle());
	}
	_dirtyRects.clear();
}

} // End of namespace Cyberflix
