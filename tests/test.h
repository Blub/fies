#ifndef FIES_TESTS_TEST_H
#define FIES_TESTS_TEST_H

#include "../config.h"

#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include <exception>
#include <initializer_list>

#include "../include/fies.h"

#include "util.h"

extern size_t test_errors;

void err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void failed(const char *expr, const char *fn, unsigned long ln);
#define \
CK(EXPR) do { \
	if (!(EXPR)) \
		failed(#EXPR, __FUNCTION__, __LINE__); \
} while(0)
#define \
ASSERT(EXPR) do { \
	if (!(EXPR)) {\
		failed(#EXPR, __FUNCTION__, __LINE__); \
		abort(); \
	} \
} while(0)

typedef struct FiesWriter FiesWriter;
typedef struct FiesWriter_Funcs FiesWriter_Funcs;
typedef struct FiesFile FiesFile;
typedef struct FiesFile_Extent FiesFile_Extent;
typedef struct fies_time fies_time;

struct TestException : std::exception {
	TestException(string msg) : msg_(move(msg)) {
	}

	const char *what() const noexcept override {
		return msg_.c_str();
	}

	string msg_;
};

struct FinalizedException : TestException {
	FinalizedException() : TestException("already finalized") {
	}
};

// std::binary_search doesn't return an iterator...
template<typename ITER, typename T, typename CMP>
ITER binsearch(ITER beg, ITER end, const T& key, CMP&& cmp, bool hi = false)
{
	ssize_t a = -1, b = end-beg;
	while ((b-a) > 1) {
		ssize_t i = a + (b-a)/2;
		auto c = cmp(key, beg[i]);
		if      (c < 0) b = i;
		else if (c > 0) a = i;
		else return beg+i;
	}
	return beg+(hi ? b : a);
}


#endif
