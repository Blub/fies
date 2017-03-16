#include "test.h"
#include "checkfile.h"

CheckFile::CheckFile(string name,
                     fies_sz size,
                     uint32_t mode,
                     std::initializer_list<DataRange> ranges)
	: name_(move(name))
	, size_(size)
	, mode_(mode)
	, ranges_(ranges)
{}

size_t
CheckFile::findRange(fies_pos offset)
{
	auto beg = ranges_.begin();
	auto end = ranges_.end();
	auto pos = binsearch(beg, end, offset,
		[](fies_pos off, const DataRange& r)
		-> int {
			return (off < r.pos) ? -1 :
			       (off >= r.pos+r.len) ? 1 :
			       0;
		});
	if (pos < beg || pos >= end)
		return ranges_.size();
	return cast<size_t>(pos-beg);
}

fies_ssz
CheckFile::pwrite(const void *data, fies_sz count, fies_pos offset)
{
	auto ri = findRange(offset);
	if (ri == ranges_.size()) {
		err("file %s: writing out ouf range: %" PRI_X_FIES_POS "\n",
		    c_name(), offset);
		return -EINVAL;
	}
	auto& r = ranges_[ri];
	r.written++;
	if (r.written > r.expected) {
		err("file %s: wrote range %zu %zu times, expected %zu\n",
		    c_name(), ri, r.written, r.expected);
		return -EEXIST;
	}
	if (r.data == DataClass::Omitted ||
	    r.data == DataClass::Cloned)
	{
		err("file %s: wrote omitted extent %zu at "
		    "%" PRI_X_FIES_POS "\n",
		    c_name(), ri, r.pos);
	} else if (r.data == DataClass::Zero && data) {
		err("file %s: wrote data over zero extent %zu at "
		    "%" PRI_X_FIES_POS "\n",
		    c_name(), ri, r.pos);
	} else if (r.data != DataClass::Zero && !data) {
		err("file %s: wrote zeros over data extent %zu at "
		    "%" PRI_X_FIES_POS "\n",
		    c_name(), ri, r.pos);
	} else if (r.data == DataClass::PosData) {
		auto pb = reinter<const fies_pos*>(data);
		auto end = pb + count / sizeof(*pb);
		fies_pos at = offset;
		while (pb != end) {
			if (*pb != at) {
				err("file %s: bad positional data: "
				    "%" PRI_X_FIES_POS " != %" PRI_X_FIES_POS
				    "\n",
				    c_name(), *pb, at);
				break;
			}
			at += sizeof(*pb);
			++pb;
		}
	}
	return cast<fies_ssz>(count);
}

int
CheckFile::clone(fies_pos off, CheckFile *src, fies_pos src_pos, fies_sz len)
{
	auto ri = findRange(off);
	if (ri >= ranges_.size()) {
		err("file %s: writing out ouf range: %" PRI_X_FIES_POS "\n",
		    c_name(), off);
		return -EINVAL;
	}
	auto si = src->findRange(src_pos);
	if (si >= src->ranges_.size()) {
		err("file %s: cloning bad range from %s\n",
		    c_name(), src->c_name());
		return -EINVAL;
	}
	auto& r = ranges_[ri];
	r.written++;
	if (r.written > r.expected) {
		err("file %s: wrote range %zu %zu times, expected %zu\n",
		    c_name(), ri, r.written, r.expected);
		return -EEXIST;
	}
	if (r.data == DataClass::Cloned) {
		if (len != r.len) {
			err("file %s: cloned extent of unexpected size: "
			    "%" PRI_X_FIES_POS " != %" PRI_X_FIES_POS "\n",
			    c_name(), len, r.len);
		}
		return 0;
	} else if (r.data == DataClass::Omitted) {
		err("file %s: cloned omitted extent %zu at "
		    "%" PRI_X_FIES_POS "\n",
		    c_name(), ri, r.pos);
	} else if (r.data == DataClass::Zero) {
		err("file %s: cloned zero extent %zu at "
		    "%" PRI_X_FIES_POS "\n",
		    c_name(), ri, r.pos);
	}
	err("file %s: cloned non-clone extent %zu at "
	    "%" PRI_X_FIES_POS "\n",
	    c_name(), ri, r.pos);
	return -EINVAL;
}

int
CheckFile::done()
{
	for (auto i : num::range(ranges_.size())) {
		if (ranges_[i].written != ranges_[i].expected) {
			err("file: %s: extent %zu: written %zu time (!=%zu)\n",
			    c_name(), i,
			    ranges_[i].written, ranges_[i].expected);
			// we don't bail out just yet
		}
	}
	return 0;
}
