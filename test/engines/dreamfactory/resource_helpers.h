#include <cxxtest/TestSuite.h>

#include "engines/dreamfactory/resource_helpers.h"

class DreamFactoryResourceHelpersTestSuite : public CxxTest::TestSuite {
public:
	void testRecordRangeIteration() {
		const byte data[] = { 1, 2, 3, 4, 5 };
		const DreamFactory::ResourceView view(data, sizeof(data));
		const DreamFactory::RecordRange records(view, 1, 2, 2);
		uint count = 0;
		for (const DreamFactory::ResourceView record : records) {
			TS_ASSERT_EQUALS(record.size(), 2U);
			TS_ASSERT_EQUALS(*record.dataAt(0, 1), 2 + count * 2);
			++count;
		}
		TS_ASSERT_EQUALS(count, 2U);
		const DreamFactory::RecordRange invalid(view, 1, 3, 2);
		for (const DreamFactory::ResourceView record : invalid) {
			(void)record;
			TS_FAIL("An invalid range must be empty");
		}
	}

	void testResourceViewBounds() {
		const byte data[] = { 3, 'f', 'o', 'o', 0x34, 0x12 };
		const DreamFactory::ResourceView view(data, sizeof(data));

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
		const DreamFactory::ResourceView view(data, sizeof(data));

		TS_ASSERT(view.readPascalString(0).empty());
		TS_ASSERT_EQUALS(view.readPascalString(0, true), Common::String("bar"));
	}

	void testRecordRangeRejectsIncompleteTable() {
		const byte data[10] = {};
		const DreamFactory::ResourceView view(data, sizeof(data));
		const DreamFactory::RecordRange valid(view, 2, 2, 4);
		const DreamFactory::RecordRange invalid(view, 2, 3, 4);

		TS_ASSERT(valid.valid());
		TS_ASSERT_EQUALS(valid.size(), 2U);
		TS_ASSERT(valid.record(0).valid());
		TS_ASSERT(valid.record(1).valid());
		TS_ASSERT(!valid.record(2).valid());
		TS_ASSERT(!invalid.valid());
		TS_ASSERT_EQUALS(invalid.size(), 0U);
	}
};
