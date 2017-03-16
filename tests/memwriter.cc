#include <string.h>

#include "memwriter.h"

MemWriter::MemWriter()
	: Writer(nullptr)
{
	createDefault();
}

MemWriter::~MemWriter()
{
}

ssize_t
MemWriter::writev(const struct iovec *iov, size_t cnt)
{
	size_t size = 0;
	for (size_t i = 0; i != cnt; ++i)
		size += iov[i].iov_len;

	size_t at = data_.size();
	data_.resize(data_.size() + size);
	for (size_t i = 0; i != cnt; ++i) {
		::memcpy(data_.data()+at, iov[i].iov_base, iov[i].iov_len);
		at += iov[i].iov_len;
	}

	return cast<ssize_t>(size);
}
