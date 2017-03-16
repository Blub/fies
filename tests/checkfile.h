#ifndef FIES_TESTS_CHECKFILE_H
#define FIES_TESTS_CHECKFILE_H

#include <string>
#include <initializer_list>
#include "testfile.h"

enum class DataClass {
	Ignore,
	Omitted,
	Zero,
	Cloned,
	PosData
};

struct DataRange {
	fies_pos  pos;
	fies_sz   len;
	DataClass data;
	size_t    expected;
	size_t    written = 0;
};

struct CheckFile {
	CheckFile() = delete;
	CheckFile(CheckFile&&) = default;
	CheckFile(const CheckFile&) = default;
	CheckFile(string name,
	          fies_sz size,
	          uint32_t mode,
	          std::initializer_list<DataRange> ranges);
	CheckFile(const TestFile& vf,
	          uint32_t mode,
	          std::initializer_list<DataRange> ranges)
		: CheckFile(vf.name_, vf.size_, mode, ranges)
	{}

	size_t findRange(fies_pos offset);

	fies_ssz pwrite(const void *data, fies_sz count, fies_pos offset);
	int clone(fies_pos, CheckFile*, fies_pos srcpos, fies_sz len);
	int done();

	const char *c_name() const;

	string name_;
	fies_sz size_;
	uint32_t mode_;
	vector<DataRange> ranges_;
};

inline const char*
CheckFile::c_name() const
{
	return name_.c_str();
}

#endif
