#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <sys/ioctl.h>

#include "fies.h"
#include "fies_writer.h"
#include "util.h"

#ifndef ENOATTR
# define ENOATTR ENODATA
#endif

static int
dev_t_cmp(const void *pa, const void *pb)
{
	const dev_t *a = pa, *b = pb;
	return *a < *b ? -1 :
	       *a > *b ?  1 : 0;
}

int
fies_id_cmp(const void *pa, const void *pb)
{
	const fies_id *a = pa, *b = pb;
	return *a < *b ? -1 :
	       *a > *b ?  1 : 0;
}

static void
FiesDevice_delete_p(void *ptr)
{
	FiesDevice *self = *(FiesDevice**)ptr;
	FiesEMap_delete(self->extents);
	if (self->is_osdev)
		Map_remove(&self->writer->osdevs, &self->osdev);
	free(self);
}

extern FiesFile*
FiesFile_new2(void *opaque,
              const struct FiesFile_Funcs *funcs,
              const char *filename,
              size_t filenamelen,
              const char *linkdest,
              size_t linkdestlen,
              fies_sz filesize,
              uint32_t mode,
              fies_id device)
{
	int err = 0;
	FiesFile *self = u_malloc0(sizeof(*self));
	if (!self)
		return NULL;
	self->opaque = opaque;
	self->funcs = funcs;
	self->filename = strndup(filename, filenamelen);
	if (!self->filename)
		goto out_errno;
	if (linkdest) {
		self->linkdest = strndup(linkdest, linkdestlen);
		if (!self->linkdest)
			goto out_errno;
	}
	self->filesize = filesize;
	self->mode = mode;
	self->device = device;
	return self;

out_errno:
	err = errno;
	free(self->linkdest);
	free(self->filename);
	free(self);
	errno = err;
	return NULL;
}

extern FiesFile*
FiesFile_new(void *opaque,
             const struct FiesFile_Funcs *funcs,
             const char *filename,
             const char *linkdest,
             fies_sz filesize,
             uint32_t mode,
             fies_id device)
{
	return FiesFile_new2(opaque, funcs,
	                     filename, strlen(filename),
	                     linkdest, linkdest ? strlen(linkdest) : 0,
	                     filesize, mode, device);
}

extern void
FiesFile_close(FiesFile *self)
{
	if (!self)
		return;
	if (self->funcs->close)
		self->funcs->close(self);
	free(self->filename);
	free(self->linkdest);
	free(self->uname);
	free(self->gname);
	free(self);
}

static int FiesWriter_writeHeader(FiesWriter *self);

extern FiesWriter*
FiesWriter_newFull(const struct FiesWriter_Funcs *funcs,
                   void *opaque,
                   uint32_t flags)
{
	if (!funcs || !funcs->writev) {
		errno = EINVAL;
		return NULL;
	}
	FiesWriter *self;
	self = u_malloc0(sizeof(*self));
	if (!self)
		return NULL;

	self->error = NULL;
	self->funcs = funcs;
	self->opaque = opaque;
	self->flags = flags;
	Vector_init(&self->free_devices, sizeof(fies_id), _Alignof(fies_id));
	self->next_device = 0;
	Map_init_type(&self->devices, fies_id_cmp,
	              fies_id, NULL,
	              FiesDevice*, FiesDevice_delete_p);
	Map_init_type(&self->osdevs, dev_t_cmp, dev_t, NULL, fies_id, NULL);

	int rc = FiesWriter_writeHeader(self);
	if (rc < 0) {
		FiesWriter_delete(self);
		errno = -rc;
		return NULL;
	}
	return self;
}

extern FiesWriter*
FiesWriter_new(const struct FiesWriter_Funcs *funcs, void *opaque)
{
	return FiesWriter_newFull(funcs, opaque, FIES_F_DEFAULT_FLAGS);
}

extern void
FiesWriter_delete(FiesWriter *self)
{
	if (!self)
		return;
	if (self->funcs->finalize)
		self->funcs->finalize(self->opaque);
	free(self->sendbuffer);
	Vector_destroy(&self->free_devices);
	Map_destroy(&self->devices);
	Map_destroy(&self->osdevs);
	free(self);
}

extern int
FiesWriter_setError(FiesWriter *self, int errc, const char *msg)
{
	self->error = msg;
	return -errc;
}

//
// Write functions
//

static int
FiesWriter_writev(FiesWriter *self,
                  const struct iovec *iov,
                  fies_sz count,
                  fies_sz checksize)
{
	if (!self->funcs->writev)
		return -ENOTSUP;
	fies_ssz put = self->funcs->writev(self->opaque, iov, count);
	if (put < 0)
		return (int)put;
	if ((fies_sz)put != checksize)
		return FiesWriter_setError(self, EIO, "short write");
	return 0;
}

static fies_ssz
FiesWriter_copy(FiesWriter *self,
                FiesFile *infd,
                fies_pos logical,
                fies_sz size,
                fies_pos physical)
{
	if (!(infd->funcs->pread || infd->funcs->preadp))
		return -ENOTSUP;

	if (!self->sendbuffer) {
		self->sendcapacity = 1*1024*1024;
		self->sendbuffer = malloc(self->sendcapacity);
	}

	fies_sz total = 0;
	while (size) {
		fies_sz step = size;
		if (step > self->sendcapacity)
			step = self->sendcapacity;
		fies_ssz got;
		if (infd->funcs->preadp) {
			got = infd->funcs->preadp(infd, self->sendbuffer, step,
		                                  logical, physical);
		} else {
			got = infd->funcs->pread(infd, self->sendbuffer, step,
		                                 logical);
		}
		if (got < 0)
			return got;
		logical += (fies_sz)got;
		physical += (fies_sz)got;
		// short writes error in FiesWriter_send()
		if ((fies_sz)got != step)
			return (fies_ssz)total;

		struct iovec iov = {
			self->sendbuffer,
			step
		};
		iov.iov_base = self->sendbuffer;
		fies_ssz put = self->funcs->writev(self->opaque, &iov, 1);
		if (put < 0)
			return put;
		total += (fies_sz)put;
		size -= (fies_sz)put;
		if ((fies_sz)put != step)
			break;
	}
	return (fies_ssz)total;
}

static int
FiesWriter_send(FiesWriter *self,
                FiesFile *infd,
                fies_pos logical,
                fies_sz size,
                fies_pos physical)
{
	fies_ssz put = -ENOTSUP;
	if (self->funcs->sendfile)
		put = self->funcs->sendfile(self->opaque, infd, logical, size);
	if (put == -ENOTSUP || put == -EOPNOTSUPP)
		put = FiesWriter_copy(self, infd, logical, size, physical);
	if (put < 0)
		return (int)put;
	if ((fies_sz)put != size)
		return FiesWriter_setError(self, EIO, "short write");
	return 0;
}

//
// Packet writing functions
//

static void
swap_fies_packet_le(struct fies_packet *pkt)
{
	SwapLE(pkt->type);
	SwapLE(pkt->size);
}

static int FIES_SENTINEL
FiesWriter_putPacket(FiesWriter *self, unsigned int type, ...)
{
	// FIXME: We know how many parameters we use the function with at most,
	// so stop using a Vector here.
	Vector iovs;
	Vector_init(&iovs, sizeof(struct iovec), _Alignof(struct iovec));

	struct fies_packet pkt = {
		.magic    = FIES_PACKET_HDR_MAGIC,
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
		.type     = type,
#pragma clang diagnostic pop
		.reserved = 0,
		.size     = sizeof(struct fies_packet)
	};

	struct iovec entry = {
		&pkt, sizeof(pkt)
	};

	Vector_push(&iovs, &entry);

	va_list ap;
	va_start(ap, type);
	while (1) {
		entry.iov_base = va_arg(ap, void*);
		if (!entry.iov_base)
			break;
		entry.iov_len = va_arg(ap, size_t);
		Vector_push(&iovs, &entry);
		pkt.size += entry.iov_len;
	}
	va_end(ap);

	swap_fies_packet_le(&pkt);

	int rc = FiesWriter_writev(self,
	                           Vector_data(&iovs), Vector_length(&iovs),
	                           pkt.size);
	Vector_destroy(&iovs);
	return rc;
}

//
// Protocol
//

static FiesDevice*
FiesWriter_createDevice(FiesWriter *self,
                        bool is_osdev,
                        dev_t osdev)
{
	fies_id id;
	if (Vector_length(&self->free_devices)) {
		id = *(const fies_id*)Vector_last(&self->free_devices);
		Vector_pop(&self->free_devices);
	} else {
		id = self->next_device++;
	}

	FiesDevice *dev = malloc(sizeof(*dev));
	dev->id = id;
	dev->writer = self;
	dev->extents = FiesEMap_new();
	dev->is_osdev = is_osdev;
	dev->osdev = osdev;
	Map_insert(&self->devices, &dev->id, &dev);
	return dev;
}

extern fies_id
FiesWriter_newDevice(FiesWriter *self)
{
	FiesDevice *dev = FiesWriter_createDevice(self, false, 0);
	return dev->id;
}

extern int
FiesWriter_getOSDevice(struct FiesWriter *self,
                       dev_t node,
                       fies_id *pid,
                       bool create)
{
	fies_id *eid = Map_get(&self->osdevs, &node);
	if (eid) {
		*pid = *eid;
		return 0;
	}
	if (!create)
		return -ENOENT;

	FiesDevice *dev = FiesWriter_createDevice(self, true, node);
	Map_insert(&self->osdevs, &node, &dev->id);
	*pid = dev->id;
	return 0;
}

extern int
FiesWriter_closeDevice(struct FiesWriter *self, fies_id id)
{
	if (!Map_remove(&self->devices, &id))
		return -ENOENT;
	Vector_push(&self->free_devices, &id);
	// FIXME: close all fies-files of the stream refering to this device?
	return 0;
}

static int
FiesWriter_sendFileHeader(FiesWriter *self,
                          FiesFile *fh,
                          fies_id fileid,
                          uint32_t mode,
                          const char *filename, size_t filenamelen,
                          const char *linkdest, size_t linkdestlen)
{
	struct fies_file file = {
		.id = FIES_LE(fileid),
		.mode = FIES_LE(mode),
		.size = FIES_LE((fies_pos)fh->filesize),
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
		.name_length = FIES_LE(filenamelen),
		.link_length = FIES_LE(linkdestlen),
#pragma clang diagnostic pop
		.reserved = 0,
	};

	const void *extra = linkdest;
	size_t extralen = linkdestlen;

	struct fies_file_device device;
	unsigned long ftype = fh->mode & FIES_M_FMT;

	if (ftype == FIES_M_FBLK || ftype == FIES_M_FCHR) {
		if (!fh->funcs->get_device)
			return -ENOTSUP;
		int rc = fh->funcs->get_device(fh,
		                               &device.major_id,
		                               &device.minor_id);
		if (rc < 0)
			return rc;
		SwapLE(device.major_id);
		SwapLE(device.minor_id);
		extra = &device;
		extralen = sizeof(device);
	}

	return FiesWriter_putPacket(self, FIES_PACKET_FILE,
	                            &file, sizeof(file),
	                            filename, filenamelen,
	                            extra, // NOTE: may be the sentinel!
	                            extralen,
	                            NULL);
}

static int
FiesWriter_sendMetaOwner(FiesWriter *self, FiesFile *file, fies_id fileid)
{
	struct fies_meta_owner data = {
		FIES_LE(file->uid),
		FIES_LE(file->gid)
	};

	struct fies_file_meta meta = {
		FIES_LE(FIES_META_OWNER),
		FIES_LE(fileid)
	};

	return FiesWriter_putPacket(self, FIES_PACKET_FILE_META,
	                            &meta, sizeof(meta),
	                            &data, sizeof(data),
	                            NULL);
}

static int
FiesWriter_sendMetaTime(FiesWriter *self, FiesFile *file, fies_id fileid)
{
	struct fies_file_meta meta = {
		FIES_LE(FIES_META_TIME),
		FIES_LE(fileid)
	};
	struct fies_time time = file->mtime;
	SwapLE(time.secs);
	SwapLE(time.nsecs);

	return FiesWriter_putPacket(self, FIES_PACKET_FILE_META,
	                            &meta, sizeof(meta),
	                            &time.secs, sizeof(time.secs),
	                            &time.nsecs, sizeof(time.nsecs),
	                            NULL);
}

static int
FiesWriter_sendMetaXAttrs(FiesWriter *self, FiesFile *file, fies_id fileid)
{
	if (!file->funcs->list_xattrs || !file->funcs->get_xattr)
		return 0;

	const char *xalist = NULL;
	ssize_t lstsize = file->funcs->list_xattrs(file, &xalist);
	if (lstsize <= 0)
		return (int)lstsize;

	struct fies_file_meta meta = {
		FIES_LE(FIES_META_XATTR),
		FIES_LE(fileid)
	};

	int retval = 0;
	const char *name = xalist;
	const char *end = xalist + lstsize;
	size_t namelen = 0;
	int nul = 0;
	for (; name < end; name += namelen+1) {
		namelen = strlen(name);
		if (!name || !namelen) // user gave us garbage
			continue;

		const char *data = NULL;
		ssize_t datalen = file->funcs->get_xattr(file, name, &data);
		if (datalen < 0) {
			// The xattr may have been removed just now:
			if (datalen == -ENODATA ||
			    datalen == -ENOATTR ||
			    datalen == -ENOENT)
			{
				continue;
			}

			// Otherwise this is an error:
			retval = (int)datalen;
			break;
		}
		if (!data)
			continue;

		struct fies_meta_xattr head = {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
			.name_length = FIES_LE(namelen),
			.value_length = FIES_LE(datalen)
#pragma clang diagnostic pop
		};

		retval = FiesWriter_putPacket(self, FIES_PACKET_FILE_META,
		                              &meta, sizeof(meta),
		                              &head, sizeof(head),
		                              name, namelen,
		                              &nul, (size_t)1,
		                              data, datalen,
		                              NULL);

		if (file->funcs->free_xattr)
			file->funcs->free_xattr(file, data);

		if (retval < 0)
			break;
	}

	if (file->funcs->free_xattr_list)
		file->funcs->free_xattr_list(file, xalist);

	return retval;
}

static int
FiesWriter_sendMetaEnd(FiesWriter *self, fies_id fileid)
{
	struct fies_file_meta meta = {
		FIES_LE(FIES_META_END),
		FIES_LE(fileid)
	};
	return FiesWriter_putPacket(self, FIES_PACKET_FILE_META,
	                            &meta, sizeof(meta), NULL);
}

static int
FiesWriter_sendFileMeta(FiesWriter *self,
                        FiesFile *file,
                        fies_id fileid,
                        const char *filename, size_t filenamelen,
                        const char *linkdest, size_t linkdestlen)
{
	int rc = FiesWriter_sendFileHeader(self, file, fileid, file->mode,
	                                   filename, filenamelen,
	                                   linkdest, linkdestlen);
	if (rc < 0)
		return rc;

	// FIXME: acls, ...

	rc = FiesWriter_sendMetaOwner(self, file, fileid);
	if (rc < 0 && rc != -ENOTSUP && rc != -EOPNOTSUPP)
		return rc;
	rc = FiesWriter_sendMetaTime(self, file, fileid);
	if (rc < 0 && rc != -ENOTSUP && rc != -EOPNOTSUPP)
		return rc;

	rc = FiesWriter_sendMetaXAttrs(self, file, fileid);
	if (rc < 0 && rc != -ENOTSUP && rc != -EOPNOTSUPP)
		return rc;

	rc = FiesWriter_sendMetaEnd(self, fileid);
	if (rc < 0)
		return rc;
	return 0;
}

static int
FiesWriter_sendFileEnd(FiesWriter *self, fies_id fileid)
{
	struct fies_file_end end = {
		FIES_LE(fileid)
	};
	return FiesWriter_putPacket(self, FIES_PACKET_FILE_END,
	                            &end, sizeof(end), NULL);
}

static int
FiesWriter_sendFileSnapshots(FiesWriter *self,
                             fies_id fileid,
                             const char **snapshots,
                             size_t count)
{
	if (count > 0xFFFF)
		return FiesWriter_setError(self, ERANGE, "too many snapshots");

	Vector data;
	Vector_init_type(&data, uint8_t);
	for (size_t i = 0; i != count; ++i) {
		size_t len = strlen(snapshots[i]);
		if (len > 0xFFFF) {
			Vector_destroy(&data);
			return FiesWriter_setError(self, ENAMETOOLONG,
			                           "snapshot name too long");
		}

		struct fies_snapshot_entry *entry;
		size_t size = sizeof(*entry) + len;
		entry = Vector_appendUninitialized(&data, size);
		entry->name_length = (uint16_t)len;
		memcpy(entry+1, snapshots[i], len);
	}
	struct fies_snapshot_list pkt = {
		.file = FIES_LE(fileid),
		.count = (uint16_t)FIES_LE(count),
		.reserved = 0
	};
	int rc = FiesWriter_putPacket(self, FIES_PACKET_SNAPSHOT_LIST,
	                              &pkt, sizeof(pkt),
	                              Vector_data(&data), Vector_length(&data),
	                              NULL);
	Vector_destroy(&data);
	return rc;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	FiesWriter *self;
	FiesFile *file;
	fies_id fileid;
	const FiesFile_Extent *ex;
	bool ref_file;
} FiesWriter_sendExtent_capture;
#pragma clang diagnostic pop

static int
FiesWriter_sendExtent_forNew(void *opaque,
                             fies_pos logical,
                             fies_sz len,
                             fies_pos physical)
{
	FiesWriter_sendExtent_capture *cap = opaque;
	if (cap->ref_file)
		return 0;

	const uint32_t in_flags = cap->ex->flags;
	uint32_t extype = in_flags & FIES_FL_EXTYPE_MASK;
	bool hasdata = false;
	switch (extype) {
	case FIES_FL_DATA:
		hasdata = true;
		break;
	case FIES_FL_ZERO:
	case FIES_FL_HOLE:
	case FIES_FL_HOLE|FIES_FL_ZERO:
		break;
	case FIES_FL_COPY:
		return FiesWriter_setError(cap->self, EINVAL,
		                           "explicit copy extent found");
	default:
		// no other combinations make sense currently
		return FiesWriter_setError(cap->self, EINVAL,
		                           "invalid extent type");
	}

	uint32_t flags = extype;

	struct fies_extent fex = {
		FIES_LE(cap->fileid),
		FIES_LE(flags),
		FIES_LE(logical),
		FIES_LE(len)
	};

	if (!hasdata)
		return FiesWriter_putPacket(cap->self, FIES_PACKET_EXTENT,
		                            &fex, sizeof(fex), NULL);

	struct fies_packet pkt = {
		.magic    = FIES_PACKET_HDR_MAGIC,
		.type     = FIES_PACKET_EXTENT,
		.reserved = 0,
		.size     = sizeof(struct fies_packet) + sizeof(fex) + len
	};
	swap_fies_packet_le(&pkt);

	struct iovec iov[2] = {
		{ &pkt, sizeof(pkt) },
		{ &fex, sizeof(fex) }
	};
	int rc = FiesWriter_writev(cap->self, iov, 2, sizeof(pkt)+sizeof(fex));
	if (rc < 0)
		return rc;
	rc = FiesWriter_send(cap->self, cap->file, logical, len, physical);

	if (rc < 0)
		return rc;

	return 0;
}

static inline void
swap_fies_extent_le(struct fies_extent *fex)
{
	SwapLE(fex->file);
	SwapLE(fex->flags);
	SwapLE(fex->offset);
	SwapLE(fex->length);
}

static int
FiesWriter_sendExtent_forAvail(void *opaque, fies_pos pos, fies_sz len,
                               fies_id src_file, fies_pos src_pos)
{
	FiesWriter_sendExtent_capture *cap = opaque;
	if (cap->ref_file)
		return 0;

	struct fies_extent fex = { cap->fileid, FIES_FL_COPY, pos, len };
	struct fies_source src = { src_file, src_pos };
	swap_fies_extent_le(&fex);
	SwapLE(src.file);
	SwapLE(src.offset);
	return FiesWriter_putPacket(cap->self, FIES_PACKET_EXTENT,
	                            &fex, sizeof(fex),
	                            &src, sizeof(src),
	                            NULL);
}

static ssize_t
FiesWriter_sendHole(FiesWriter *self,
                    fies_id fileid,
                    fies_pos pos,
                    fies_sz length,
                    fies_sz filesize)
{
	// sanity check - this should never happen
	if (pos + length > filesize)
		return FiesWriter_setError(self, EINVAL,
		                           "hole tracked past EOF");
	struct fies_extent fex = { fileid, FIES_FL_HOLE, pos, length };
	swap_fies_extent_le(&fex);
	return FiesWriter_putPacket(self, FIES_PACKET_EXTENT,
	                            &fex, sizeof(fex),
	                            NULL);
}

static ssize_t
FiesWriter_sendExtent(FiesWriter *self,
                      FiesFile *file,
                      fies_id fileid,
                      FiesFile_Extent *ex,
                      size_t filesize,
                      FiesDevice *device,
                      bool ref_file)
{
	if (ex->logical + ex->length > filesize)
		ex->length = filesize - ex->logical;

	FiesWriter_sendExtent_capture cap = {
		self, file, fileid, ex, ref_file
	};

	int rc;
	if (ex->flags & FIES_FL_COPY) {
		// User requested to explicitly copy from a file
		rc = FiesWriter_sendExtent_forAvail(&cap, ex->logical,
		                                    ex->length,
		                                    ex->source.file,
		                                    ex->source.offset);
	}
	else if (!(ex->flags & FIES_FL_SHARED)) {
		// This extent is not marked as shared, so we don't even try.
		rc = FiesWriter_sendExtent_forNew(&cap, ex->logical,
		                                  ex->length, ex->physical);
	}
	else {
		rc = FiesEMap_add(device->extents,
		                  ex->device,
		                  ex->physical, ex->logical, ex->length,
		                  fileid,
		                  &FiesWriter_sendExtent_forNew,
		                  &FiesWriter_sendExtent_forAvail,
		                  &cap);
	}
	if (rc < 0)
		return rc;
	return (ssize_t)ex->length;
}

static fies_id
FiesWriter_registerFile(FiesWriter *self, FiesFile *file)
{
	file->fileid = self->next_fileid++;
	return file->fileid;
}

extern int
FiesWriter_writefd(FiesWriter *self,
                   int fd,
                   const char *filename,
                   unsigned int flags)
{
	FiesFile *file = FiesFile_fdopen(fd, filename, self, flags);
	if (!file)
		return -errno;
	int rc = FiesWriter_writeFile(self, file);
	FiesFile_close(file);
	return rc;
}

extern int
FiesWriter_writeOSFile(FiesWriter *self,
                       const char *filename,
                       unsigned int flags)
{
	FiesFile *file = FiesFile_open(filename, self, flags);
	if (!file)
		return -errno;
	int rc = FiesWriter_writeFile(self, file);
	FiesFile_close(file);
	return rc;
}

static int
FiesWriter_writeHeader(FiesWriter *self)
{
	if (self->flags & FIES_F_RAW)
		return 0;

	struct fies_header hdr = {
		.magic = FIES_HDR_MAGIC,
		.version = FIES_VERSION,
		.flags = self->flags
	};
	SwapLE(hdr.version);
	SwapLE(hdr.flags);

	struct iovec iov = {
		.iov_base = &hdr,
		.iov_len = sizeof(hdr)
	};
	int rc = FiesWriter_writev(self, &iov, 1, sizeof(hdr));
	if (rc < 0)
		return rc;

	self->flags |= FIES_F_RAW;
	return 0;
}

static inline size_t
merge_extents(FiesFile_Extent *ex, size_t pi, size_t count)
{
	size_t i = pi;
	while (i+1 != count &&
	       ex[i+1].flags == ex[i].flags &&
	       ex[i+1].device == ex[i].device &&
	       ex[i+1].physical == ex[i].physical + ex[i].length &&
	       ex[i+1].logical  == ex[i].logical  + ex[i].length)
	{
		ex[pi].length += ex[i+1].length;
		++i;
	}
	return i;
}

static int
FiesWriter_writeFileDo(FiesWriter *self, FiesFile *file, bool ref_file)
{
	if (!file->funcs)
		return FiesWriter_setError(self, EINVAL,
		                           "file without function callbacks");
	if (!self->funcs->writev)
		return FiesWriter_setError(self, ENOSYS,
		                           "no write callback available");

	if (ref_file && file->linkdest)
		return FiesWriter_setError(self, EINVAL,
			"symbolic link cannot be a ref file");

	FiesDevice *device = PMap_get(&self->devices, &file->device);
	if (!device) {
		// Should not be possible
		return FiesWriter_setError(self, EFAULT,
		       "file is not properly associated with a device");
	}

	unsigned long filetype = file->mode & FIES_M_FMT;
	if (!filetype)
		return FiesWriter_setError(self, EINVAL, "invalid file type");
	if (ref_file && !FIES_M_HAS_EXTENTS(filetype))
		return FiesWriter_setError(self, EINVAL,
			"only files with extents can be reference files");
	if (file->linkdest && filetype != FIES_M_FLNK)
		return FiesWriter_setError(self, ELOOP,
		                           "non-link must not have a link");
	else if (!file->linkdest && filetype == FIES_M_FLNK)
		return FiesWriter_setError(self, ELOOP,
		                           "missing symbolic link target");

	size_t filenamelen = strlen(file->filename);
	if (filenamelen > 0xFFFF)
		return FiesWriter_setError(self, ENAMETOOLONG,
		                           "filename too long");
	size_t linkdestlen = file->linkdest ? strlen(file->linkdest) : 0;
	if (linkdestlen > 0xFFFF)
		return FiesWriter_setError(self, ENAMETOOLONG,
		                           "symlink destination too long");

	if (filetype == FIES_M_FHARD) {
		return FiesWriter_sendFileHeader(self, file, file->fileid,
		                                 file->mode,
		                                 file->filename, filenamelen,
		                                 NULL, 0);
	}

	int retval;

	fies_id fileid = FiesWriter_registerFile(self, file);

	if (ref_file) {
		retval = FiesWriter_sendFileHeader(self, file, file->fileid,
		                                   FIES_M_FREF,
		                                   file->filename, filenamelen,
		                                   NULL, 0);
	} else {
		retval = FiesWriter_sendFileMeta(self, file, fileid,
		                                 file->filename, filenamelen,
		                                 file->linkdest, linkdestlen);
	}
	if (retval < 0)
		return retval;

	if (!FIES_M_HAS_EXTENTS(file->mode))
		return FiesWriter_sendFileEnd(self, fileid);

	if (!file->funcs->next_extents)
		return FiesWriter_setError(self, ENOTSUP,
		                           "cannot map extents of file");

	const size_t capacity = 8*1024;
	FiesFile_Extent *exbuf = malloc(capacity * sizeof(*exbuf));

	const fies_sz filesize = file->filesize;
	fies_pos at = 0;
	retval = 0;
	while (at != filesize) {
		ssize_t count = file->funcs->next_extents(file, self,
		                                          at, exbuf,
		                                          capacity);
		if (count < 0) {
			retval = (int)count;
			break;
		}
		if (!count)
			break;

		for (size_t i = 0; i != (size_t)count; ++i) {
			FiesFile_Extent *ex = &exbuf[i];
			i = merge_extents(exbuf, i, (size_t)count);
			if (ref_file)
				at = ex->logical;
			else if (ex->logical > at) {
				fies_pos len = ex->logical - at;
				fies_ssz rc = FiesWriter_sendHole(self, fileid,
				                                  at, len,
				                                  filesize);
				if (rc < 0) {
					retval = (int)rc;
					break;
				}
				at += len;
			}
			fies_ssz rc = FiesWriter_sendExtent(self, file, fileid,
			                                    ex, filesize,
			                                    device, ref_file);
			if (rc < 0) {
				retval = (int)rc;
				break;
			}
			at += (fies_sz)rc;
		}

		if (retval < 0)
			break;

		if (at > filesize) {
			retval = -EOVERFLOW;
			break;
		}
	}
	if (!ref_file && at < filesize) {
		fies_ssz rc = FiesWriter_sendHole(self, fileid, at,
		                                  filesize-at, filesize);
		if (rc < 0)
			retval = (int)rc;
	}
	if (retval < 0)
		return retval;
	retval = FiesWriter_sendFileEnd(self, fileid);
/*out:*/
	free(exbuf);
	return retval;
}

extern int
FiesWriter_writeFile(FiesWriter *self, FiesFile *file)
{
	return FiesWriter_writeFileDo(self, file, false);
}

extern int
FiesWriter_readRefFile(FiesWriter *self, FiesFile *file)
{
	return FiesWriter_writeFileDo(self, file, true);
}

extern int
FiesWriter_snapshots(struct FiesWriter *self,
                     struct FiesFile *file,
                     const char **snapshots,
                     size_t count)
{
	return FiesWriter_sendFileSnapshots(self, file->fileid,
	                                    snapshots, count);
}

extern const char*
FiesWriter_getError(const FiesWriter *self)
{
	return self->error;
}
