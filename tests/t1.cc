#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "memwriter.h"
#include "memreader.h"
#include "extents.h"
#include "testfile.h"

#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"

#if 0
static void
fieserr(FiesReader *fies, int rc)
{
	if (rc == 0)
		return;
	const char *msg = FiesReader_getError(fies);
	if (!msg)
		msg = strerror(rc);
	err("fies error: %s\n", msg);
}
#endif

static void
fieserr(FiesWriter *fies, int rc)
{
	if (rc == 0)
		return;
	const char *msg = FiesWriter_getError(fies);
	if (!msg)
		msg = strerror(rc);
	err("fies error: %s\n", msg);
}

static void
file_test(std::initializer_list<TestFile> testfiles,
          std::initializer_list<CheckFile> checkfiles)
{
	MemWriter mwr;
	ASSERT(mwr);
	auto dev0 = FiesWriter_newDevice(mwr);

	for (auto tf : testfiles) {
		auto f = newFiesFile(&tf, tf.c_name(), tf.size_, 0644_freg,
		                      dev0);
		ASSERT(f);
		fieserr(mwr, FiesWriter_writeFile(mwr, f.get()));
		tf.done();
	}
	MemReader mrd(mwr);
	ASSERT(mrd);
	for (auto cf : checkfiles)
		mrd.expectFile(new CheckFile(cf));
	if (!mrd.readAll())
		err("reading failed");
}

static void
t1()
{
	auto SA = PhyExt { 0x10A000, 0x1000, "ds"_exfl };
	auto SB = PhyExt { 0x10B000, 0x1000, "ds"_exfl };

	auto D1 = PhyExt { 0x001000, 0x1000, "d"_exfl };
	auto D2 = PhyExt { 0x002000, 0x1000, "d"_exfl };

	auto Z1 = PhyExt { 0x003000, 0x1000, "z"_exfl };

	file_test
	({
		{
			"/f1", 0x4000, {
				{ extent(0x0000, SA), 1, 1 },
				{ extent(0x1000, D1), 1, 1 },
				{ extent(0x2000, SB), 1, 1 },
				{ extent(0x3000, Z1), 1, 0 }
			}
		}, {
			"/f2", 0x3000, {
				{ extent(0x0000, SB), 1, 0 },
				{ extent(0x1000, D2), 1, 1 },
				{ extent(0x2000, SA), 1, 0 },
			}
		}
	}, {
		{
			"/f1", 0x4000, 0644_freg, {
				{ 0x0000, 0x1000, DataClass::PosData, 1 },
				{ 0x1000, 0x1000, DataClass::PosData, 1 },
				{ 0x2000, 0x1000, DataClass::PosData, 1 },
				{ 0x3000, 0x1000, DataClass::Zero,    1 },
			}
		}, {
			"/f2", 0x3000, 0644_freg, {
				{ 0x0000, 0x1000, DataClass::Cloned,  1 },
				{ 0x1000, 0x1000, DataClass::PosData, 1 },
				{ 0x2000, 0x1000, DataClass::Cloned,  1 },
			}
		}
	});
}

static void
t_filelist_1()
{
	MemWriter mwr;
	ASSERT(mwr);

	auto dev0 = FiesWriter_newDevice(mwr);

	auto Z1 = PhyExt { 0x100000, 0x1000, "z"_exfl };
	TestFile tf { "/f1", 0x1000, { { extent(0x0000, Z1), 1, 0 } } };
	CheckFile ef { "/f1", 0x1000, 0644_freg, {
	                { 0x0000, 0x1000, DataClass::Zero, 1 } } };
	auto f = newFiesFile(&tf, tf.c_name(), tf.size_, 0644_freg, dev0);
	ASSERT(f);
	fieserr(mwr, FiesWriter_writeFile(mwr, f.get()));
	tf.done();

	struct TestData {
		std::vector<const char*> files;
	} data = { { "snapshot1", "snapshot2", "snapshot3" } };

	fieserr(mwr, FiesWriter_snapshots(mwr, f.get(),
	                                  data.files.data(),
	                                  data.files.size()));

	struct TestReader : MemReader {
		TestData& data;
		TestReader(MemWriter& writer, TestData& d)
			: MemReader(writer)
			, data(d)
		{}

		int snapshots (void *fh, const char **snapshots, size_t count)
		{
			(void)fh;
			ASSERT(data.files.size() == count);
			for (size_t i = 0;i != count; ++i) {
				ASSERT(!::strcmp(snapshots[i], data.files[i]));
			}
			data.files.clear(); // should only happen once
			return 0;
		}
	};

	TestReader trd(mwr, data);
	trd.expectFile(new CheckFile(ef));
	ASSERT(trd);
	if (!trd.readAll())
		err("reading failed");
	ASSERT(!data.files.size());
}

int
main()
{
	t1();
	t_filelist_1();
	return test_errors == 0 ? 0 : 1;
}
