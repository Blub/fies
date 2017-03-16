#ifndef FIES_TESTS_CPPWRITER_H
#define FIES_TESTS_CPPWRITER_H

#include <sys/uio.h>

#include "test.h"

extern const FiesWriter_Funcs cppwriter_funcs;

struct Writer {
	Writer();
	constexpr Writer(nullptr_t);
	virtual ~Writer();

	void createDefault();

	operator bool() const;
	FiesWriter *fies();
	operator FiesWriter*();

	virtual ssize_t writev(const struct iovec *iov, size_t cnt);
	virtual ssize_t sendfile(struct FiesFile *fd,
	                         fies_pos offset,
	                         size_t length);
	virtual void finalize();

	FiesWriter *self_;
	bool finalized_ = false;
};

inline constexpr
Writer::Writer(nullptr_t)
	: self_(nullptr)
{
}

inline
Writer::operator bool() const
{
	return !!self_;
}

inline FiesWriter*
Writer::fies()
{
	return self_;
}

inline
Writer::operator FiesWriter*()
{
	return fies();
}

struct FiesWriterDeleter {
	constexpr FiesWriterDeleter() = default;
	inline void operator()(FiesWriter *fies) {
		FiesWriter_delete(fies);
	}
};

//static inline uniq<FiesWriter, FiesWriterDeleter>
//newFiesWriter(Writer *wr)
//{
//	return uniq<FiesWriter, FiesWriterDeleter>{
//		FiesWriter_new(&cppwriter_funcs,
//		               reinter<void*>(wr))
//	};
//}

#endif
