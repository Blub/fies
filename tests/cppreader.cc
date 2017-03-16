#include <string.h>

#include "cppreader.h"

static fies_ssz
vf_read(void *opaque, void *data, fies_sz count)
{
	auto self = reinter<Reader*>(opaque);
	return self->read(data, count);
}

static int
vf_create(void       *opaque,
          const char *filename,
          fies_sz     filesize,
          uint32_t    mode,
          void      **out_fh)
{
	auto self = reinter<Reader*>(opaque);
	return self->create(filename, filesize, mode, out_fh);
}

static int
vf_mkdir(void       *opaque,
         const char *dirname,
         uint32_t    mode,
         void      **out_fh)
{
	auto self = reinter<Reader*>(opaque);
	return self->mkdir(dirname, mode, out_fh);
}

static int
vf_symlink(void       *opaque,
           const char *filename,
           const char *target,
           void      **out_fh)
{
	auto self = reinter<Reader*>(opaque);
	return self->symlink(filename, target, out_fh);
}

static int
vf_hardlink(void       *opaque,
            void       *src_fh,
            const char *filename)
{
	auto self = reinter<Reader*>(opaque);
	return self->hardlink(src_fh, filename);
}

static int
vf_mknod(void       *opaque,
         const char *filename,
         uint32_t    mode,
         uint32_t    major_id,
         uint32_t    minor_id,
         void      **out_fh)
{
	auto self = reinter<Reader*>(opaque);
	return self->mknod(filename, mode, major_id, minor_id, out_fh);
}

static int
vf_chown(void *opaque, void *fh, uid_t uid, gid_t gid)
{
	auto self = reinter<Reader*>(opaque);
	return self->chown(fh, uid, gid);
}

static int
vf_set_mtime(void *opaque, void *fh, struct fies_time time)
{
	auto self = reinter<Reader*>(opaque);
	return self->setMTime(fh, time);
}

static int
vf_set_xattr(void *opaque,
             void *fh,
             const char *name,
             const char *value,
             size_t length)
{
	auto self = reinter<Reader*>(opaque);
	return self->setXAttr(fh, name, value, length);
}

static int
vf_meta_end(void *opaque, void *fh)
{
	auto self = reinter<Reader*>(opaque);
	return self->metaEnd(fh);
}

static fies_ssz
vf_send(void *opaque, void *fh, fies_pos off, fies_sz len)
{
	auto self = reinter<Reader*>(opaque);
	return self->send(fh, off, len);
}

static fies_ssz
vf_pwrite(void       *opaque,
          void       *fh,
          const void *data,
          fies_sz     count,
          fies_pos    offset)
{
	auto self = reinter<Reader*>(opaque);
	return self->pwrite(fh, data, count, offset);
}

static int
vf_punch_hole(void *opaque, void *fh, fies_pos off, fies_sz len)
{
	auto self = reinter<Reader*>(opaque);
	return self->punchHole(fh, off, len);
}

static int
vf_clone(void    *opaque,
         void    *dest_fh,
         fies_pos dest_offset,
         void    *src_fh,
         fies_pos src_offset,
         fies_sz  length)
{
	auto self = reinter<Reader*>(opaque);
	return self->clone(dest_fh, dest_offset, src_fh, src_offset, length);
}

static int
vf_file_done(void *opaque, void *fh)
{
	auto self = reinter<Reader*>(opaque);
	return self->fileDone(fh);
}

static int
vf_close(void *opaque, void *fh)
{
	auto self = reinter<Reader*>(opaque);
	return self->close(fh);
}

static void
vf_finalize(void *opaque)
{
	auto self = reinter<Reader*>(opaque);
	return self->finalize();
}

const FiesReader_Funcs
cppreader_funcs = {
	vf_read,
	vf_create,
	vf_mkdir,
	vf_symlink,
	vf_hardlink,
	vf_mknod,
	vf_chown,
	vf_set_mtime,
	vf_set_xattr,
	vf_meta_end,
	vf_send,
	vf_pwrite,
	vf_punch_hole,
	vf_clone,
	vf_file_done,
	vf_close,
	vf_finalize,
};

#pragma clang diagnostic ignored "-Wunused-parameter"

Reader::Reader()
	: self_(nullptr)
{
	self_ = FiesReader_new(&cppreader_funcs, reinter<void*>(this));
}

Reader::~Reader() {
	FiesReader_delete(self_);
}

bool
Reader::readAll()
{
	int rc = FiesReader_readHeader(self_);
	if (!rc) {
		gotFlags(FiesReader_flags(self_));
		rc = 1;
	}
	while (rc > 0) {
		rc = FiesReader_iterate(self_);
	}
	if (rc < 0) {
		const char *emsg = FiesReader_getError(self_);
		if (!emsg)
			emsg = strerror(-rc);
		err("reader: %s\n", emsg);
	}
	return rc == 0;
}

int
Reader::gotFlags(uint32_t flags)
{
	return 0;
}

fies_ssz
Reader::read(void *data, fies_sz count)
{
	return -ENOTSUP;
}

int
Reader::create(const char *filename,
               fies_sz     filesize,
               uint32_t    mode,
               void      **out_fh)
{
	return -ENOTSUP;
}

int
Reader::mkdir(const char *dirname, uint32_t mode, void **out_fh)
{
	return -ENOTSUP;
}

int
Reader::symlink (const char *filename, const char *target, void **out_fh)
{
	return -ENOTSUP;
}

int
Reader::hardlink(void *src_fh, const char *filename)
{
	return -ENOTSUP;
}

int
Reader::mknod(const char *filename,
              uint32_t    mode,
              uint32_t    major_id,
              uint32_t    minor_id,
              void      **out_fh)
{
	return -ENOTSUP;
}

int
Reader::chown(void *fh, uid_t uid, gid_t gid)
{
	return -meta_err_;
}

int
Reader::setMTime(void *fh, struct fies_time time)
{
	return -meta_err_;
}

int
Reader::setXAttr(void *fh, const char *name, const char *value, size_t length)
{
	return -meta_err_;
}

int
Reader::metaEnd(void *fh)
{
	return -meta_err_;
}

fies_ssz
Reader::send(void *fh, fies_pos off, fies_sz len)
{
	return -ENOTSUP;
}

fies_ssz
Reader::pwrite(void *fh, const void *data, fies_sz count, fies_pos offset)
{
	return -ENOTSUP;
}

int
Reader::punchHole(void *fh, fies_pos off, fies_sz len)
{
	return -ENOTSUP;
}

int
Reader::clone(void    *dest_fh,
              fies_pos dest_offset,
              void    *src_fh,
              fies_pos src_offset,
              fies_sz  length)
{
	return -ENOTSUP;
}

int
Reader::fileDone(void *fh)
{
	return -meta_err_;
}

int
Reader::close(void *fh)
{
	return -ENOTSUP;
}

void
Reader::finalize()
{
}

