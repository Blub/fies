#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "../lib/util.h"
#include "../include/fies.h"

#define EQZ(X, Y) do { \
	++count; \
	size_t x = (X); \
	size_t y = (Y); \
	if (x != y) { \
		++failed; \
		fprintf(stderr, "Assertion (%s) failed: %zu != %zu\n", \
		        #X " == " #Y, x, y); \
	} \
} while (0)

#define EQS(X, Y) do { \
	++count; \
	const char *x = (X); \
	const char *y = (Y); \
	if (strcmp(x, y)) { \
		++failed; \
		fprintf(stderr, "Assertion (%s) failed: `%s` != `%s`\n", \
		        #X " == " #Y, x, y); \
	} \
} while (0)

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	size_t count = 0;
	size_t failed = 0;
	char buf[32];

	EQZ(fies_mtree_encode(NULL, 0, "a file", 6), sizeof("a\\040file")-1);
	EQZ(fies_mtree_encode(buf, 32, "a file", 6), sizeof("a\\040file")-1);
	EQS(buf, "a\\040file");
	EQZ(fies_mtree_decode(NULL, 0, "a\\040file", 100), 6);
	EQZ(fies_mtree_decode(buf, 32, "a\\040file", 100), 6);
	EQS(buf, "a file");

	// we need a terminating nul byte
	EQZ(fies_mtree_encode(buf, 4, "1234567", 6), 3);
	EQS(buf, "123");
	// we don't want truncated escape sequences
	EQZ(fies_mtree_encode(buf, 3, "a file", 6), 1);
	EQS(buf, "a");
	// we need a terminating nul byte also after an escape sequence
	EQZ(fies_mtree_encode(buf, 4, "a file", 6), 1);
	EQS(buf, "a");
	EQZ(fies_mtree_encode(buf, 5, "a file", 6), 1);
	EQS(buf, "a");
	// properly end with an escape sequence
	EQZ(fies_mtree_encode(buf, 6, "a file", 6), sizeof("a\\040")-1);
	EQS(buf, "a\\040");
	// decode escape sequences
	EQZ(fies_mtree_decode(buf, 3, "a\\040file", 100), 2);
	EQS(buf, "a ");

	return 0;
}
