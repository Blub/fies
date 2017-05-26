#ifndef FIECOPY_SRC_FIES_H
#define FIECOPY_SRC_FIES_H

/*! \file fies.h
 * Main header for the fiestream library libfies.
 */

#include <stdint.h>
#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIES_PACKED __attribute__((packed))

/*! \brief Reference to a file in a fiestream. */
typedef uint32_t fies_id;

/*! \brief Pointer to a position in a file. */
typedef uint64_t fies_pos;

/*! \brief Pointer to a position in a file. */
typedef uint64_t fies_sz;

/*! \brief Signed size type. */
typedef int64_t fies_ssz;

/*! \brief Seconds since the epoch. */
typedef uint64_t fies_secs;

/*! \brief Nanoseconds since the last second. */
typedef uint32_t fies_nsecs;
/* 32 bit are sufficient for nanoseconds if we also store seconds */

#define PRI_D_FIES_ID  PRIu32
#define PRI_X_FIES_ID  PRIx32
#define PRI_O_FIES_ID  PRIo32
#define PRI_D_FIES_POS PRIu64
#define PRI_X_FIES_POS PRIx64
#define PRI_O_FIES_POS PRIo64
#define PRI_D_FIES_SZ  PRIu64
#define PRI_X_FIES_SZ  PRIx64
#define PRI_O_FIES_SZ  PRIo64
#define PRI_D_FIES_SSZ PRIi64
#define PRI_X_FIES_SSZ PRIx64
#define PRI_O_FIES_SSZ PRIo64

/*! \brief Represents a point in time. */
struct fies_time {
	/*! \brief Seconds since the epoch. */
	fies_secs secs;
	/*! \brief Nanoseconds since the last second. */
	fies_nsecs nsecs;
	uint32_t invalid; /* make tail-padding apparent to users of this */
};

/*! \brief The size of a \ref fies_time "\c struct \c fies_time". */
#define FIES_TIME_SIZE (sizeof(fies_secs)+sizeof(fies_nsecs))

/*! \defgroup FiesReaderGroup FiesReader methods.
 * @{
 */

/*! \brief Callbacks to manage files used by a \c FiesReader reading an
 * incoming fiestream.
 */
struct FiesReader_Funcs {
	/*! \brief Called to tead from the fiestream into a buffer. */
	fies_ssz (*read)      (void *opaque, void *data, fies_sz count);

	/*! \brief Called to create a file, should fill the out_fh pointer. */
	int      (*create)    (void       *opaque,
	                       const char *filename,
	                       fies_sz     filesize,
	                       uint32_t    mode,
	                       void      **out_fh);

	/*! \brief Called to add a reference handle for incremental updates,
	 * should fill the out_fh pointer. */
	int      (*reference) (void       *opaque,
	                       const char *filename,
	                       fies_sz     filesize,
	                       uint32_t    mode,
	                       void      **out_fh);

	/*! \brief Called to create a directory, may fill the out_fh pointer.
	 */
	int      (*mkdir)     (void       *opaque,
	                       const char *dirname,
	                       uint32_t    mode,
	                       void      **out_fh);

	/*! \brief Called for symlinks (instead of the create callback). */
	int      (*symlink)   (void       *opaque,
	                       const char *filename,
	                       const char *target,
	                       void      **out_fh);

	/*! \brief Called for hardlinks (instead of the create callback). */
	int      (*hardlink)  (void       *opaque,
	                       void       *src_fh,
	                       const char *filename);

	/*! \brief Called for device nodes (instead of the create callback). */
	int      (*mknod)     (void       *opaque,
	                       const char *filename,
	                       uint32_t    mode,
	                       uint32_t    major_id,
	                       uint32_t    minor_id,
	                       void      **out_fh);

	/*! \brief Called when file ownership information is available. */
	int      (*chown)     (void *opaque, void *fh, uid_t uid, gid_t gid);

	/*! \brief Called when file modification time information. */
	int      (*set_mtime) (void *opaque, void *fh, struct fies_time time);

	/*! \brief Set an extended attribute on the file. */
	int      (*set_xattr) (void       *opaque,
	                       void       *fh,
	                       const char *name,
	                       const char *value,
	                       size_t      length);

	/*! \brief Called after all file metadata has been processed. */
	int      (*meta_end)  (void *opaque, void *fh);

	/*! \brief Optional: Called to have raw data written from the input
	 * stream into a file. This may return \c -ENOTSUP or be \c NULL if the
	 * operation is not supported or desired.
	 */
	fies_ssz (*send)      (void    *opaque,
	                       void    *fh,
	                       fies_pos off,
	                       fies_sz  len);

	/*! \brief Called to write data into a previously created file. */
	fies_ssz (*pwrite)    (void       *opaque,
	                       void       *fh,
	                       const void *data,
	                       fies_sz     count,
	                       fies_pos    offset);

	/*! \brief Called when a section of a file should be deallocated. */
	int      (*punch_hole)(void    *opaque,
	                       void    *fh,
	                       fies_pos off,
	                       fies_sz  len);

	/*! \brief Called when a section of a file should be cloned form
	 * another already created file which already had this section written
	 * out.
	 */
	int      (*clone)     (void    *opaque,
	                       void    *dest_fh,
	                       fies_pos dest_offset,
	                       void    *src_fh,
	                       fies_pos src_offset,
	                       fies_sz  length);

	/*! \brief Optional: Called when a file is no longer written to.
	 *
	 * It will still be cloned, but when the file descriptor limit pressure
	 * goes up it's probably best to keep the files closed and reopen them
	 * when they need to be cloned.
	 */
	int      (*file_done) (void *opaque, void *fh);

	/*! \brief Called when a file handle is no longer needed and can be
	 * closed.
	 */
	int      (*close)     (void *opaque, void *fh);

	/*! \brief Optional: The FiesReader is being destroyed. */
	void     (*finalize)  (void *opaque);
};


/*! \brief Class handling the reading and interpreting of a fiestream. */
struct FiesReader;

/*! \brief Create a new FiesReader instance. */
struct FiesReader* FiesReader_new       (const struct FiesReader_Funcs *funcs,
                                         void *opaque);

/*! \brief Create a new FiesReader instance. */
struct FiesReader* FiesReader_newFull   (const struct FiesReader_Funcs *funcs,
                                         void *opaque,
                                         uint32_t required_flags,
                                         uint32_t rejected_flags);

/*! \brief Delete a FiesReader instance. */
void               FiesReader_delete    (struct FiesReader *self);

/*! \brief Iterate to past the stream header. */
int                FiesReader_readHeader(struct FiesReader *self);

/*! \brief Iterate through the fiestream. */
int                FiesReader_iterate   (struct FiesReader *self);

/*! \brief Retrieve error information, if any. */
const char*        FiesReader_getError  (const struct FiesReader *self);

/*! \brief Get flags found in the stream header. */
uint32_t           FiesReader_flags     (const struct FiesReader *self);

/*! @} */

/*
 * FiesWriter - Write out a fiestream.
 */

struct FiesFile;
/*! \brief Callbacks used by a FiesWriter to write to the output stream. */
struct FiesWriter_Funcs {
	/*! \brief Write data to the output stream. */
	ssize_t (*writev)  (void *opaque, const struct iovec *iov, size_t cnt);

	/*! \brief Optional: Copy data from a file into the output stream. */
	ssize_t (*sendfile)(void *opaque,
	                    struct FiesFile *fd,
	                    fies_pos offset,
	                    size_t length);

	/*! \brief Optional: The FiesWriter is being destroyed. */
	void    (*finalize)  (void *opaque);
};

/*! \defgroup FiesWriterGroup FiesWriter methods.
 *  @{
 */

/*! \brief Create a new FiesWriter instance. */
struct FiesWriter* FiesWriter_new        (const struct FiesWriter_Funcs *funcs,
                                          void *opaque);

/*! \brief Create a new FiesWriter instance. */
struct FiesWriter* FiesWriter_newFull    (const struct FiesWriter_Funcs *funcs,
                                          void *opaque,
                                          uint32_t flags);

/*! \brief Delete a FiesWriter instance. */
void               FiesWriter_delete     (struct FiesWriter *self);

/*! \brief Get (and possibly create) the device id for an OS device. */
int                FiesWriter_getOSDevice(struct FiesWriter *self,
                                          dev_t node,
                                          fies_id *id,
                                          bool create);

/*! \brief Create another physical device with its own extent map. */
fies_id            FiesWriter_newDevice  (struct FiesWriter *self);

/*! \brief Close a device. Mostly useful to reduce memory usage. */
int                FiesWriter_closeDevice(struct FiesWriter *self, fies_id);

/*! \brief Write a regular file out into the fiestream.
 * \note The file must be a special file or support the \c FIEMAP \c ioctl().
 */
int                FiesWriter_writeOSFile(struct FiesWriter *self,
                                          const char *filename,
                                          unsigned int flags);

/*! \brief Write an OS file descriptor into the fiestream. */
int                FiesWriter_writefd    (struct FiesWriter *self,
                                          int fd,
                                          const char *filename,
                                          unsigned int flags);

/*! \brief Write a custom \ref FiesFile "\c struct \c FiesFile" handle into the
 * fiestream.
 */
int                FiesWriter_writeFile  (struct FiesWriter *self,
                                          struct FiesFile *handle);

/*! \brief Add a file as reference for COW or incremental updates. */
int                FiesWriter_readRefFile(struct FiesWriter *self,
                                          struct FiesFile *handle);

/*! \brief Set an error message, usable by callbacks for convenience. */
int                FiesWriter_setError   (struct FiesWriter *self,
                                          int errc,
                                          const char *msg);

/*! \brief Retrieve error information, if any. */
const char*        FiesWriter_getError   (const struct FiesWriter *self);

/*! @} */

/*
 * Custom file handling.
 *
 * Some callbacks also get a FiesWriter - this should only be used for
 * FiesWriter_setError() in order to clarify an occurring error if printing
 * it to stderr is not desirable.
 */

/*! \addtogroup FiesWriterGroup
 * @{
 */

struct FiesFile_Extent;
/*! \brief Callbacks used to read data from a file into a fiestream. */
struct FiesFile_Funcs {
	/*! \brief Read data from a file from a specific offset. */
	ssize_t (*pread)          (struct FiesFile *handle,
	                           void *buffer,
	                           size_t length,
	                           fies_pos offset);
	/*! \brief Close a file which is no longer needed. */
	void    (*close)          (struct FiesFile *handle);

	/*! \brief Called to map a range of extents into a buffer. */
	ssize_t (*next_extents)   (struct FiesFile *handle,
	                           struct FiesWriter *writer,
	                           fies_pos logical_start,
	                           struct FiesFile_Extent *buffer,
	                           size_t buffer_elements);

	/*! \brief Currently unused. */
	int     (*verify_extent)  (struct FiesFile *handle,
	                           struct FiesWriter *writer,
	                           const struct FiesFile_Extent *extent);

	/*! \brief Deprecated convenience callback. */
	int     (*get_os_fd)      (struct FiesFile *handle);

	// FIXME: Add support for ids by name.
	/*! \brief Get owner (user and group) of a file. */
	int     (*get_owner)      (struct FiesFile *handle,
	                           uid_t *uid,
	                           gid_t *gid);

	/*! \brief Get the last modification time of a file. */
	int     (*get_mtime)      (struct FiesFile *handle,
	                           struct fies_time *time);

	/*! \brief Get the device major and minor numbers of a special file. */
	int     (*get_device)     (struct FiesFile *handle,
	                           uint32_t *out_major,
	                           uint32_t *out_minor);

	/*! \brief Get a list of extended attributes. */
	ssize_t (*list_xattrs)    (struct FiesFile *handle,
	                           const char **names);
	/*! \brief Free the list previously retrieved from \c list_xattrs . */
	void    (*free_xattr_list)(struct FiesFile *handle, const char *names);

	/*! \brief Get an extended attribute by name. */
	ssize_t (*get_xattr)      (struct FiesFile *handle,
	                           const char *name,
	                           const char **buffer);
	/*! \brief Free an xattr retrieved via \c get_xattr . */
	void    (*free_xattr)     (struct FiesFile *handle,
	                           const char *buffer);
};

extern const struct FiesFile_Funcs fies_os_file_funcs;

/*! \brief FiesFile device IDs should be initialized to this. */
#define FIES_DEVICE_INVALID ((fies_id)-1)

/*! \brief Reference to a file read into a \c FiesWriter , public members
 * need to be filled by the creator of the entry.
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
struct FiesFile {
	void *opaque; /*!< \brief User data, ignored by the \c FiesWriter .*/
	const struct FiesFile_Funcs *funcs; /*!< \brief Callbacks. */
	char *filename;   /*!< \brief Filename, free()d by the FiesWriter. */
	char *linkdest;   /*!< \brief Symlink target or \c NULL, free()d. */
	fies_sz filesize; /*!< \brief File size. */
	uint32_t mode;    /*!< \brief Fies compatible file mode. */
	fies_id device;   /*!< \brief Device ID for this file. */
	fies_id fileid;   /*!< \brief Internal id, filled in by FiesWriter. */
};
#pragma clang diagnostic pop

/*! \brief Create a \c FiesFile . */
struct FiesFile* FiesFile_new   (void *opaque,
                                 const struct FiesFile_Funcs *funcs,
                                 const char *filename,
                                 const char *linkdest,
                                 fies_sz filesize,
                                 uint32_t mode,
                                 fies_id device);
/*! \brief Convenience constructor with explicit file and link name lengths. */
struct FiesFile* FiesFile_new2  (void *opaque,
                                 const struct FiesFile_Funcs *funcs,
                                 const char *filename, size_t filenamelen,
                                 const char *linkdest, size_t linkdestlen,
                                 fies_sz filesize,
                                 uint32_t mode,
                                 fies_id device);


/*! \brief Destroy a \c FiesFile. */
void   FiesFile_close           (struct FiesFile *self);

/*! \brief Try to tetrieve the file descriptor of a \c FiesFile . */
int    FiesFile_get_os_fd       (struct FiesFile *self);

/*! \brief Set the device id of a file. */
void   FiesFile_setDevice       (struct FiesFile *self, fies_id id);

/*! \brief Create a device for the file if it doesn't exist yet. */
#define FIES_FILE_CREATE_DEVICE    0x001
/*! \brief Follow symlinks when opening files. */
#define FIES_FILE_FOLLOW_SYMLINKS  0x002

/*! \brief Open a file as a \c FiesFile .  */
struct FiesFile* FiesFile_open  (const char *filename,
                                 struct FiesWriter *writer,
                                 unsigned int flags);

/*! \brief Open a file as a \c FiesFile .  */
struct FiesFile* FiesFile_openat(int dirfd,
                                 const char *filename,
                                 struct FiesWriter *writer,
                                 unsigned int flags);

/*! \brief Turn a file descriptor into a \c FiesFile . */
struct FiesFile* FiesFile_fdopen(int fd,
                                 const char *filename,
                                 struct FiesWriter *writer,
                                 unsigned int flags);

/*! \brief Convert \c FIES_M_* file modes to \c fstat() compatible ones. */
bool fies_mode_to_stat  (uint32_t fiesmode, mode_t *statmode);
/*! \brief Convert \c fstat() file modes to \c FIES_M_* compatible ones. */
bool fies_mode_from_stat(mode_t   statmode, uint32_t *fiesmode);

/*! \brief Describes a file extent so the \c FiesWriter knows what to do
 * with it, created by the \ref FiesFile callbacks.
 */
struct FiesFile_Extent {
	/*! \brief Physical device id of the extent. */
	fies_pos device;
	/*! \brief Logical offset of the extent within the file. */
	fies_pos logical;
	/*! \brief Physical offset of the extent on the underlying device. */
	fies_pos physical;
	/*! \brief Length of the extent in bytes. */
	fies_sz length;
	/*! \brief Flags describing the type of extent. */
	uint32_t flags;

	uint32_t reserved0;
};

/*! @} */

/*
 * Stream format.
 * All numbers in the stream are little endian.
 */

#define FIES_HDR_MAG0 'F'
#define FIES_HDR_MAG1 'I'
#define FIES_HDR_MAG2 'E'
#define FIES_HDR_MAG3 'S'
#define FIES_HDR_MAGIC \
	{ FIES_HDR_MAG0, FIES_HDR_MAG1, FIES_HDR_MAG2, FIES_HDR_MAG3 }

#define FIES_VERSION 1

/* Header flags are mostly hints but special purpose tools might require some
 * "order" in the file, eg. `fies-restore` needs FIES_WHOLE_FILES.
 */

/*! \brief Each file is finished before another one starts. */
#define FIES_WHOLE_FILES 0x00000001
/*! \brief Random Access: Extents may be out of order. */
#define FIES_UNORDERED   0x00000002
/*! \brief Incremental: Files should not be zero initialized. */
#define FIES_INCREMENTAL 0x00000004

/*! \brief This tells FiesWriter_newFull not to write a fies_header. */
#define FIES_RAW         0x80000000

/*! \brief If the user doesn't bother to specify their intentions we assume
 * they're writing whole files in a possibly random order.
 */
#define FIES_DEFAULT_FLAGS (FIES_WHOLE_FILES | FIES_UNORDERED)

struct fies_header {
	char magic[4];
	uint32_t version;
	uint32_t flags;
};

#define FIES_PACKET_HDR_MAG0 'F'
#define FIES_PACKET_HDR_MAG1 'I'
#define FIES_PACKET_HDR_MAGIC { FIES_PACKET_HDR_MAG0, FIES_PACKET_HDR_MAG1 }

#define FIES_PACKET_INVALID   0
#define FIES_PACKET_END       1
#define FIES_PACKET_FILE      2
#define FIES_PACKET_FILE_META 3
#define FIES_PACKET_EXTENT    4
#define FIES_PACKET_FILE_END  5

struct fies_packet {
	char magic[2];
	uint16_t type;
	uint32_t reserved;
	uint64_t size;
};

#define FIES_M_FMT       0770000
#define FIES_M_FREF      0400000
#define FIES_M_FHARD     0200000
#define FIES_M_FSOCK     0140000
#define FIES_M_FLNK      0120000
#define FIES_M_FREG      0100000
#define FIES_M_FBLK      0060000
#define FIES_M_FDIR      0040000
#define FIES_M_FCHR      0020000
#define FIES_M_FIFO      0010000

#define FIES_M_PERMS     0007777

# define FIES_M_PSUID    0004000
# define FIES_M_PSGID    0002000
# define FIES_M_PSSTICKY 0001000

# define FIES_M_PRWXU    0000700
#  define FIES_M_PRUSR   0000400
#  define FIES_M_PWUSR   0000200
#  define FIES_M_PXUSR   0000100

# define FIES_M_PRWXG    0000070
#  define FIES_M_PRGRP   0000040
#  define FIES_M_PWGRP   0000020
#  define FIES_M_PXGRP   0000010
# define FIES_M_PRWXO    0000007
#  define FIES_M_PROTH   0000004
#  define FIES_M_PWOTH   0000002
#  define FIES_M_PXOTH   0000001


/*! \brief Test whether a file mode may have extents. */
#define FIES_M_HAS_EXTENTS(M) \
	(((M) & FIES_M_FMT) == FIES_M_FREG)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
/*! \brief Starts a new file. The file name follows directly afterwards. */
struct fies_file {
	fies_id  id;          /*!< \brief File ID used in later packets. */
	uint32_t mode;        /*!< \brief File type and permission flags. */
	fies_sz  size;        /*!< \brief File size in bytes. */
	uint16_t name_length; /*!< \brief File name length. */
	uint16_t link_length; /*!< \brief Length of symbolic link target. */
	uint32_t reserved;
	char name[0];
	// After this packet there the name, possibly followed by file-specific
	// data:
	//   For symlinks: the destination.
	//   For device nodes: a fies_file_device entry.
};
#pragma clang diagnostic pop

/*! \brief Device specification for a device file, 
 * packets.
 */
struct fies_file_device {
	uint32_t major_id; /*!< \brief Numeric major ID. */
	uint32_t minor_id; /*!< \brief Numeric minor ID. */
};

/*! \brief Meta information such as ownership, modification times, ACLs or
 * extended attributes.
 */
struct fies_file_meta {
	uint32_t type; /*!< \brief Type of information. */
	fies_id  file; /*!< \brief File id the information is meant for. */
};
/*! \brief Type zero metadata is reserved and thus invalid. */
#define FIES_META_INVALID ((uint32_t)0)

/*! \brief This ends the array of fies_meta packets for a file. */
#define FIES_META_END     ((uint32_t)1)

/*! \brief The packet contains a \ref fies_meta_owner . */
#define FIES_META_OWNER   ((uint32_t)2)

/*! \brief The packet contains the 2 values of a \ref fies_time structure . */
#define FIES_META_TIME    ((uint32_t)3)

/*! \brief TODO, POSIX.1e ACLs. */
#define FIES_META_ACL     ((uint32_t)4)

/*! \brief TODO. */
#define FIES_META_XATTR   ((uint32_t)5)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
/*! \brief File xattrs are name value pairs, data follows the size header. */
struct fies_meta_xattr {
	uint32_t name_length;  /*!< \brief Name length in the data field. */
	uint32_t value_length; /*!< \brief Value length in the data field. */
	char data[0];          /*!< \brief Name, nul byte, value. */
};
#pragma clang diagnostic pop

/*! \brief Any values equal or above \c FIES_META_CUSTOM may safely be ignored.
 */
#define FIES_META_CUSTOM  ((uint32_t)0x8000)

/*! \brief File ownership information found in \ref fies_file_meta packets. */
struct fies_meta_owner {
	uint32_t uid; /*!< \brief Numeric user id. */
	uint32_t gid; /*!< \brief Numeric group id. */
};

/*! \brief Internal convenience macro. */
#define FIES_META_TIME_SIZE FIES_TIME_SIZE

/*! \brief File extent information and possibly followed by data to be written
 * to the file.
 */
struct fies_extent {
	fies_id  file;   /*!< \brief File ID, \see fies_file . */
	uint32_t flags;  /*!< \brief Extent type, \see FiesFile_Extent .*/
	fies_pos offset; /*!< \brief Extent offset in bytes. */
	fies_sz  length; /*!< \brief Extent length in bytes. */
};

#define FIES_FL_EXTYPE_MASK 0x000000FF
#define FIES_FL_DATA        0x00000001
#define FIES_FL_ZERO        0x00000002
#define FIES_FL_HOLE        0x00000004
#define FIES_FL_COPY        0x00000008

#define FIES_FL_SHARED      0x00000100

/*! \brief A \c fies_extent with the \c FIES_FL_COPY flag set is followed by a
 *  a source specification to clone from.
 */
struct fies_source {
	fies_id file; /*!< File ID, \see fies_file . */
	fies_pos offset FIES_PACKED; /*!< Extent offset in the source file. */
};

struct fies_file_end {
	fies_id file;
};

#ifdef __cplusplus
} /* extern "C" */
#endif

/*!
 * \typedef fies_id
 *
 * Every file in a fiestream gets an id which is unique across the current
 * "device". A fies-reader implementation should be able to deal with arbitrary
 * numeric IDs, but a correct fies-writer implementation should start with id
 * zero and increment the number for each file on the device.
 */

/*!
 * \typedef fies_pos
 *
 * This is used in place of `off_t` types when the data is used to reference
 * file offsets.
 */

/*!
 * \typedef fies_sz
 *
 * This is used in place of `size_t` types when the data is used to reference
 * file lengths.
 */

/*!
 * \typedef fies_nsecs
 *
 *   A 32 bit number is enough to hold this.
 *
 * \struct fies_time
 *
 *   Used to store file modification times.
 *
 *   \note In C this struct gets tail padded on most systems. To point this out
 *         the \c invalid member has been added. You should never use the C size
 *         of this structure. Instead use \ref FIES_TIME_SIZE.
 *
 * \def FIES_TIME_SIZE
 *   This macro is provided due to the fact that the \ref fies_time "\c struct
 *   \c fies_time" is tail-padded.
 */

/*!
 * \typedef FiesWriter
 *   This class provides the basic mechanisms to track mapped file extents and
 *   turn them into a fiestream. The most basic usage allows writing files from
 *   the OS into the stream. Advanced users can create file handles with custom
 *   callbacks used to map extents and read data.
 * \fn FiesWriter_new
 *   \memberof FiesWriter
 * \fn FiesWriter_delete
 *   \memberof FiesWriter
 */

/*!
 * \def FIES_FL_EXTYPE_MASK
 * \brief Mask for the extent type type in a \c fies_file's flag list.
 *
 * \def FIES_FL_DATA
 * \brief Extent type: The packet contains data to be written to the file.
 *
 * \def FIES_FL_ZERO
 * \brief Extent type: The extent is allocated and reads as all zeroes.
 * This usually means the extents are not written but their space is allocated.
 *
 * \def FIES_FL_HOLE
 * \brief Extent type: The extent is not allocated and reads as all zeros.
 * This describes a sparse section of a file.
 *
 * \def FIES_FL_COPY
 * \brief Extent type: This is a shared extent.
 * The packet contains a \ref fies_source "\c struct \c fies_source"
 * describing which file to clone from.
 */
/*!
 * \struct FiesFile_Extent
 *
 * This structure is to be filled by the user in the \c next_extents callback
 * of a \ref FiesFile "\c struct \c FiesFile".
 * It shares the same extent flags as \ref fies_extent "\c struct
 * \c fies_extent". It also contains a physical offset which is used to
 * identify already encountered versions of this extent.
 *
 */
/*!
 * \struct FiesFile_Funcs
 *
 * This can be used in order to implement extent mapping of files where this
 * library otherwise failed. For convenience there are functions to open such
 * a file handle from a file name on disk or a file descriptor. This, hoever,
 * is usually not necessary as the \ref FiesWriter "\c FiesWriter" class
 * provides convenience methods to directly read a file from disk or a file
 * descriptor.
 *
 *   \var FiesFile_Funcs::pread
 *     Used if the sendfile callback is not available or returns \c -ENOTSUP .
 *
 *     \param handle Pointer to a \ref FiesFile "\c FiesFile".
 *     \param buffer Buffer to write the data to.
 *     \param length Amount of data to read.
 *     \param offset Position to start reading from.
 *
 *   \var FiesFile_Funcs::close
 *     \param handle Pointer to a \ref FiesFile "\c FiesFile".
 *
 *   \var FiesFile_Funcs::next_extents
 *     This function should try to map as many extents as the buffer can hold.
 *
 *     \param handle Pointer to a \ref FiesFile "\c FiesFile".
 *     \param writer Pointer to the current \ref FiesWriter "\c FiesWriter".
 *     \param logical_start Logical offset in the file from which to start
 *         mapping extents into the buffer.
 *     \param buffer Pointer to a \ref FiesFile_Extent "\c FiesFile_Extent"
 *         buffer to be filled with as many extents as possible.
 *     \param buffer_elements Number of elements the buffer can hold.
 *     \return The number of mapped extents, a negative errno value on error,
 *         zero if no more maps are available.
 *
 *   \var FiesFile_Funcs::verify_extent
 *     \param handle Pointer to a \ref FiesFile "\c FiesFile".
 *
 *   \var FiesFile_Funcs::get_os_fd
 *     Used by \ref FiesFile_get_os_fd in order to get an OS file descriptor.
 *     This can be used for the deprecated \c sendfile callback.
 *
 *     \param handle Pointer to a \ref FiesFile "\c FiesFile".
 *
 *   \var FiesFile_Funcs::get_owner
 *     \param handle Pointer to a \ref FiesFile "\c FiesFile".
 *     \param uid Non-NULL pointer the user id should be written to.
 *     \param gid Non-NULL pointer the group id should be written to.
 *     \return Should return zero on success, a negative errno value otherwise.
 *
 *   \var FiesFile_Funcs::get_mtime
 *     \param handle Pointer to a \ref FiesFile "\c FiesFile".
 *     \param time Non-NULL pointer the time should be written to.
 *     \return Should return zero on success, a negative errno value otherwise.
 *
 *   \var FiesFile_Funcs::get_device
 *     The type of device is encoded into the file's mode as \c FIES_M_FCHR
 *     or \c FIES_M_FBLK .
 *     \param handle Pointer to a \ref FiesFile "\c FiesFile".
 *     \param out_major Major device ID.
 *     \param out_minor Minor device ID.
 */

/*!
 * \def FIES_PACKET_INVALID
 *   \brief Zero is simply an invalid packet type.
 *
 * \def FIES_PACKET_END
 *   \brief Signifies the end of a fiestream.
 *
 * \def FIES_PACKET_NEWDEV
 *   \brief Introduces or resets a device id.
 *
 *   If the id already exists the previous device can be considered done, all
 *   file handles on it can be closed and later files refering to this ID no
 *   longer refer to this device. File extent packets for files of this device
 *   id which have been created before this packet are illegal, should be
 *   warned about and discarded.
 *
 * \def FIES_PACKET_FILE
 *   \brief Creates a new file. A set of \c FIES_PACKET_FILE_META packets
 *   follow it.
 *
 * \def FIES_PACKET_FILE_META
 *   \brief File meta information such as ownership and time stamps.
 *
 *   Only legal directly after a \c FIES_PACKET_FILE packet or another
 *   \c FIES_PACKET_FILE_META packet. A \c FIES_PACKET_FILE_META of type
 *   \c FIES_META_END must terminate the series of meta packets.
 *
 * \def FIES_PACKET_EXTENT
 *   \brief A file extent, possibly with data or a \c fies_source definition
 *   to clone from.
 *
 * \def FIES_PACKET_FILE_END
 *   \brief A file's data is finished and will only be cloned (read) from now
 *   on.
 */

/*! \struct fies_file_meta
 *
 * \brief Contains various meta information about files.
 *
 * The receiver should error when encountering a meta-field which is
 * not known to it and below FIES_META_CUSTOM.
 *
 * Every file must have a FIES_META_END packet following it to signal the
 * end of metadata. This is because it is good practice to not link the file
 * into the file system until at least all the permission-critical metadata has
 * been processed and applied to the O_TMPFILE-created file descriptor.
 *
 * Anything equal to or above FIES_META_CUSTOM may be warned about but
 * should not be treated as error. This is meant for application specific
 * information.
 */

#endif
