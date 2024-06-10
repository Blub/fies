#ifndef FIES_TESTS_TESTFILE_H
#define FIES_TESTS_TESTFILE_H

#include <string.h>

#include "cppfile.h"

struct TestExtent {
	TestExtent() = delete;

	TestExtent(FiesFile_Extent extent,
	           size_t query_expected,
	           size_t read_expected);

	FiesFile_Extent extent_;
	size_t query_expected_;
	size_t read_expected_;

	size_t query_count_ = 0;
	size_t read_count_ = 0;
};

struct TestFile : File {
	TestFile() = delete;

	TestFile(string name,
	         fies_pos size,
	         std::initializer_list<TestExtent> extents);

	void done() const;

	ssize_t nextExtents(FiesWriter*,
	                    fies_pos logical_start,
	                    FiesFile_Extent *buffer,
	                    size_t buffer_elements) override;
	ssize_t preadp(void *buffer,
	               size_t length,
	               fies_pos offset,
	               fies_pos physical) override;

	size_t findExtent(fies_pos offset) const;
	void markLogical(fies_pos offset, size_t length);
	fies_pos endpos() const;

	const char *c_name() const;

	string name_;
	fies_pos size_;
	vector<TestExtent> extents_;
};

inline fies_pos
TestFile::endpos() const
{
	return extents_.empty() ? 0 :
	       extents_.back().extent_.logical +
	       extents_.back().extent_.length;
}

inline const char*
TestFile::c_name() const
{
	return name_.c_str();
}

#endif
