#include <string.h>
#include "cppwriter.h"

static ssize_t
vf_writev(void *opaque, const struct iovec *iov, size_t cnt)
{
	auto self = reinter<Writer*>(opaque);
	return self->writev(iov, cnt);
}

static ssize_t
vf_sendfile(void *opaque, FiesFile *fd, fies_pos offset, size_t length)
{
	auto self = reinter<Writer*>(opaque);
	return self->sendfile(fd, offset, length);
}

static void
vf_finalize(void *opaque)
{
	auto self = reinter<Writer*>(opaque);
	return self->finalize();
}

const FiesWriter_Funcs
cppwriter_funcs = {
	vf_writev,
	vf_sendfile,
	vf_finalize
};

#pragma clang diagnostic ignored "-Wunused-parameter"

Writer::Writer()
	: Writer(nullptr)
{
	createDefault();
}

void
Writer::createDefault()
{
	self_ = FiesWriter_new(&cppwriter_funcs, reinter<void*>(this));
}

Writer::~Writer()
{
	FiesWriter_delete(self_);
}

ssize_t
Writer::writev(const struct iovec *iov, size_t cnt)
{
	return -ENOTSUP;
}

ssize_t
Writer::sendfile(FiesFile *fd, fies_pos offset, size_t length)
{
	return -ENOTSUP;
}

void
Writer::finalize()
{
	if (finalized_)
		throw FinalizedException();
	finalized_ = true;
}
