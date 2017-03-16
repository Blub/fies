#ifndef FIES_TESTS_MEMREADER_H
#define FIES_TESTS_MEMREADER_H

#include "memwriter.h"
#include "cppreader.h"
#include "checkfile.h"

struct MemReader : Reader {
	MemReader() = delete;
	MemReader(const uint8_t *data, size_t length);
	MemReader(MemWriter&);

	fies_ssz read(void *data, fies_sz count) override;

	int      create  (const char *filename,
	                  fies_sz     filesize,
	                  uint32_t    mode,
	                  void      **out_fh) override;
	int      close   (void *fh) override;
	fies_ssz pwrite  (void *fh,
	                  const void *data,
	                  fies_sz count,
	                  fies_pos offset) override;
	int      clone   (void    *dest_fh,
	                  fies_pos dest_offset,
	                  void    *src_fh,
	                  fies_pos src_offset,
	                  fies_sz  length) override;
	int      fileDone(void *fh) override;

	size_t remaining() const;
	const uint8_t *data() const;

	void expectFile(uniq<CheckFile>);
	void expectFile(CheckFile*); // screw make_unique

	const uint8_t *data_;
	size_t length_;
	size_t pos_ = 0;

	map<string, uniq<CheckFile>> expected_files_;
};

inline size_t
MemReader::remaining() const
{
	return length_ - pos_;
}

inline const uint8_t*
MemReader::data() const
{
	return data_ + pos_;
}

inline void
MemReader::expectFile(uniq<CheckFile> file)
{
	expected_files_.emplace(file->name_, move(file));
}

inline void
MemReader::expectFile(CheckFile *file)
{
	return expectFile(uniq<CheckFile>{file});
}

#endif
