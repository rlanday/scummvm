#include <cxxtest/TestSuite.h>

#include "engines/cyberflix/resource_helpers.h"

class CyberFlixResourceHelpersTestSuite : public CxxTest::TestSuite {
public:
	void testResourceViewBounds() {
		const byte data[] = { 3, 'f', 'o', 'o', 0x34, 0x12 };
		const CyberFlix::ResourceView view(data, sizeof(data));

		TS_ASSERT(view.valid());
		TS_ASSERT(view.contains(0, sizeof(data)));
		TS_ASSERT(view.contains(sizeof(data), 0));
		TS_ASSERT(!view.contains(sizeof(data), 1));
		TS_ASSERT(!view.contains(1, sizeof(data)));
		TS_ASSERT_EQUALS(view.readPascalString(0), Common::String("foo"));
		TS_ASSERT(view.pascalEqualsIgnoreCase(0, "FOO"));
		TS_ASSERT(!view.pascalEqualsIgnoreCase(0, "food"));

		uint16 value = 0;
		TS_ASSERT(view.readUint16LE(4, value));
		TS_ASSERT_EQUALS(value, 0x1234);
		TS_ASSERT(!view.readUint16LE(5, value));
	}

	void testTruncatedPascalString() {
		const byte data[] = { 5, 'b', 'a', 'r' };
		const CyberFlix::ResourceView view(data, sizeof(data));

		TS_ASSERT(view.readPascalString(0).empty());
		TS_ASSERT_EQUALS(view.readPascalString(0, true), Common::String("bar"));
	}

	void testRecordRangeRejectsIncompleteTable() {
		const byte data[10] = {};
		const CyberFlix::ResourceView view(data, sizeof(data));
		const CyberFlix::RecordRange valid(view, 2, 2, 4);
		const CyberFlix::RecordRange invalid(view, 2, 3, 4);

		TS_ASSERT(valid.valid());
		TS_ASSERT_EQUALS(valid.size(), 2U);
		TS_ASSERT(valid.record(0).valid());
		TS_ASSERT(valid.record(1).valid());
		TS_ASSERT(!valid.record(2).valid());
		TS_ASSERT(!invalid.valid());
		TS_ASSERT_EQUALS(invalid.size(), 0U);
	}
};
