#ifndef FIES_TESTS_CPPFILE_H
#define FIES_TESTS_CPPFILE_H

#include "test.h"

struct File {
	constexpr File() = default;
	constexpr File(File&&) = default;
	constexpr File(const File&) = default;
	virtual ~File();
	virtual ssize_t pread(void *buffer, size_t length, fies_pos offset);
	virtual void    close();
	virtual ssize_t nextExtents(FiesWriter*,
	                            fies_pos logical_start,
	                            FiesFile_Extent *buffer,
	                            size_t buffer_elements);
	virtual int     verifyExtent(FiesWriter*, const FiesFile_Extent*);
	virtual int     getOSFD();
	virtual int     getDevice(uint32_t *out_maj, uint32_t *out_min);
	virtual ssize_t listXAttrs(const char **names);
	virtual void    freeXAttrList(const char *names);
	virtual ssize_t getXAttr(const char *name, const char **buffer);
	virtual void    freeXAttr(const char *buffer);
};

extern const struct FiesFile_Funcs virt_file_funcs;

struct FiesFileDeleter {
	constexpr FiesFileDeleter() = default;
	inline void operator()(FiesFile *fies) {
		FiesFile_close(fies);
	}
};

uniq<FiesFile, FiesFileDeleter>
static inline newFiesFile(File *self,
                          const char *name,
                          fies_sz size,
                          uint32_t mode,
                          fies_id device = 0)
{
	return uniq<FiesFile, FiesFileDeleter>{
		FiesFile_new(reinter<void*>(self), &virt_file_funcs,
		             name, nullptr, size, mode, device)
	};
}

#endif
