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

// Disable symbol overrides so that we can use system headers.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "backends/platform/sdl/macosx/macosx-window.h"
#include "backends/platform/sdl/macosx/macosx-compat.h"
#include <AppKit/NSCursor.h>
#include <AppKit/NSEvent.h>
#include <AppKit/NSImage.h>
#include <AppKit/NSView.h>
#include <AppKit/NSWindow.h>

namespace {

NSCursor *getInvisibleCursor() {
	static NSCursor *invisibleCursor = nil;

	if (!invisibleCursor) {
		// SDL's generic visibility flag can say "hidden" while AppKit still has
		// an arrow cursor current. Setting a transparent cursor lets us correct
		// AppKit's current cursor object without touching the NSCursor hide/unhide
		// stack that SDL maintains internally.
		NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(1.0, 1.0)];
		invisibleCursor = [[NSCursor alloc] initWithImage:image hotSpot:NSZeroPoint];
		[image release];
	}

	return invisibleCursor;
}

} // End of anonymous namespace

float SdlWindow_MacOSX::getDpiScalingFactor() const {
#if !SDL_VERSION_ATLEAST(3, 0, 0) && SDL_VERSION_ATLEAST(2, 0, 0) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_7
	SDL_SysWMinfo wmInfo;
	if (getSDLWMInformation(&wmInfo)) {
		NSWindow *nswindow = wmInfo.info.cocoa.window;
		if ([nswindow respondsToSelector:@selector(backingScaleFactor)]) {
			debug(4, "Reported DPI ratio: %g", [nswindow backingScaleFactor]);
			return [nswindow backingScaleFactor];
		}
	}
#endif

	return SdlWindow::getDpiScalingFactor();
}

void SdlWindow_MacOSX::debugHostCursorState(const char *context, bool targetVisible) const {
#if !SDL_VERSION_ATLEAST(3, 0, 0) && SDL_VERSION_ATLEAST(2, 0, 0)
	// This purely feeds a debug(2) line, but the Cocoa/SDL introspection below
	// (autorelease pool, NSCursor queries, mouseLocationOutsideOfEventStream,
	// SDL_ShowCursor(SDL_QUERY)) runs on every cursor update. Skip it entirely
	// unless level-2 logging is active, so normal play does not pay that cost
	// per mouse move (it makes the software cursor stutter).
	if (gDebugLevel < 2)
		return;
	@autoreleasepool {
		SDL_SysWMinfo wmInfo;
		NSWindow *nswindow = nil;
		NSView *contentView = nil;

		if (getSDLWMInformation(&wmInfo) && wmInfo.subsystem == SDL_SYSWM_COCOA) {
			nswindow = wmInfo.info.cocoa.window;
			contentView = [nswindow contentView];
		}

		NSCursor *currentCursor = [NSCursor currentCursor];
		NSCursor *arrowCursor = [NSCursor arrowCursor];
		NSCursor *iBeamCursor = [NSCursor IBeamCursor];
		NSCursor *pointingHandCursor = [NSCursor pointingHandCursor];
		NSPoint windowMouse = NSZeroPoint;
		NSPoint viewMouse = NSZeroPoint;
		BOOL mouseInsideView = NO;

		if (contentView) {
			windowMouse = [nswindow mouseLocationOutsideOfEventStream];
			viewMouse = [contentView convertPoint:windowMouse fromView:nil];
			mouseInsideView = [contentView mouse:viewMouse inRect:[contentView bounds]];
		}

		const int sdlShown = SDL_ShowCursor(SDL_QUERY);
		SDL_Cursor *currentSdlCursor = SDL_GetCursor();
		SDL_Cursor *defaultSdlCursor = SDL_GetDefaultCursor();
		const uint32 windowFlags = getSDLWindow() ? SDL_GetWindowFlags(getSDLWindow()) : 0;

		debug(2, "Cocoa host cursor: %s targetVisible=%d sdlShown=%d sdlCursor=%p defaultCursor=%p windowFlags=0x%x cocoaWindow=%d key=%d main=%d mouseInsideView=%d windowMouse=(%.1f,%.1f) viewMouse=(%.1f,%.1f) nsCurrent=%p arrow=%d ibeam=%d hand=%d",
				context, targetVisible ? 1 : 0, sdlShown,
				(void *)currentSdlCursor, (void *)defaultSdlCursor, windowFlags,
				nswindow ? 1 : 0, nswindow && [nswindow isKeyWindow] ? 1 : 0,
				nswindow && [nswindow isMainWindow] ? 1 : 0, mouseInsideView ? 1 : 0,
				static_cast<double>(windowMouse.x), static_cast<double>(windowMouse.y),
				static_cast<double>(viewMouse.x), static_cast<double>(viewMouse.y),
				(void *)currentCursor, currentCursor == arrowCursor ? 1 : 0,
				currentCursor == iBeamCursor ? 1 : 0,
				currentCursor == pointingHandCursor ? 1 : 0);
	}
#endif
}

void SdlWindow_MacOSX::syncHostCursorVisibility(bool visible) const {
#if !SDL_VERSION_ATLEAST(3, 0, 0) && SDL_VERSION_ATLEAST(2, 0, 0)
	@autoreleasepool {
		SDL_SysWMinfo wmInfo;
		if (!getSDLWMInformation(&wmInfo) || wmInfo.subsystem != SDL_SYSWM_COCOA) {
			return;
		}

		NSWindow *nswindow = wmInfo.info.cocoa.window;
		NSView *contentView = [nswindow contentView];
		if (!contentView) {
			return;
		}

		const NSPoint windowMouse = [nswindow mouseLocationOutsideOfEventStream];
		const NSPoint viewMouse = [contentView convertPoint:windowMouse fromView:nil];
		const BOOL mouseInsideView = [contentView mouse:viewMouse inRect:[contentView bounds]];
		NSCursor *invisibleCursor = getInvisibleCursor();
		NSCursor *currentCursor = [NSCursor currentCursor];

		if (visible) {
			if (currentCursor == invisibleCursor) {
				// We may have installed the transparent cursor on the previous
				// hidden transition. When ScummVM wants the host cursor again
				// outside the game area/window, restore an AppKit cursor object
				// immediately instead of waiting for a later cursor-rect reset.
				[nswindow invalidateCursorRectsForView:contentView];
				[[NSCursor arrowCursor] set];
				debug(2, "Cocoa host cursor: restored visible cursor mouseInsideView=%d windowMouse=(%.1f,%.1f) viewMouse=(%.1f,%.1f)",
						mouseInsideView ? 1 : 0,
						static_cast<double>(windowMouse.x), static_cast<double>(windowMouse.y),
						static_cast<double>(viewMouse.x), static_cast<double>(viewMouse.y));
			}
			return;
		}

		if (!mouseInsideView) {
			return;
		}

		if (currentCursor != invisibleCursor) {
			// AppKit cursor rects can reapply the arrow after SDL has already
			// hidden the cursor, especially after leave/enter or focus changes.
			// Reassert the invisible cursor whenever ScummVM is actively drawing
			// its own cursor inside the SDL view.
			[invisibleCursor set];
			debug(2, "Cocoa host cursor: forced invisible cursor previous=%p windowMouse=(%.1f,%.1f) viewMouse=(%.1f,%.1f)",
					(void *)currentCursor,
					static_cast<double>(windowMouse.x), static_cast<double>(windowMouse.y),
					static_cast<double>(viewMouse.x), static_cast<double>(viewMouse.y));
		}
	}
#endif
}
