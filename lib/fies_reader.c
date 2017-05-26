#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdnoreturn.h>

#include "fies.h"
#include "fies_reader.h"
#include "util.h"

static FiesReader_File*
FiesReader_File_new(FiesReader *reader,
                    fies_id id,
                    char *filename,
                    char *linkdest,
                    fies_sz size,
                    uint32_t mode,
                    void *opaque)
{
	FiesReader_File *self = malloc(sizeof(*self));
	if (!self)
		return NULL;
	self->id = id;
	self->filename = filename;
	self->linkdest = linkdest;
	self->size = size;
	self->mode = mode;
	self->opaque = opaque;
	self->reader = reader;
	return self;
}

static void
FiesReader_File_destroy_p(void *pself)
{
	FiesReader_File *self = *(void**)pself;
	FiesReader *reader = self->reader;
	if (reader->funcs->close && self->opaque)
		reader->funcs->close(reader->opaque, self->opaque);
	free(self->filename);
	free(self->linkdest);
	free(self);
}

extern FiesReader*
FiesReader_newFull(const struct FiesReader_Funcs *funcs,
                   void *opaque,
                   uint32_t required_flags,
                   uint32_t rejected_flags)
{
	if (!funcs || !funcs->read) {
		errno = EINVAL;
		return NULL;
	}
	FiesReader *self = u_malloc0(sizeof(*self));
	if (!self)
		return NULL;

	self->funcs = funcs;
	self->opaque = opaque;

	Map_init_type(&self->files, fies_id_cmp,
	              fies_id, NULL,
	              FiesReader_File*, FiesReader_File_destroy_p);

	self->buffer.capacity = 128*1024;
	self->buffer.data = malloc(self->buffer.capacity);

	self->state = FR_State_Header;
	self->hdr_flags_required = required_flags;
	self->hdr_flags_rejected = rejected_flags;

	return self;
}

extern FiesReader*
FiesReader_new(const struct FiesReader_Funcs *funcs, void *opaque)
{
	return FiesReader_newFull(funcs, opaque, FIES_F_DEFAULT_FLAGS, 0);
}

extern void
FiesReader_delete(FiesReader *self)
{
	if (!self)
		return;
	if (self->funcs->finalize)
		self->funcs->finalize(self->opaque);
	Map_destroy(&self->files);
	free(self->buffer.data);
	free(self);
}

static inline noreturn void
FiesReader_return(FiesReader *self)
{
	longjmp(self->jmpbuf, 1);
}

static inline noreturn void
FiesReader_throw(FiesReader *self, int err, const char *msg)
{
	self->errstr = msg;
	self->errc = -err;
	FiesReader_return(self);
}

static void
FiesReader_shiftBuffer(FiesReader *self)
{
	if (self->buffer.at) {
		memcpy(self->buffer.data,
		       FiesReader_data(self),
		       FiesReader_filled(self));
		self->buffer.filled -= self->buffer.at;
		self->buffer.at = 0;
	}
}

static void
FiesReader_eat(FiesReader *self, size_t cnt, FiesReader_State next_state)
{
	if (cnt > FiesReader_filled(self))
		FiesReader_throw(self, EFAULT, "buffer underflow");
	self->buffer.at += cnt;
	self->state = next_state;
}

static ssize_t
FiesReader_bufferSome(FiesReader *self, size_t len, bool fail_short)
{
	if (FiesReader_filled(self) >= len)
		return 0;

	// by design we should never have to exceed the buffer...
	if (self->buffer.filled + len > self->buffer.capacity)
		FiesReader_shiftBuffer(self);
	if (FiesReader_filled(self) + len > self->buffer.capacity) {
		// If we don't want to fail on short requests, let's just cap
		// it with our buffer size.
		if (fail_short)
			FiesReader_throw(self, ENOBUFS,
			                 "exceeding maximum buffer capacity");
		len = self->buffer.capacity - self->buffer.filled;
	}

	size_t need = len - FiesReader_filled(self);
	if (!self->funcs->read)
		FiesReader_throw(self, ENOTSUP, "no read callback available");
	fies_ssz rc = self->funcs->read(self->opaque,
	                                self->buffer.data+self->buffer.filled,
	                                need);
	if (rc < 0) {
		if (rc == -EAGAIN || rc == -EWOULDBLOCK)
			return rc;
		FiesReader_throw(self, (int)-rc, "read error");
	}
	if (rc == 0) {
		self->eof = true;
		FiesReader_return(self);
	}
	if ((size_t)rc > need)
		FiesReader_throw(self, EIO, "read callback misbehaved");

	self->buffer.filled += (size_t)rc;
	if (fail_short && (size_t)rc < need)
		return -EAGAIN;
	return rc;
}

static int
FiesReader_bufferAtLeast(FiesReader *self, size_t len)
{
	ssize_t got = FiesReader_bufferSome(self, len, true);
	return got < 0 ? (int)got : 0;
}

static int
FiesReader_newFileCreate(FiesReader *self)
{
	const struct fies_file *file = FiesReader_data(self);
	const size_t filesize = FIES_LE(file->size);
	const uint16_t namelen = FIES_LE(file->name_length);
	const uint16_t linklen = FIES_LE(file->link_length);
	const fies_id fileid = FIES_LE(file->id);
	const uint32_t mode = FIES_LE(file->mode);

	unsigned long filetype = mode & FIES_M_FMT;

	size_t maxlen = self->pkt_size - sizeof(*file);

	// sanity checks:
	FiesReader_File *existingfile = PMap_get(&self->files, &fileid);
	if (existingfile && filetype != FIES_M_FHARD)
		FiesReader_throw(self, EINVAL, "duplicate file id");
	if (filetype == FIES_M_FHARD && !existingfile)
		FiesReader_throw(self, EINVAL, "hardlink to unknown file");

	size_t extralen = 0;
	if (filetype != FIES_M_FLNK && linklen)
		FiesReader_throw(self, EINVAL, "non-symlink with link length");

	switch (filetype) {
	case FIES_M_FLNK:
		extralen = linklen;
		if (!linklen)
			FiesReader_throw(self, EINVAL,
			                 "symlink packet without target");
		if (strnlen(file->name+namelen, linklen) != linklen)
			FiesReader_throw(self, EINVAL,
			                 "link target length mismatch");
		break;
	case FIES_M_FCHR:
	case FIES_M_FBLK:
	case FIES_M_FIFO:
		extralen = sizeof(struct fies_file_device);
		break;
	case FIES_M_FREG:
	case FIES_M_FHARD:
	case FIES_M_FDIR:
	case FIES_M_FREF:
		extralen = 0;
		break;
	default:
		FiesReader_throw(self, EFAULT, "TODO: special files");
	}

	if (namelen+extralen > maxlen)
		FiesReader_throw(self, EINVAL, "truncated file packet");
	if (strnlen(file->name, namelen) != namelen)
		FiesReader_throw(self, EINVAL, "file name length mismatch");
	const void *extra = file->name + extralen;

	int rc;
	void *handle = NULL;
	char *filename = strndup(file->name, namelen);
	char *linkdest = NULL;
	bool add_handle = true;
	bool expect_meta = true;
	switch (filetype) {
	case FIES_M_FREF:
		expect_meta = false;
		if (!self->funcs->reference)
			goto notsup;
		rc = self->funcs->reference(self->opaque, filename,
		                            filesize, mode, &handle);
		break;
	case FIES_M_FREG:
		if (!self->funcs->create)
			goto notsup;
		rc = self->funcs->create(self->opaque, filename,
		                         filesize, mode, &handle);
		break;
	case FIES_M_FHARD:
		add_handle = false;
		if (!self->funcs->hardlink || !existingfile->opaque)
			goto notsup;
		rc = self->funcs->hardlink(self->opaque, existingfile->opaque,
		                           filename);
		break;
	case FIES_M_FDIR:
		if (!self->funcs->mkdir)
			goto notsup;
		rc = self->funcs->mkdir(self->opaque, filename, mode, &handle);
		break;
	case FIES_M_FLNK:
		if (!self->funcs->symlink)
			goto notsup;
		linkdest = strndup(file->name+namelen, linklen);
		rc = self->funcs->symlink(self->opaque, filename, linkdest,
		                          &handle);
		break;
	case FIES_M_FCHR:
	case FIES_M_FBLK:
	case FIES_M_FIFO: {
		if (!self->funcs->mknod)
			goto notsup;
		const struct fies_file_device *dev = extra;
		rc = self->funcs->mknod(self->opaque, filename, mode,
		                        dev->major_id, dev->minor_id,
		                        &handle);
		break;
	}
	default:
		free(filename);
		FiesReader_throw(self, EFAULT, "TODO: special files (2)");
	}
	if (rc < 0) {
		free(filename);
		free(linkdest);
		FiesReader_throw(self, -rc, "failed to create file");
	}

	if (add_handle) {
		FiesReader_File *entry = FiesReader_File_new(self, fileid,
		                                             filename, linkdest,
		                                             filesize, mode,
		                                             handle);
		if (!entry) {
			int err = errno;
			free(filename);
			free(linkdest);
			FiesReader_throw(self, err, "allocation failed");
		}

		Map_insert(&self->files, &entry->id, &entry);
		if (expect_meta)
			self->newfile = entry;
	} else {
		self->newfile = NULL;
	}
	FiesReader_eat(self, self->pkt_size, FR_State_Begin);

	return 0;
notsup:
	free(filename);
	FiesReader_throw(self, ENOTSUP, "failed to create file");
}

static int
FiesReader_getNewFile(FiesReader *self)
{
	int rc = FiesReader_bufferAtLeast(self, self->pkt_size);
	if (rc < 0)
		return rc;
	self->state = FR_State_NewFile_Create;
	return FiesReader_newFileCreate(self);
}

#if 0
static int
FiesReader_fileClose(FiesReader *self)
{
	const struct fies_file_end *end = FiesReader_data(self);

	if (!Map_remove(&self->files, &end->file)) // impossible
		FiesReader_throw(self, EFAULT,
		                 "file for meta packet disappeared");

	FiesReader_eat(self, self->pkt_size, FR_State_Begin);
	return 0;
}
#endif

static int
FiesReader_getFileEnd(FiesReader *self)
{
	const struct fies_file_end *end = FiesReader_data(self);
	int rc = FiesReader_bufferAtLeast(self, self->pkt_size);
	if (rc < 0)
		return rc;

	FiesReader_File *file = PMap_get(&self->files, &end->file);
	if (!file) // impossible
		FiesReader_throw(self, EFAULT,
		                 "file for meta packet disappeared");

	if (self->funcs->file_done && file->opaque) {
		rc = self->funcs->file_done(self->opaque, file->opaque);
		if (rc == -EAGAIN || rc == -EWOULDBLOCK)
			return rc;
		if (rc < 0 && (rc != -ENOTSUP && rc != -EOPNOTSUPP))
			FiesReader_throw(self, -rc, "error finishing file");
	}

#if 0
	if (!FIES_M_HAS_EXTENTS(file->mode)) {
		self->state = FR_State_FileClose;
		return FiesReader_fileClose(self);
	}
#endif

	FiesReader_eat(self, self->pkt_size, FR_State_Begin);
	return 0;
}

static inline void
FiesReader_assertMetaSize(FiesReader *self, size_t size)
{
	if (self->pkt_size != sizeof(struct fies_file_meta) + size)
		FiesReader_throw(self, EINVAL, "meta packet of invalid size");
}

static int
FiesReader_handleFileMeta(FiesReader *self)
{
	int retval = 0;
	const struct fies_file_meta *pmeta = FiesReader_data(self);
	struct fies_file_meta meta = *pmeta;
	SwapLE(meta.type);
	SwapLE(meta.file);

	if (meta.file != self->newfile->id)
		FiesReader_throw(self, EINVAL, "meta packet with bad file id");

	FiesReader_File *file = PMap_get(&self->files, &meta.file);
	if (!file) // impossible
		FiesReader_throw(self, EFAULT,
		                 "file for meta packet disappeared");

	switch (meta.type) {
	case FIES_META_END:
		if (self->funcs->meta_end && file->opaque)
			retval = self->funcs->meta_end(self->opaque,
			                               file->opaque);
		self->newfile = NULL;
		goto done;
	default:
		if (meta.type >= FIES_META_CUSTOM)
			goto done;
	case FIES_META_INVALID:
		FiesReader_throw(self, EINVAL, "bad meta packet");

	case FIES_META_OWNER: {
		const struct fies_meta_owner *owner = (const void*)(pmeta+1);
		FiesReader_assertMetaSize(self, sizeof(*owner));
		if (!self->funcs->chown || !file->opaque)
			break;
		retval = self->funcs->chown(self->opaque, file->opaque,
		                            FIES_LE(owner->uid),
		                            FIES_LE(owner->gid));
		break;
	}
	case FIES_META_TIME: {
		FiesReader_assertMetaSize(self, FIES_META_TIME_SIZE);
		if (!self->funcs->set_mtime || !file->opaque)
			break;
		const struct fies_time *ptime = (const void*)(pmeta+1);
		struct fies_time time = *ptime;
		SwapLE(time.secs);
		SwapLE(time.nsecs);
		retval = self->funcs->set_mtime(self->opaque, file->opaque,
		                                time);
		break;
	}
	case FIES_META_XATTR: {
		const struct fies_meta_xattr *xa = (const void*)(pmeta+1);
		size_t namelen = (size_t)FIES_LE(xa->name_length);
		size_t valuelen = (size_t)FIES_LE(xa->value_length);
		size_t datasize = sizeof(*xa) + namelen + 1 + valuelen;
		FiesReader_assertMetaSize(self, datasize);
		const char *name = xa->data;
		if (name[namelen] || strlen(name) != namelen)
			FiesReader_throw(self, EINVAL, "bad xattr meta data");
		if (!self->funcs->set_xattr || !file->opaque)
			break;
		retval = self->funcs->set_xattr(self->opaque, file->opaque,
		                                name,
		                                name+namelen+1, valuelen);
		break;
	}
	}

done:
	FiesReader_eat(self, self->pkt_size, FR_State_Begin);
	return retval;
}

static int
FiesReader_getFileMeta(FiesReader *self)
{
	int rc = FiesReader_bufferAtLeast(self, self->pkt_size);
	if (rc < 0)
		return rc;
	self->state = FR_State_FileMeta_Do;
	return FiesReader_handleFileMeta(self);
}

static fies_ssz
FiesReader_pwrite(FiesReader *self,
                  void *fd,
                  const void *buf,
                  fies_sz size,
                  fies_pos off)
{
	if (!fd)
		return (fies_ssz)size;
	if (!self->funcs->pwrite)
		FiesReader_throw(self, ENOTSUP, "no write callback available");
	fies_ssz put = self->funcs->pwrite((void*)self->opaque,
	                                   fd, buf, size, off);
	if (put >= 0)
		return put;
	if (put == -EAGAIN || put == -EWOULDBLOCK)
		return put;
	FiesReader_throw(self, (int)-put, "write error");
}

static int
FiesReader_zeroExtent(FiesReader *self)
{
	FiesReader_File *file = PMap_get(&self->files, &self->extent.file);
	if (!file)
		FiesReader_throw(self, EFAULT,
		                 "dropped file for current extent");

	fies_sz remaining = self->extent.length - self->extent_at;
	fies_pos offset = self->extent.offset + self->extent_at;

	fies_ssz put = FiesReader_pwrite(self, file->opaque,
	                                 NULL, remaining, offset);
	if (put < 0)
		return (int)put;

	if ((fies_sz)put == remaining)
		self->state = FR_State_Begin;
	else
		self->extent_at += (fies_sz)put;
	return 0;
}

static int
FiesReader_punchHole(FiesReader *self)
{
	FiesReader_File *file = PMap_get(&self->files, &self->extent.file);
	if (!file)
		FiesReader_throw(self, EFAULT,
		                 "dropped file for current extent");

	if (!file->opaque) {
		self->state = FR_State_Begin;
		return 0;
	}

	if (!self->funcs->punch_hole)
		FiesReader_throw(self, ENOTSUP,
		                 "no punch-hole callback available");
	int rc = self->funcs->punch_hole(self->opaque, file->opaque,
	                                 self->extent.offset,
	                                 self->extent.length);
	if (rc < 0)
		FiesReader_throw(self, -rc, "failed to punch hole");

	self->state = FR_State_Begin;
	return 0;
}

static fies_ssz
FiesReader_send(FiesReader *self, void *fd, fies_pos off, fies_sz size)
{
	if (!fd)
		return -ENOTSUP;
	if (!self->funcs->send)
		FiesReader_throw(self, EFAULT, "tried to use send-callback");
	fies_ssz put = self->funcs->send(self->opaque, fd, off, size);
	if (put >= 0 ||
	    put == -EAGAIN || put == -EWOULDBLOCK ||
	    put == -ENOTSUP || put == -EOPNOTSUPP)
	{
		return put;
	}
	FiesReader_throw(self, (int)-put, "write error");
}

static int
FiesReader_clone(FiesReader *self,
                 void *dst, fies_pos dstoff,
                 void *src, fies_pos srcoff,
                 fies_sz len)
{
	if (!dst)
		return 0;
	if (!self->funcs->clone)
		FiesReader_throw(self, ENOTSUP, "no clone callback available");
	int rc = self->funcs->clone(self->opaque,
	                            dst, dstoff, src, srcoff, len);
	if (rc < 0)
		FiesReader_throw(self, -rc, "clone error");
	return rc;
}

static int
FiesReader_getExtentCopyInfo(FiesReader *self)
{
	int rc = FiesReader_bufferAtLeast(self, sizeof(struct fies_source));
	if (rc < 0)
		return rc;

	FiesReader_File *file = PMap_get(&self->files, &self->extent.file);
	if (!file)
		FiesReader_throw(self, EFAULT,
		                 "dropped file for current extent");

	const struct fies_source *psource = FiesReader_data(self);
	struct fies_source source = *psource;
	SwapLE(source.file);
	SwapLE(source.offset);
	FiesReader_File *srcfile = PMap_get(&self->files, &source.file);
	if (!srcfile)
		FiesReader_throw(self, EFAULT,
		                 "clone from an unknown file id");

	rc = FiesReader_clone(self,
	                      file->opaque, self->extent.offset,
	                      srcfile->opaque, source.offset,
	                      self->extent.length);
	if (rc < 0)
		return rc;

	FiesReader_eat(self, sizeof(source), FR_State_Begin);

	return 0;
}

static int
FiesReader_readExtent(FiesReader *self)
{
	FiesReader_File *file = PMap_get(&self->files, &self->extent.file);
	if (!file)
		FiesReader_throw(self, EFAULT,
		                 "dropped file for current extent");

	fies_sz remaining = self->extent.length - self->extent_at;
	fies_pos offset = self->extent.offset + self->extent_at;

	if (!self->funcs->send || !file->opaque) {
	 send_not_supported:{}
		ssize_t got = FiesReader_bufferSome(self, remaining, false);
		if (got < 0)
			return (int)got;
		if (!FiesReader_filled(self))
			FiesReader_throw(self, EFAULT, "buffering failed");
	}

	if (FiesReader_filled(self)) {
		size_t has = FiesReader_filled(self);
		if (has > remaining)
			has = remaining;
		fies_ssz put = FiesReader_pwrite(self, file->opaque,
		                                 FiesReader_data(self),
		                                 has, offset);
		if (put < 0)
			return (int)put;
		const fies_sz zput = (fies_sz)put;
		if (zput > has)
			FiesReader_throw(self, EINVAL,
			                 "write callback misbehaved");
		FiesReader_eat(self, zput, FR_State_Extent_Read);
		remaining -= zput;
		if (!remaining) {
			self->state = FR_State_Begin;
			return 0;
		}
		self->extent_at += zput;
		offset += zput;
		if (FiesReader_filled(self)) // short write
			return -EAGAIN;
		if (!self->funcs->send)
			return 0;
	}

	if (!self->funcs->send)
		return -EAGAIN;

	fies_ssz put = FiesReader_send(self, file->opaque, offset, remaining);
	if (put == -ENOTSUP || put == -EOPNOTSUPP)
		goto send_not_supported;
	if (put < 0)
		return (int)put;
	const fies_sz zput = (fies_sz)put;
	if (zput > remaining)
		FiesReader_throw(self, EINVAL, "send callback misbehaved");
	if (zput == remaining) {
		self->state = FR_State_Begin;
		return 0;
	}
	self->extent_at += zput;
	return 0;
}

static int
FiesReader_startExtent(FiesReader *self)
{
	self->extent_at = 0;
	const struct fies_extent *next_extent = FiesReader_data(self);
	self->extent = *next_extent;
	SwapLE(self->extent.file);
	SwapLE(self->extent.flags);
	SwapLE(self->extent.offset);
	SwapLE(self->extent.length);

	FiesReader_File *file = PMap_get(&self->files, &self->extent.file);
	if (!file)
		FiesReader_throw(self, EINVAL, "extenet for bad file id");

	if (self->extent.offset + self->extent.length > file->size)
		FiesReader_throw(self, EINVAL, "extent exceeds file size");

	unsigned long extype = self->extent.flags & FIES_FL_EXTYPE_MASK;
	if (self->extent.flags & ~(unsigned long)FIES_FL_EXTYPE_MASK)
		FiesReader_throw(self, EINVAL, "extent with unknown flags");

	if (extype == FIES_FL_ZERO) {
		if (self->pkt_size != sizeof(self->extent))
			FiesReader_throw(self, EINVAL,
			                 "bad packet size for zero extent");
		FiesReader_eat(self, sizeof(self->extent),
		               FR_State_Extent_ZeroOut);
		return FiesReader_zeroExtent(self);
	}
	else if (extype == FIES_FL_HOLE) {
		if (self->pkt_size != sizeof(self->extent))
			FiesReader_throw(self, EINVAL,
			                 "bad packet size for hole extent");
		FiesReader_eat(self, sizeof(self->extent),
		               FR_State_Extent_PunchHole);
		return FiesReader_punchHole(self);
	}
	else if (extype == FIES_FL_COPY) {
		if (self->pkt_size !=
		    (sizeof(self->extent) + sizeof(struct fies_source)))
		{
			FiesReader_throw(self, EINVAL,
			                 "bad copy extent packet size");
		}
		FiesReader_eat(self, sizeof(self->extent),
		               FR_State_Extent_GetCopyInfo);
		return FiesReader_getExtentCopyInfo(self);
	}
	else if (extype != FIES_FL_DATA)
		FiesReader_throw(self, EINVAL, "bad extent type flags");

	// data extents:

	if (self->extent.length != self->pkt_size - sizeof(self->extent))
		FiesReader_throw(self, EINVAL, "extent data length mismatch");

	if (!self->extent.length) {
		// zero sized extent, should not happen...
		self->warnings++;
		FiesReader_eat(self, sizeof(self->extent),
		               FR_State_Begin);
		return 0;
	}

	FiesReader_eat(self, sizeof(self->extent), FR_State_Extent_Read);
	return FiesReader_readExtent(self);
}

static int
FiesReader_getExtentHeader(FiesReader *self)
{
	int rc = FiesReader_bufferAtLeast(self, sizeof(struct fies_extent));
	if (rc < 0)
		return rc;
	self->state = FR_State_Extent_Start;
	return FiesReader_startExtent(self);
}

static int
FiesReader_readPacket(FiesReader *self)
{
	int rc = FiesReader_bufferAtLeast(self, sizeof(struct fies_packet));
	if (rc < 0)
		return rc;

	const struct fies_packet *pkt = FiesReader_data(self);
	if (pkt->magic[0] != FIES_PACKET_HDR_MAG0 ||
	    pkt->magic[1] != FIES_PACKET_HDR_MAG1)
	{
		FiesReader_throw(self, EINVAL, "Bad packet magic");
	}

	self->pkt_type = FIES_LE(pkt->type);
	self->pkt_size = FIES_LE(pkt->size) - sizeof(*pkt);

	if (pkt->size < sizeof(*pkt))
		FiesReader_throw(self, EINVAL, "Invalid packet size");

	FiesReader_eat(self, sizeof(*pkt), FR_State_Botched);

	if (self->newfile) {
		if (self->pkt_type != FIES_PACKET_FILE_META)
			FiesReader_throw(self, EINVAL,
			                 "Missing file meta packets");
		self->state = FR_State_FileMeta_Get;
		return FiesReader_getFileMeta(self);
	}
	else if (self->pkt_type == FIES_PACKET_FILE_META)
		FiesReader_throw(self, EINVAL, "Unexpected file meta packet");

	switch (self->pkt_type) {
	case FIES_PACKET_END:
		self->eof = true;
		FiesReader_return(self);

	case FIES_PACKET_FILE:
		if (self->pkt_size > sizeof(struct fies_file) + 0xFFFF)
			FiesReader_throw(self, EINVAL,
			                 "File packet too large");
		self->state = FR_State_NewFile_Get;
		return FiesReader_getNewFile(self);

	case FIES_PACKET_FILE_END:
		if (self->pkt_size != sizeof(struct fies_file_end))
			FiesReader_throw(self, EINVAL,
			                 "File End packet too large");
		self->state = FR_State_FileEnd;
		return FiesReader_getFileEnd(self);

	case FIES_PACKET_EXTENT:
		self->state = FR_State_Extent_Get;
		return FiesReader_getExtentHeader(self);

	case FIES_PACKET_INVALID:
	default:
		FiesReader_throw(self, EINVAL, "Invalid packet type");
	}
}

static void
FiesReader_checkVersion(FiesReader *self, uint32_t version)
{
	if (version == FIES_VERSION)
		return;
	FiesReader_throw(self, ENOTSUP, "Unsupported stream version");
}

static int
FiesReader_doReadHeader(FiesReader *self)
{
	int rc = FiesReader_bufferAtLeast(self, sizeof(struct fies_header));
	if (rc < 0)
		return rc;
	const struct fies_header *hdr = FiesReader_data(self);
	if (hdr->magic[0] != FIES_HDR_MAG0 || hdr->magic[1] != FIES_HDR_MAG1 ||
	    hdr->magic[2] != FIES_HDR_MAG2 || hdr->magic[3] != FIES_HDR_MAG3)
	{
		FiesReader_throw(self, EINVAL, "Bad header magic");
	}

	FiesReader_checkVersion(self, FIES_LE(hdr->version));

	self->hdr_flags = FIES_LE(hdr->flags);

	if ( (self->hdr_flags & self->hdr_flags_required) !=
	     self->hdr_flags_required)
	{
		FiesReader_throw(self, EINVAL, "Missing user-required flags");
	}

	if (self->hdr_flags & self->hdr_flags_rejected)
	{
		FiesReader_throw(self, EINVAL, "Flags rejected by user");
	}

	FiesReader_eat(self, sizeof(*hdr), FR_State_Begin);

	return 0;
}

extern int
FiesReader_iterate(FiesReader *self)
{
	if (self->errc) return self->errc;
	if (self->eof) return 0;

	int rc = -EFAULT;

	if (!setjmp(self->jmpbuf)) {
		switch (self->state) {
		case FR_State_Header:
			rc = FiesReader_doReadHeader(self);
			break;
		case FR_State_Begin:
			rc = FiesReader_readPacket(self);
			break;
		case FR_State_NewFile_Get:
			rc = FiesReader_getNewFile(self);
			break;
		case FR_State_NewFile_Create:
			rc = FiesReader_newFileCreate(self);
			break;
		case FR_State_FileMeta_Get:
			rc = FiesReader_getFileMeta(self);
			break;
		case FR_State_FileMeta_Do:
			rc = FiesReader_handleFileMeta(self);
			break;
		case FR_State_FileEnd:
			rc = FiesReader_getFileEnd(self);
			break;
#if 0
		case FR_State_FileClose:
			rc = FiesReader_fileClose(self);
			break;
#endif
		case FR_State_Extent_Get:
			rc = FiesReader_getExtentHeader(self);
			if (rc < 0)
				return rc;
			//[[clang::fallthrough]];
		case FR_State_Extent_Start:
			rc = FiesReader_startExtent(self);
			break;
		case FR_State_Extent_GetCopyInfo:
			rc = FiesReader_getExtentCopyInfo(self);
			break;
		case FR_State_Extent_Read:
			rc = FiesReader_readExtent(self);
			break;
		case FR_State_Extent_ZeroOut:
			rc = FiesReader_zeroExtent(self);
			break;
		case FR_State_Extent_PunchHole:
			rc = FiesReader_punchHole(self);
			break;
		case FR_State_Botched:
			FiesReader_throw(self, EFAULT, "FiesReader botched");
		}
	} else {
		if (!self->errc) {
			if (self->eof)
				return 0;
			self->errc = -EFAULT;
			self->errstr = "unknown error";
		}
		else if (!self->errstr)
			self->errstr = strerror(-self->errc);
		//fprintf(stderr, "fies exception: %s\n", self->errstr);
		return -self->errc;
	}

	return rc < 0 ? rc : 1;
}

extern int
FiesReader_readHeader(FiesReader *self)
{
	if (self->state != FR_State_Header)
		return 0;
	int rc = FiesReader_iterate(self);
	if (rc == 1) {
		if (self->state != FR_State_Header)
			return 0;
		return -EIO;
	}
	if (rc == 0)
		return -EIO;
	if (rc == -EWOULDBLOCK)
		return -EAGAIN;
	return rc;
}

extern const char*
FiesReader_getError(const FiesReader *self)
{
	if (self->errstr)
		return self->errstr;
	if (self->errc)
		return strerror(self->errc);
	return NULL;
}

extern uint32_t
FiesReader_flags(const FiesReader *self)
{
	return self->hdr_flags;
}
