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
#include <AppKit/NSView.h>
#include <AppKit/NSWindow.h>

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
