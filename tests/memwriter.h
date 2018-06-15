#ifndef FIES_TESTS_MEMWRITER_H
#define FIES_TESTS_MEMWRITER_H

#include "cppwriter.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
struct MemWriter : Writer {
	MemWriter();
	~MemWriter() override;
	ssize_t writev(const struct iovec *iov, size_t cnt) override;
	vector<uint8_t> data_;
};
#pragma clang diagnostic pop

#endif
