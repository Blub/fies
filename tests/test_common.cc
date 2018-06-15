#include <stdio.h>
#include <stdarg.h>
#include "test.h"

size_t test_errors = 0;

void
err(const char *fmt, ...)
{
	++test_errors;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void
failed(const char *expr, const char *fn, unsigned long ln)
{
	err("assertion (%s) failed at %s: %lu\n", expr, fn, ln);
}

const char*
TestException::what() const noexcept
{
	return msg_.c_str();
}
