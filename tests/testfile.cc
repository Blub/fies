#include <stdio.h>
#include "testfile.h"

TestFile::TestFile(string name,
                   fies_pos size,
                   std::initializer_list<TestExtent> extents)
	: name_(move(name))
	, size_(size)
	, extents_(extents)
{}

void
TestFile::done() const
{
	for (auto i : num::range(extents_.size())) {
		if (extents_[i].query_count_ != extents_[i].query_expected_) {
			err("file %s: extent %zu was queried "
			    "%zu instead of %zu times\n",
			    c_name(), i,
			    extents_[i].query_count_,
			    extents_[i].query_expected_);
		}
		if (extents_[i].read_count_ != extents_[i].read_expected_) {
			err("file %s: extent %zu was read "
			    "%zu instead of %zu times\n",
			    c_name(), i,
			    extents_[i].read_count_,
			    extents_[i].read_expected_);
		}
	}
}

size_t
TestFile::findExtent(fies_pos offset) const
{
	auto beg = extents_.begin();
	auto end = extents_.end();
	auto start = binsearch(beg, end, offset,
		[](fies_pos k, const TestExtent& e)
		-> int {
			return (k < e.extent_.logical) ? -1 :
			       (k >= (e.extent_.logical +
			              e.extent_.length)) ? 1 :
			       0;
		});
	if (start < beg)
		return 0;
	if (start >= end)
		return extents_.size();
	return cast<size_t>(start-beg);
}

ssize_t
TestFile::nextExtents(FiesWriter*,
                      fies_pos logical_start,
                      FiesFile_Extent *buffer,
                      size_t count)
{
	size_t ei = findExtent(logical_start);
	if (ei >= extents_.size())
		return 0;
	size_t available = extents_.size() - ei;
	if (count > available)
		count = available;
	for (auto i : num::range(count)) {
		::memcpy(&buffer[i], &extents_[ei+i].extent_, sizeof(*buffer));
		extents_[ei+i].query_count_++;
	}
	return cast<ssize_t>(count);
}

void
TestFile::markLogical(fies_pos offset, size_t length)
{
	if (!length)
		err("file %s: reading zero length\n", c_name());
	if (offset >= size_) {
		err("file %s: reading out of bounds: "
		    "%" PRI_X_FIES_POS " > %" PRI_X_FIES_POS "\n",
		    c_name(), offset, size_);
	}
	size_t ei = findExtent(offset);
	if (ei >= extents_.size()) {
		err("file %s: reading hole at %" PRI_X_FIES_POS "\n",
		    c_name(), offset);
		return;
	}
	if (extents_[ei].extent_.logical > offset) {
		err("file %s: reading hole before extent at "
		    "%" PRI_X_FIES_POS "\n",
		    c_name(), offset);
	}
	fies_pos end = offset + cast<fies_pos>(length);
	while (ei != extents_.size() &&
	       end > extents_[ei].extent_.logical)
	{
		extents_[ei].read_count_++;
		++ei;
	}
}

ssize_t
TestFile::preadp(void *buffer, size_t length, fies_pos offset, fies_pos phys)
{
	(void)phys;
	markLogical(offset, length);

	auto data = reinter<fies_pos*>(buffer);
	const size_t count = length/sizeof(fies_pos);
	for (size_t i = 0; i != count; ++i)
		data[i] = offset + i*sizeof(fies_pos);

	auto raw = reinter<uint8_t*>(buffer);
	auto end = raw + length;
	for (raw += count*sizeof(fies_pos); raw != end; ++raw)
		*raw = 0xFF;
	return cast<ssize_t>(length);
}
