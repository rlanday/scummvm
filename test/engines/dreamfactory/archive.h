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

#include <cxxtest/TestSuite.h>

#include "common/endian.h"
#include "common/memstream.h"
#include "engines/dreamfactory/archive.h"

class DreamFactoryArchiveTestSuite : public CxxTest::TestSuite {
	class ReportedSizeStream : public Common::MemoryReadStream {
	public:
		ReportedSizeStream(const byte *data, uint32 readableSize, int64 reportedSize) :
				MemoryReadStream(data, readableSize), _reportedSize(reportedSize) {}
		int64 size() const override { return _reportedSize; }
	private:
		int64 _reportedSize;
	};

	class FailingSeekStream : public Common::MemoryReadStream {
	public:
		FailingSeekStream(const byte *data, uint32 size) : MemoryReadStream(data, size) {}
		bool seek(int64 offset, int whence = SEEK_SET) override {
			return offset < 0x400 && MemoryReadStream::seek(offset, whence);
		}
	};

	Common::Array<byte> container() {
		Common::Array<byte> data(0x420);
		memset(data.begin(), 0, data.size());
		WRITE_LE_UINT32(data.begin(), 0x10000);
		WRITE_LE_UINT32(data.begin() + 4, data.size());
		WRITE_LE_UINT32(data.begin() + 0x14, 2);
		memcpy(data.begin() + 0x20, "LPPALPPA", 8);
		WRITE_LE_UINT32(data.begin() + 0x400, 0x408);
		WRITE_LE_UINT32(data.begin() + 0x408, 99);
		WRITE_LE_UINT32(data.begin() + 0x40c, 12);
		data[0x414] = 42;
		return data;
	}

public:
	void testUnsupportedStreamSizes() {
		const Common::Array<byte> data = container();
		DreamFactory::Archive archive;
		TS_ASSERT(!archive.open(new ReportedSizeStream(data.begin(), data.size(), -1), "test"));
		TS_ASSERT(!archive.open(new ReportedSizeStream(data.begin(), data.size(), 0x100000000LL), "test"));
		TS_ASSERT(!archive.open(new Common::MemoryReadStream(data.begin(), 0x27), "test"));
	}

	void testShortRecordReadClosesArchive() {
		const Common::Array<byte> data = container();
		DreamFactory::Archive archive;
		TS_ASSERT(!archive.open(new ReportedSizeStream(data.begin(), 0x40a, data.size()), "test"));
		TS_ASSERT(!archive.isOpen());
		TS_ASSERT_EQUALS(archive.resourceIndexById(99), -1);
	}

	void testFailedDirectorySeekClosesArchive() {
		const Common::Array<byte> data = container();
		DreamFactory::Archive archive;
		TS_ASSERT(!archive.open(new FailingSeekStream(data.begin(), data.size()), "test"));
		TS_ASSERT(!archive.isOpen());
	}

	void testDuplicateIdsKeepFirstNonEmptyEntry() {
		Common::Array<byte> data = container();
		WRITE_LE_UINT32(data.begin() + 0x404, 0x414);
		WRITE_LE_UINT32(data.begin() + 0x414, 99);
		DreamFactory::Archive archive;
		TS_ASSERT(archive.open(new Common::MemoryReadStream(data.begin(), data.size()), "test"));
		TS_ASSERT_EQUALS(archive.resourceIndexById(99), 0);
		TS_ASSERT_EQUALS(archive.resourceIndexById(1), -1);
		archive.close();
		TS_ASSERT_EQUALS(archive.resourceIndexById(99), -1);
		WRITE_LE_UINT32(data.begin() + 0x400, 0);
		TS_ASSERT(archive.open(new Common::MemoryReadStream(data.begin(), data.size()), "test"));
		TS_ASSERT_EQUALS(archive.resourceIndexById(99), 1);
	}

	void testSwapLongsLeavesPartialWordUnchanged() {
		byte data[] = { 1, 2, 3, 4, 5, 6 };
		DreamFactory::Archive::swapLongs(data, sizeof(data));
		TS_ASSERT_EQUALS(data[0], 4);
		TS_ASSERT_EQUALS(data[3], 1);
		TS_ASSERT_EQUALS(data[4], 5);
		TS_ASSERT_EQUALS(data[5], 6);
	}

	void testStoredIdIsNotDirectoryIndex() {
		const Common::Array<byte> data = container();
		DreamFactory::Archive archive;
		TS_ASSERT(archive.open(new Common::MemoryReadStream(data.begin(), data.size()), "test"));
		TS_ASSERT_EQUALS(archive.getResourceCount(), 2U);
		const DreamFactory::Archive::Resource &resource = archive.getResource(0);
		TS_ASSERT_EQUALS(resource.id, 99U);
		TS_ASSERT(archive.getResource(1).empty);
		Common::ScopedPtr<Common::SeekableReadStream> stream(archive.createReadStreamForResource(resource));
		TS_ASSERT(stream);
		if (stream)
			TS_ASSERT_EQUALS(stream->readByte(), 42);
		TS_ASSERT(!archive.createReadStreamForResource(99));
	}

	void testTruncatedPayloadIsClamped() {
		Common::Array<byte> data = container();
		WRITE_LE_UINT32(data.begin() + 0x40c, 0xffffffffU);
		DreamFactory::Archive archive;
		TS_ASSERT(archive.open(new Common::MemoryReadStream(data.begin(), data.size()), "test"));
		TS_ASSERT_EQUALS(archive.getResource(0).length, 12U);
	}

	void testInvalidOffsetRemainsEmpty() {
		Common::Array<byte> data = container();
		WRITE_LE_UINT32(data.begin() + 0x400, 0xfffffffcU);
		DreamFactory::Archive archive;
		TS_ASSERT(archive.open(new Common::MemoryReadStream(data.begin(), data.size()), "test"));
		TS_ASSERT(archive.getResource(0).empty);
	}

	void testImpossibleDirectoryCountFailsAndCloses() {
		Common::Array<byte> data = container();
		DreamFactory::Archive archive;
		TS_ASSERT(archive.open(new Common::MemoryReadStream(data.begin(), data.size()), "test"));
		WRITE_LE_UINT32(data.begin() + 0x14, 0xffffffffU);
		TS_ASSERT(!archive.open(new Common::MemoryReadStream(data.begin(), data.size()), "test"));
		TS_ASSERT_EQUALS(archive.getResourceCount(), 0U);
	}
};
