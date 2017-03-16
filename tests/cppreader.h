#ifndef FIES_TESTS_CPPREADER_H
#define FIES_TESTS_CPPREADER_H

#include <sys/uio.h>

#include "test.h"

extern const FiesReader_Funcs cppreader_funcs;

struct Reader {
	Reader();
	virtual ~Reader();

	operator bool() const;
	FiesReader *fies();
	operator FiesReader*();

	bool readAll();

	virtual fies_ssz read      (void *data, fies_sz count);
	virtual int      create    (const char *filename,
	                            fies_sz     filesize,
	                            uint32_t    mode,
	                            void      **out_fh);
	virtual int      mkdir     (const char *dirname,
	                            uint32_t    mode,
	                            void      **out_fh);
	virtual int      symlink   (const char *filename,
	                            const char *target,
	                            void      **out_fh);
	virtual int      hardlink  (void *src_fh, const char *filename);
	virtual int      mknod     (const char *filename,
	                            uint32_t    mode,
	                            uint32_t    major_id,
	                            uint32_t    minor_id,
	                            void      **out_fh);
	virtual int      chown     (void *fh, uid_t uid, gid_t gid);
	virtual int      setMTime  (void *fh, struct fies_time time);
	virtual int      setXAttr  (void *fh,
	                            const char *name,
	                            const char *value,
	                            size_t length);
	virtual int      metaEnd   (void *fh);
	virtual fies_ssz send      (void *fh, fies_pos off, fies_sz len);
	virtual fies_ssz pwrite    (void       *fh,
	                            const void *data,
	                            fies_sz     count,
	                            fies_pos    offset);
	virtual int      punchHole (void *fh, fies_pos off, fies_sz len);
	virtual int      clone     (void    *dest_fh,
	                            fies_pos dest_offset,
	                            void    *src_fh,
	                            fies_pos src_offset,
	                            fies_sz  length);
	virtual int      fileDone  (void *fh);
	virtual int      close     (void *fh);
	virtual void     finalize  ();

	virtual int gotFlags(uint32_t flags);

	FiesReader *self_;
	int meta_err_ = ENOTSUP;
};

inline
Reader::operator bool() const
{
	return !!self_;
}

inline FiesReader*
Reader::fies()
{
	return self_;
}

inline
Reader::operator FiesReader*()
{
	return fies();
}

struct FiesReaderDeleter {
	constexpr FiesReaderDeleter() = default;
	inline void operator()(FiesReader *fies) {
		FiesReader_delete(fies);
	}
};

//static inline uniq<FiesReader, FiesReaderDeleter>
//newFiesReader(void *wr)
//{
//	return uniq<FiesReader, FiesReaderDeleter>{
//		FiesReader_new(&cppreader_funcs, wr);
//	};
//}

#endif
