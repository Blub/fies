#include <string.h>

#include "cppfile.h"

static ssize_t
vf_pread(struct FiesFile *handle,
         void *buffer,
         size_t length,
         fies_pos offset)
{
	auto self = reinter<File*>(handle->opaque);
	return self->pread(buffer, length, offset);
}

static ssize_t
vf_preadp(struct FiesFile *handle,
          void *buffer,
          size_t length,
          fies_pos offset,
          fies_pos physical)
{
	auto self = reinter<File*>(handle->opaque);
	return self->preadp(buffer, length, offset, physical);
}

static void
vf_close(struct FiesFile *handle)
{
	auto self = reinter<File*>(handle->opaque);
	return self->close();
}

static ssize_t
vf_next_extents(struct FiesFile *handle,
                struct FiesWriter *writer,
                fies_pos logical_start,
                struct FiesFile_Extent *buffer,
                size_t bufsize)
{
	auto self = reinter<File*>(handle->opaque);
	return self->nextExtents(writer, logical_start, buffer, bufsize);
}

static int
vf_verify_extent(struct FiesFile *handle,
                 struct FiesWriter *writer,
                 const struct FiesFile_Extent *extent)
{
	auto self = reinter<File*>(handle->opaque);
	return self->verifyExtent(writer, extent);
}

static int
vf_get_os_fd(struct FiesFile *handle)
{
	auto self = reinter<File*>(handle->opaque);
	return self->getOSFD();
}

static int
vf_get_device(struct FiesFile *handle, uint32_t *out_maj, uint32_t *out_min)
{
	auto self = reinter<File*>(handle->opaque);
	return self->getDevice(out_maj, out_min);
}

static ssize_t
vf_list_xattrs(struct FiesFile *handle, const char **names)
{
	auto self = reinter<File*>(handle->opaque);
	return self->listXAttrs(names);
}

static void
vf_free_xattr_list(struct FiesFile *handle, const char *names)
{
	auto self = reinter<File*>(handle->opaque);
	return self->freeXAttrList(names);
}

static ssize_t
vf_get_xattr(struct FiesFile *handle, const char *name, const char **buffer)
{
	auto self = reinter<File*>(handle->opaque);
	return self->getXAttr(name, buffer);
}

static void
vf_free_xattr(struct FiesFile *handle, const char *buffer)
{
	auto self = reinter<File*>(handle->opaque);
	return self->freeXAttr(buffer);
}

const struct FiesFile_Funcs
virt_file_funcs = {
	vf_pread,
	vf_preadp,
	vf_close,
	vf_next_extents,
	vf_verify_extent,
	vf_get_os_fd,
	vf_get_device,
	vf_list_xattrs,
	vf_free_xattr_list,
	vf_get_xattr,
	vf_free_xattr
};

#pragma GCC diagnostic ignored "-Wunused-parameter"

File::~File() {
}

ssize_t
File::pread(void *buffer, size_t length, fies_pos offset)
{
	return -ENOTSUP;
}

ssize_t
File::preadp(void *buffer, size_t length, fies_pos offset, fies_pos physical)
{
	return -ENOTSUP;
}

void
File::close()
{
}

ssize_t
File::nextExtents(FiesWriter*,
                            fies_pos logical_start,
                            FiesFile_Extent *buffer,
                            size_t buffer_elements)
{
	return -ENOTSUP;
}

int
File::verifyExtent(FiesWriter*, const FiesFile_Extent*)
{
	return -ENOTSUP;
}

int
File::getOSFD()
{
	return -ENOTSUP;
}

int
File::getDevice(uint32_t *out_maj, uint32_t *out_min)
{
	return -ENOTSUP;
}

ssize_t
File::listXAttrs(const char **names)
{
	return -ENOTSUP;
}

void
File::freeXAttrList(const char *names)
{
}

ssize_t
File::getXAttr(const char *name, const char **buffer)
{
	return -ENOTSUP;
}

void
File::freeXAttr(const char *buffer)
{
}
