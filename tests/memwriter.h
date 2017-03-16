#ifndef FIES_TESTS_MEMWRITER_H
#define FIES_TESTS_MEMWRITER_H

#include "cppwriter.h"

struct MemWriter : Writer {
	MemWriter();
	~MemWriter();
	ssize_t writev(const struct iovec *iov, size_t cnt) override;
	vector<uint8_t> data_;
};

#endif
