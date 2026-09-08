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

#include "dreamfactory/dreamfactory.h"
#include "dreamfactory/set.h"
#include "dreamfactory/stage.h"
#include "dreamfactory/runtime/set_helpers.h"

namespace DreamFactory {

// ---- Shop/prop subsystem (TI.EXE FUN_00428450 and friends) ----------------
// The original keeps one global prop array across all open shops; here the
// by-name lookups and countprops/indextoprop span _shops in open order, which
// preserves the global-index semantics (shops are only ever appended).

Shop *PropRuntime::findShop(const Common::String &name) {
	Common::SharedPtr<Shop> shop = findShopShared(name);
	return shop.get();
}

Common::SharedPtr<Shop> PropRuntime::findShopShared(const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (const Common::SharedPtr<Shop> &shop : _shops)
		if (shop->name() == key)
			return shop;
	return Common::SharedPtr<Shop>();
}

Shop::Prop *PropRuntime::findProp(const Common::String &name) {
	return findPropRef(name).prop;
}

PropRuntime::PropRef PropRuntime::findPropRef(const Common::String &name) {
	for (const Common::SharedPtr<Shop> &shop : _shops) {
		Shop::Prop *prop = shop->findProp(name);
		if (prop) {
			PropRef ref;
			ref.shop = shop;
			ref.prop = prop;
			return ref;
		}
	}
	return PropRef();
}

bool PropRuntime::resolvePropStar(DreamFactoryEngine &engine, Shop::Prop &prop) {
	if (!engine._setRuntime.set() || !engine._setRuntime.set()->isOpen() ||
			!prop.setName.equalsIgnoreCase(engine._setRuntime.set()->setName()))
		return false;

	int16 x = 0, y = 0, z = 0;
	if (!engine._setRuntime.set()->starXYZ(prop.sceneName, x, y, z))
		return false;

	if (prop.x != x || prop.y != y || prop.z != z || prop.mode != 1) {
		prop.x = x;
		prop.y = y;
		prop.z = z;
		prop.mode = 1;
		_propsDirty = true;
	}
	return true;
}

void PropRuntime::collectScreenProps(Common::Array<DrawEntry> &draw) const {
	for (const Common::SharedPtr<Shop> &shop : _shops) {
		// Native FUN_0042ba40 walks the global SHOP prop array; replacement
		// stages rely on scripts to hide stale props and re-show live overlays.
		for (uint32 i = 0; i < shop->propCount(); ++i) {
			const Shop::Prop &p = shop->prop(i);
			if (p.visible && p.mode == 0)
				draw.push_back(DrawEntry(&p, shop.get(), p.depth));
		}
	}
	// Native FUN_004434f0 sorts larger signed depths first, then hittest walks
	// the display list backward. This leaves more-negative screen props on top.
	for (uint i = 1; i < draw.size(); ++i) {
		const DrawEntry entry = draw[i];
		uint j = i;
		for (; j > 0 && draw[j - 1].depth < entry.depth; --j)
			draw[j] = draw[j - 1];
		draw[j] = entry;
	}
}

void PropRuntime::advancePropPoses() {
	for (const Common::SharedPtr<Shop> &shop : _shops)
		shop->advancePropPoses();
}

void PropRuntime::advanceAnimationFrame(DreamFactoryEngine &engine) {
	struct AnimatedScreenProp {
		Common::SharedPtr<Shop> shop;
		uint32 propIndex = 0;
		Common::Rect oldRect;
		bool hadOldRect = false;
	};
	Common::Array<AnimatedScreenProp> animatedScreenProps;
	// A prop whose pose changed has to reach the screen this pass. Screen props
	// (mode 0) repaint through their dirty bounds; world props only exist while
	// a set is up and ride along with the whole-frame set recomposite.
	const bool setVisible = engine.setRuntime().visible() &&
			engine.setRuntime().set() && engine.setRuntime().set()->isOpen();
	bool animated = false;
	for (const Common::SharedPtr<Shop> &shop : _shops) {
		for (uint32 i = 0; i < shop->propCount(); ++i) {
			const Shop::Prop &prop = shop->prop(i);
			if (!prop.visible || prop.poseCount <= 1)
				continue;
			if (prop.mode != 0) {
				animated |= setVisible;
				continue;
			}
			animated = true;
			AnimatedScreenProp entry;
			entry.shop = shop;
			entry.propIndex = i;
			entry.hadOldRect = screenPropRect(*entry.shop, prop, entry.oldRect);
			animatedScreenProps.push_back(entry);
		}
	}

	advancePropPoses();
	engine.actorRuntime().advanceActorPoses();

	for (const AnimatedScreenProp &entry : animatedScreenProps) {
		if (entry.hadOldRect)
			queueDirtyRect(entry.oldRect);
		Common::Rect newRect;
		if (screenPropRect(*entry.shop, entry.shop->prop(entry.propIndex), newRect))
			queueDirtyRect(newRect);
	}
	if (animated)
		_propsDirty = true;
}

void PropRuntime::collectWorldProps(DreamFactoryEngine &engine, Common::Array<DrawEntry> &draw,
		const Shop::WorldCamera &camera) const {
	if (!engine._setRuntime.set() || !engine._setRuntime.set()->isOpen())
		return;
	const Common::String &setName = engine._setRuntime.set()->setName();
	for (const Common::SharedPtr<Shop> &shop : _shops) {
		for (uint32 i = 0; i < shop->propCount(); ++i) {
			const Shop::Prop &p = shop->prop(i);
			if (!p.visible || p.mode == 0 || !p.setName.equalsIgnoreCase(setName))
				continue;
			Shop::PropRenderResult rendered = shop->renderWorldProp(p, camera, setName);
			if (!rendered.valid)
				continue;
			draw.push_back(DrawEntry(&p, shop.get(), rendered.depth));
		}
	}

	for (uint i = 1; i < draw.size(); ++i) {
		const DrawEntry entry = draw[i];
		uint j = i;
		for (; j > 0 && draw[j - 1].depth < entry.depth; --j)
			draw[j] = draw[j - 1];
		draw[j] = entry;
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

	for (Common::Rect &dirty : _dirtyRects) {
		if (dirty.intersects(clipped)) {
			dirty.extend(clipped);
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

void PropRuntime::openShopFile(DreamFactoryEngine &engine, const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	if (findShop(key)) {
		debug(1, "DreamFactory: shop '%s' already open", key.c_str());
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

void PropRuntime::closeShopFile(DreamFactoryEngine &engine, const Common::String &name) {
	Common::String key = name;
	key.toLowercase();
	for (uint i = 0; i < _shops.size(); ++i) {
		if (_shops[i]->name() == key) {
			debug(1, "DreamFactory: shop '%s' closed", key.c_str());
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
	debug(1, "DreamFactory: closeshopfile('%s'): shop not open", key.c_str());
}

void PropRuntime::propInstance(const Common::String &source, const Common::String &newName) {
	PropRef ref = findPropRef(source);
	if (!ref.prop) {
		warning("DreamFactory: propinstance('%s', '%s'): source prop not found",
				source.c_str(), newName.c_str());
		return;
	}
	if (newName.size() > kMaxInstanceNameLength) {
		warning("DreamFactory: propinstance('%s', '%s'): name too long",
				source.c_str(), newName.c_str());
		return;
	}
	if (findProp(newName))
		return;

	if (ref.shop->addPropInstance(*ref.prop, newName)) {
		debug(1, "DreamFactory: propinstance('%s', '%s')", source.c_str(), newName.c_str());
		_propsDirty = true;
	}
}

void PropRuntime::sendToShop(DreamFactoryEngine &engine, const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "DreamFactory: sendtoshop('%s') -> %s(%u args)", shopName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Shop> shop = findShopShared(shopName);
	if (!shop) {
		warning("DreamFactory: sendtoshop('%s'): shop not open", shopName.c_str());
		return;
	}
	engine.dispatchWithScopes(shop->shopScript(), nullptr, shop->name(), Common::String(),
			message, args);
	refreshPropsIfDirty(engine);
}

Value PropRuntime::sendToShopFx(DreamFactoryEngine &engine, const Common::String &shopName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "DreamFactory: sendtoshopfx('%s') -> %s(%u args)", shopName.c_str(),
			message.c_str(), args.size());
	Common::SharedPtr<Shop> shop = findShopShared(shopName);
	if (!shop) {
		warning("DreamFactory: sendtoshopfx('%s'): shop not open", shopName.c_str());
		return Value();
	}

	Common::Array<const Script *> scopes;
	scopes.push_back(shop->shopScript());
	return engine.dispatchWithScopeChainValue(scopes, shop->name(), Common::String(),
			message, args, "shopfx");
}

void PropRuntime::sendToProp(DreamFactoryEngine &engine, const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "DreamFactory: sendtoprop('%s') -> %s(%u args)", propName.c_str(),
			message.c_str(), args.size());
	PropRef ref = findPropRef(propName);
	if (!ref.prop) {
		warning("DreamFactory: sendtoprop('%s'): no such prop", propName.c_str());
		return;
	}
	engine.dispatchWithScopes(ref.prop->script.get(), ref.shop->shopScript(), ref.prop->name, ref.prop->name,
			message, args);
	refreshPropsIfDirty(engine);
}

Value PropRuntime::sendToPropFx(DreamFactoryEngine &engine, const Common::String &propName, const Common::String &message,
		const Common::Array<Value> &args) {
	debug(1, "DreamFactory: sendtopropfx('%s') -> %s(%u args)", propName.c_str(),
			message.c_str(), args.size());
	PropRef ref = findPropRef(propName);
	if (!ref.prop) {
		warning("DreamFactory: sendtopropfx('%s'): no such prop", propName.c_str());
		return Value();
	}
	return engine.dispatchWithScopesValue(ref.prop->script.get(), ref.shop->shopScript(),
			ref.prop->name, ref.prop->name, message, args, "propfx");
}

bool PropRuntime::propVisible(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propvisible('%s'): no such prop", name.c_str());
		return false;
	}
	return prop->visible;
}

void PropRuntime::propVisible(const Common::String &name, bool visible) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propvisible('%s'): no such prop", name.c_str());
		return;
	}
	if (ref.prop->visible != visible) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
		ref.prop->visible = visible;
		markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
	}
}

Common::String PropRuntime::propView(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propview('%s'): no such prop", name.c_str());
		return Common::String();
	}
	return prop->shapeName;
}

void PropRuntime::propView(const Common::String &name, const Common::String &shape) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propview('%s'): no such prop", name.c_str());
		return;
	}
	// FUN_004293a0 validates the shape against the prop master (FUN_0042c0c0)
	// and stores poseCount - 1 at +0x20. FUN_0042ba40 then increments and wraps
	// that index before composing, making pose 0 the first displayed frame. Keep
	// the first advance pending explicitly: unlike native, this engine may repaint
	// immediately after the dispatch, so pose 0 must already be drawable, while
	// scripts still expect N forceupdate() calls to display poses 0 through N-1.
	// Without the pending advance, HOUSE.SHP's six-update bag animation displays
	// poses 1,2,3,4,5,0 and visibly flickers as it wraps on the last update.
	Shop::ShapePoseResult pose = ref.shop->shapePoseCount(*ref.prop, shape);
	if (!pose.valid) {
		warning("DreamFactory: propview('%s'): no shape '%s'", name.c_str(), shape.c_str());
		return;
	}
	Common::String key = shape;
	key.toLowercase();
	const uint16 newPoseIndex = 0;
	if (ref.prop->shapeName != key || ref.prop->poseCount != pose.poseCount || ref.prop->poseIndex != newPoseIndex) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
		ref.prop->shapeName = key;
		ref.prop->poseCount = pose.poseCount;
		ref.prop->poseIndex = newPoseIndex;
		markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
	}
	// Native restarts the pose sequence even when the requested view is already
	// selected, so this cannot be conditional on a visible state change above.
	ref.prop->poseAdvancePending = true;
}

int PropRuntime::propXY(const Common::String &name, int selector) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propxy('%s', %d): no such prop", name.c_str(), selector);
		return 0;
	}
	switch (selector) {
	case 1:
		return prop->x;
	case 2:
		return prop->y;
	case 3:
		// Native propxy has no z, so its packed point is selector 3 (propxyz
		// uses selector 4 for the same packing).
		return packPoint(prop->x, prop->y);
	default:
		warning("DreamFactory: propxy('%s', %d): bad selector", name.c_str(), selector);
		return 0;
	}
}

void PropRuntime::setPropXY(const Common::String &name, int x, int y) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propxy('%s'): no such prop", name.c_str());
		return;
	}
	// FUN_0042a370: screen-space placement — mode = 0, depth = -1 when the
	// prop was world-space (>= 0), anchor = (x, y) (record +0x16/+0x14).
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
	ref.prop->mode = 0;
	if (ref.prop->depth >= 0)
		ref.prop->depth = -1;
	ref.prop->x = static_cast<int16>(x);
	ref.prop->y = static_cast<int16>(y);
	markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
}

void PropRuntime::propSet(DreamFactoryEngine &engine, const Common::String &name, const Common::String &setName) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propset('%s'): no such prop", name.c_str());
		return;
	}
	Common::String key = setName;
	key.toLowercase();
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
	ref.prop->setName = key;
	ref.prop->mode = 1; // FUN_00428c20 writes record +0x12 = 1 for SET placement.
	resolvePropStar(engine, *ref.prop);
	markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
}

void PropRuntime::propXYZ(const Common::String &name, int x, int y, int z) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propxyz('%s'): no such prop", name.c_str());
		return;
	}
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
	ref.prop->mode = 1; // FUN_0042a140: world/SET-space placement.
	ref.prop->x = static_cast<int16>(x);
	ref.prop->y = static_cast<int16>(y);
	ref.prop->z = static_cast<int16>(z);
	markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
}

// propxyz(name, selector) getter -> FUN_0042a250: world position from the prop
// record; 1=x, 2=y, 3=z, 4=packed x/y point (CONCAT22(x, y) natively).
int PropRuntime::propXYZ(DreamFactoryEngine &engine, const Common::String &name, int selector) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propxyz('%s', %d): no such prop", name.c_str(), selector);
		return 0;
	}
	switch (selector) {
	case 1:
		return prop->x;
	case 2:
		return prop->y;
	case 3:
		return prop->z;
	case 4:
		return engine.makePoint(prop->x, prop->y);
	default:
		warning("DreamFactory: propxyz('%s', %d): bad selector", name.c_str(), selector);
		return 0;
	}
}

Common::String PropRuntime::getPropStar(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propstar('%s'): no such prop", name.c_str());
		return Common::String();
	}
	return prop->sceneName;
}

Common::String PropRuntime::setPropStar(DreamFactoryEngine &engine, const Common::String &name, const Common::String &newStar) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propstar('%s'): no such prop", name.c_str());
		return Common::String();
	}
	Common::String key = newStar;
	key.toLowercase();
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
	ref.prop->sceneName = key;
	ref.prop->mode = 1; // FUN_004291f0 switches propstar placements to SET/world mode.
	resolvePropStar(engine, *ref.prop);
	markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
	return ref.prop->sceneName;
}

void PropRuntime::propScale(const Common::String &name, int scale) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propscale('%s'): no such prop", name.c_str());
		return;
	}
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
	ref.prop->scale = scale < 0 ? 0 : scale;
	markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
}

void PropRuntime::propZClip(const Common::String &name, int dist) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propzclip('%s'): no such prop", name.c_str());
		return;
	}
	Common::Rect oldRect;
	bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
	ref.prop->zClip = dist;
	markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
}

int PropRuntime::getPropDist(DreamFactoryEngine &engine, const Common::String &name) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propdist('%s'): no such prop", name.c_str());
		return 0;
	}

	if (ref.prop->mode == 0)
		return ref.prop->depth;

	SetRuntime &setRuntime = engine.setRuntime();
	CurrentWorldCameraResult camera = currentWorldCamera(setRuntime);
	if (!camera.valid)
		return kOffCameraDepth;

	Shop::PropRenderResult rendered = ref.shop->renderWorldProp(*ref.prop, camera.camera,
			setRuntime.set()->setName());
	if (!rendered.valid)
		return kOffCameraDepth;
	return rendered.depth;
}

void PropRuntime::propDist(const Common::String &name, int dist) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propdist('%s'): no such prop", name.c_str());
		return;
	}
	// FUN_004295c0: only applied to screen-space props with a negative value.
	if (ref.prop->mode == 0 && dist < 0) {
		debug(1, "DreamFactory: propdist('%s', %d) depth %d -> %d",
				name.c_str(), dist, ref.prop->depth, dist);
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
		ref.prop->depth = static_cast<int16>(dist);
		markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
	}
}

int PropRuntime::getPropDeg(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propdeg('%s'): no such prop", name.c_str());
		return 0;
	}
	return prop->angle;
}

int PropRuntime::setPropDeg(const Common::String &name, int newDeg) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: propdeg('%s'): no such prop", name.c_str());
		return 0;
	}
	if (ref.prop->angle != static_cast<int16>(newDeg & 0xff)) {
		Common::Rect oldRect;
		bool hadOldRect = screenPropRect(*ref.shop, *ref.prop, oldRect);
		ref.prop->angle = static_cast<int16>(newDeg & 0xff);
		markPropDirty(*ref.shop, *ref.prop, hadOldRect ? &oldRect : nullptr);
	}
	return ref.prop->angle;
}

Common::String PropRuntime::getPropOwner(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propowner('%s'): no such prop", name.c_str());
		return Common::String();
	}
	return prop->owner;
}

Common::String PropRuntime::setPropOwner(const Common::String &name, const Common::String &newOwner) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propowner('%s'): no such prop", name.c_str());
		return Common::String();
	}
	prop->owner = newOwner; // FUN_00428d40: copy into record +0x8c
	return prop->owner;
}

int PropRuntime::getPropValue(const Common::String &name) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propvalue('%s'): no such prop", name.c_str());
		return 0;
	}
	return prop->value;
}

int PropRuntime::setPropValue(const Common::String &name, int newValue) {
	Shop::Prop *prop = findProp(name);
	if (!prop) {
		warning("DreamFactory: propvalue('%s'): no such prop", name.c_str());
		return 0;
	}
	prop->value = newValue; // FUN_00428e00: copy int to record +0x46
	return prop->value;
}

int PropRuntime::countProps() const {
	int total = 0;
	for (const Common::SharedPtr<Shop> &shop : _shops)
		total += static_cast<int>(shop->propCount());
	return total;
}

Common::String PropRuntime::indexToProp(int index) const {
	// 1-based index into the global prop array (FUN_0042b550).
	int i = index - 1;
	for (const Common::SharedPtr<Shop> &shop : _shops) {
		if (i >= 0 && i < static_cast<int>(shop->propCount()))
			return shop->prop(static_cast<uint32>(i)).name;
		i -= static_cast<int>(shop->propCount());
	}
	return Common::String();
}

bool PropRuntime::pointInProp(const Common::String &name, int32 packedPoint) {
	PropRef ref = findPropRef(name);
	if (!ref.prop) {
		warning("DreamFactory: pointinprop('%s'): no such prop", name.c_str());
		return false;
	}
	if (!ref.prop->visible || ref.prop->mode != 0)
		return false;

	Shop::PropRenderResult rendered = ref.shop->renderProp(*ref.prop);
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

void PropRuntime::refreshPropsIfDirty(DreamFactoryEngine &engine, bool explicitForceUpdate,
		bool present) {
	// The original recomposites the display list every tick; this engine
	// renders on demand, so repaint the current room after a dispatch that
	// changed prop state. While no scene is up yet (boot-time initprops) the
	// props are picked up by the next renderSetScene.
	if (!_propsDirty)
		return;
	// Native loop callbacks run before the single display pass; repainting
	// after each callback would advance animated prop poses too often. An
	// explicit forceupdate() still reaches the compositor even from a callback.
	if (engine.loopRuntime().processingScheduledLoops() && !explicitForceUpdate)
		return;
	if (engine._puppetRuntime.isVisible()) {
		// Native sendtoprop/sendtoshop dispatch does not repaint immediately.
		// The next forceupdate() takes the puppet compositor branch, so keep
		// SET prop dirtiness queued until the puppet is hidden or closed.
		return;
	}
	const bool replacementStage = isReplacementStage(engine._stageRuntime.stage());
	if (!engine._setRuntime.visible() || replacementStage) {
		if (engine._stageRuntime.stage() && engine._stageRuntime.stage()->isOpen()) {
			if (!_dirtyRects.empty()) {
				engine.stageRuntime().repaintDirtyStageRects(engine, present);
			} else {
				// Prop refresh without dirty bounds is still a compositor repaint,
				// not navigation to a new flat, so keep the current script cursor.
				engine.stageRuntime().renderStageNode(engine, engine.stageRuntime().node(), false, present);
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

} // End of namespace DreamFactory
