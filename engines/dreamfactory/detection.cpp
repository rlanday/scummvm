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

#include "base/plugins.h"
#include "engines/advancedDetector.h"

#include "dreamfactory/detection.h"
#include "dreamfactory/debug.h"

static const DebugChannelDef debugChannels[] = {
	{DreamFactory::kDebugResources, "resources", "Resource loading and decoding"},
	{DreamFactory::kDebugMovies, "movies", "Movie playback and timing"},
	{DreamFactory::kDebugVM, "vm", "Script virtual machine"},
	{DreamFactory::kDebugAudio, "audio", "Audio playback"},
	DEBUG_CHANNEL_END
};

static const PlainGameDescriptor dreamFactoryGames[] = {
	{"titanicaoot", "Titanic: Adventure Out of Time"},
	{nullptr, nullptr}
};

#include "dreamfactory/detection_tables.h"

using namespace DreamFactory;

// Match from the DATA subdirectory too, so the game is detected even when the
// player points ScummVM at the installed top-level folder.
static const char *const directoryGlobs[] = {
	"data",
	nullptr
};

class DreamFactoryMetaEngineDetection : public AdvancedMetaEngineDetection<DreamFactory::DreamFactoryGameDescription> {
public:
	DreamFactoryMetaEngineDetection() : AdvancedMetaEngineDetection(DreamFactory::gameDescriptions, dreamFactoryGames) {
		_maxScanDepth = 2;
		_directoryGlobs = directoryGlobs;
	}

	const char *getName() const override {
		return "dreamfactory";
	}

	const DebugChannelDef *getDebugChannels() const override {
		return debugChannels;
	}

	const char *getEngineName() const override {
		return "CyberFlix DreamFactory";
	}

	const char *getOriginalCopyright() const override {
		return "Titanic: Adventure Out of Time (C) 1996-1997 CyberFlix, Inc.";
	}
};

REGISTER_PLUGIN_STATIC(DREAMFACTORY_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, DreamFactoryMetaEngineDetection);
