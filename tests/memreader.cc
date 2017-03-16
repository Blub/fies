#include <string.h>

#include "memreader.h"

MemReader::MemReader(const uint8_t *data, size_t length)
	: data_(data)
	, length_(length)
{
	meta_err_ = 0;
}

MemReader::MemReader(MemWriter& wr)
	: MemReader(wr.data_.data(), wr.data_.size())
{}

fies_ssz
MemReader::read(void *dest, fies_sz count)
{
	if (count > remaining())
		count = remaining();

	::memcpy(dest, data(), count);
	pos_ += count;

	return cast<fies_ssz>(count);
}

int
MemReader::create(const char *filename,
                  fies_sz     filesize,
                  uint32_t    mode,
                  void      **out_fh)
{
	auto ex = expected_files_.find(filename);
	if (ex == expected_files_.end()) {
		err("created unexpected file: %s\n", filename);
		*out_fh = nullptr;
		return 0;
	}
	auto fp = ex->second.get();
	*out_fh = fp;
	if (fp->size_ != filesize) {
		err("created file %s of unexpected size "
		    "0x%" PRI_X_FIES_POS " (!= 0x%" PRI_X_FIES_POS ")\n",
		    filename, filesize, fp->size_);
	}
	if (fp->mode_ != mode) {
		err("file %s has unexpected mode: 0%o != 0%o\n",
		    filename, fp->mode_, mode);
	}
	return 0;
}

fies_ssz
MemReader::pwrite(void *pfh,
                  const void *data,
                  fies_sz count,
                  fies_pos offset)
{
	auto fh = reinter<CheckFile*>(pfh);
	return fh->pwrite(data, count, offset);
}

int
MemReader::clone(void    *dest_fh,
                 fies_pos dest_offset,
                 void    *src_fh,
                 fies_pos src_offset,
                 fies_sz  length)
{
	auto dst = reinter<CheckFile*>(dest_fh);
	auto src = reinter<CheckFile*>(src_fh);
	return dst->clone(dest_offset, src, src_offset, length);
}

int
MemReader::close(void *pfh)
{
	auto fh = reinter<CheckFile*>(pfh);
	int rc = fh->done();
	delete fh;
	return rc;
}

int
MemReader::fileDone(void *pfh)
{
	(void)pfh;
	return 0;
	// We do this in close() instead
	//auto fh = reinter<CheckFile*>(pfh);
	//return fh->done();
}
