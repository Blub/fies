#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <regex.h>

#include "../lib/vector.h"
#include "../lib/util.h"

#include "fies_regex.h"
#include "util.h"

struct Regex {
	regex_t regex;
};

char*
parse_regex(const char *in_pattern, const char **endptr)
{
	int err = EINVAL;
	char *pattern = NULL;

	char *delim = strchr("/@|!,.:", *in_pattern);
	if (!delim)
		goto out;

	++in_pattern;

	// prepare temporary buffers:
	pattern = malloc(strlen(in_pattern)+1);
	if (!pattern)
		goto out_errno;

	bool escaped = false;
	size_t at = 0;
	while (*in_pattern && (escaped || *in_pattern != *delim)) {
		if (escaped) {
			// in the pattern only the delimiter needs escaping
			if (*in_pattern != *delim)
				pattern[at++] = '\\';
			pattern[at++] = *in_pattern;
			escaped = false;
		} else {
			escaped = *in_pattern == '\\';
			if (!escaped)
				pattern[at++] = *in_pattern;
		}
		++in_pattern;
	}
	pattern[at] = 0;

	if (*in_pattern != *delim)
		goto out;

	if (endptr)
		*endptr = in_pattern+1; // skip the delimiter
	return pattern;

out_errno:
	err = errno;
out:
	free(pattern);
	errno = err;
	return NULL;
}

int
parse_regex_flags_full(const char *pattern,
                       const char **endptr,
                       int flags,
                       Regex_flag_cb *flag_cb,
                       void *opaque)
{
	while (*pattern) {
		switch (*pattern) {
		case 'i': flags |=  REGEX_F_ICASE; break;
		case 'I': flags &= ~REGEX_F_ICASE; break;
		case 'n': flags |=  REGEX_F_NOSUB; break;
		case 'N': flags &= ~REGEX_F_NOSUB; break;
		case 'm': flags |=  REGEX_F_NEWLINE; break;
		case 'M': flags &= ~REGEX_F_NEWLINE; break;
		default:
			if (flag_cb) {
				int rc = flag_cb(opaque, *pattern);
				if (rc < 0)
					return rc;
			}
			break;
		}
		++pattern;
	}
	if (endptr)
		*endptr = pattern;
	return flags;
}

int
parse_regex_flags(const char *pattern, const char **endptr, int flags)
{
	return parse_regex_flags_full(pattern, endptr, flags, NULL, NULL);
}

Regex*
Regex_new(const char *pattern, int flags, char **out_errstr)
{
	int err = 0;
	const char *errstr = NULL;
	char errbuf[1024];
	int regflags = REG_EXTENDED;

	if (flags & REGEX_F_ICASE)   regflags |= REG_ICASE;
	if (flags & REGEX_F_NOSUB)   regflags |= REG_NOSUB;
	if (flags & REGEX_F_NEWLINE) regflags |= REG_NEWLINE;

	Regex *self = malloc(sizeof(*self));
	if (!self)
		goto out_errno;
	int errc = regcomp(&self->regex, pattern, regflags);
	if (errc != 0) {
		if (regerror(errc, &self->regex, errbuf, sizeof(errbuf))) {
			errstr = errbuf;
		} else {
			errstr = "failed to parse regular expression";
		}
		err = EINVAL;
		goto out;
	}

	return self;

out_errno:
	err = errno;
	errstr = strerror(errno);
out:
	free(self);
	*out_errstr = errstr ? strdup(errstr) : NULL;
	errno = err;
	return NULL;
}

void
Regex_destroy(Regex *self)
{
	if (!self)
		return;
	regfree(&self->regex);
	free(self);
}

void
Regex_pdestroy(Regex **pself)
{
	Regex_destroy(*pself);
}

RegMatch*
Regex_exec(Regex *self, const char *text, int flags, size_t *out_count)
{
	regmatch_t m[128];
	// be safe, don't trust regexec fills them all in all impls
	memset(m, -1, sizeof(m));

	int rflg = 0;
	if (flags & REGEX_XF_NOTBOL) rflg |= REG_NOTBOL;
	if (flags & REGEX_XF_NOTEOL) rflg |= REG_NOTEOL;

	int rc = regexec(&self->regex, text, sizeof(m)/sizeof(m[0]), m, rflg);
	if (rc != 0)
		return NULL;
	size_t count = 0;
	for (; count != sizeof(m)/sizeof(m[0]); ++count) {
		if (m[count].rm_so == (regoff_t)-1)
			break;
	}

	RegMatch *matches = malloc(count * sizeof(*matches));
	if (!matches)
		return NULL;

	for (size_t i = 0; i != count; ++i) {
		matches[i].so = (size_t)m[i].rm_so;
		matches[i].eo = (size_t)m[i].rm_eo;
	}

	if (out_count)
		*out_count = count;

	return matches;
}

bool
Regex_matches(Regex *self, const char *text, int flags)
{
	RegMatch *m = Regex_exec(self, text, flags, NULL);
	if (m) {
		free(m);
		return true;
	}
	return false;
}

Regex*
Regex_parse_full(const char *in_pattern,
                 int flags,
                 char **out_errstr,
                 Regex_flag_cb *for_unknown_flag,
                 void *opaque)
{
	char *errstr = NULL;
	char errbuf[256];
	char *pattern = parse_regex(in_pattern, &in_pattern);
	if (!pattern) {
		if (errno == EINVAL)
			errstr = "missing regex delimiter";
		else
			errstr = strerror(errno);
		goto out;
	}
	flags = parse_regex_flags_full(in_pattern, &in_pattern, flags,
	                               for_unknown_flag, opaque);
	if (*in_pattern) {
		snprintf(errbuf, sizeof(errbuf), "bad regex flag: %c\n",
		         *in_pattern);
		errstr = errbuf;
		goto out;
	}
	Regex *re = Regex_new(pattern, flags, out_errstr);
	free(pattern);
	return re;
out:
	free(pattern);
	*out_errstr = strdup(errstr);
	return NULL;
}

Regex*
Regex_parse(const char *in_pattern, int flags, char **out_errstr)
{
	return Regex_parse_full(in_pattern, flags, out_errstr, NULL, NULL);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	char *head;
	size_t len;
	int sub;
} REPortion;

struct RexReplace {
	Regex *regex;
	size_t  portion_length;
	VectorOf(REPortion*) portions;
};
#pragma clang diagnostic pop

static void
REPortion_pdestroy(void *ppself)
{
	free(*(void**)ppself);
}

static bool
RexReplace_addPortion(RexReplace *self, const char *txt, size_t len, int group)
{
	REPortion *portion = malloc(sizeof(*portion) + len + 1);
	if (!portion)
		return false;
	portion->head = (char*)(portion + 1);
	memcpy(portion->head, txt, len);
	portion->head[len] = 0;
	portion->len = len;
	portion->sub = group;
	Vector_push(&self->portions, &portion);
	self->portion_length += len;
	return true;
}

void
RexReplace_destroy(RexReplace *self)
{
	Regex_destroy(self->regex);
	Vector_destroy(&self->portions);
	free(self);
}

void
RexReplace_pdestroy(RexReplace **pself)
{
	RexReplace_destroy(*pself);
}

static inline bool
my_isdigit(int c) // because isdigit(3) is locale dependent
{
	return ((unsigned)(c-'0')) <= 9;
}


RexReplace*
RexReplace_new(const char *in_pattern, char **out_errstr)
{
	int err = EINVAL;
	char *pattern = NULL;
	char *buffer = NULL;
	RexReplace *self = NULL;
	const char *errstr = NULL;

	*out_errstr = NULL;

	if (*in_pattern == 's')
		++in_pattern;
	pattern = parse_regex(in_pattern, &in_pattern);
	if (!pattern) {
		if (errno == EINVAL) {
			errstr = "missing regex delimiter";
			goto out;
		}
		goto out_errno;
	}

	buffer = malloc(strlen(in_pattern)+1);
	if (!buffer)
		goto out_errno;

	self = u_malloc0(sizeof(*self));
	if (!self)
		goto out_errno;
	Vector_init_type(&self->portions, REPortion*);
	Vector_set_destructor(&self->portions, REPortion_pdestroy);

	--in_pattern; // shift back to the delimiter
	char delim = *in_pattern++;

	bool escaped = false;
	size_t at = 0;
	while (*in_pattern && (escaped || *in_pattern != delim)) {
		if (escaped) {
			escaped = false;
			if (my_isdigit(*in_pattern)) {
				long num;
				if (!parse_long(&in_pattern, &num)) {
					errstr = "bad numeric value";
					goto out;
				}
				if (num < 0) {
					errno = ERANGE;
					goto out_errno;
				}
				if (!RexReplace_addPortion(self, buffer, at,
				                           (int)num))
				{
					goto out_errno;
				}
				at = 0;
			} else {
				buffer[at++] = *in_pattern++;
			}
			continue;
		} else if (*in_pattern == '&') {
			// other way to get whole match (beside \0):
			if (!RexReplace_addPortion(self, buffer, at, 0))
				goto out_errno;
			at = 0;
			++in_pattern;
			continue;
		}
		escaped = *in_pattern == '\\';
		if (!escaped)
			buffer[at++] = *in_pattern;
		++in_pattern;
	}
	if (*in_pattern != delim) {
		errstr = "missing regex delimiter";
		goto out;
	}
	++in_pattern;
	if (at && !RexReplace_addPortion(self, buffer, at, -1))
		goto out_errno;
	free(buffer);
	buffer = NULL;

	int regflags = REG_EXTENDED;
	while (*in_pattern && *in_pattern == 'i') {
		regflags |= REG_ICASE;
		++in_pattern;
	}

	if (*in_pattern) {
		errstr = "trailing garbage after regular expression";
		goto out;
	}

	self->regex = Regex_new(pattern, regflags, out_errstr);
	if (!self->regex) {
		err = errno;
		errstr = NULL;
		goto out;
	}

	free(pattern);
	return self;

out_errno:
	err = errno;
	errstr = strerror(errno);
out:
	if (self) {
		Regex_destroy(self->regex);
		Vector_destroy(&self->portions);
		free(self);
	}
	free(pattern);
	free(buffer);
	if (errstr)
		*out_errstr = strdup(errstr);
	errno = err;
	return NULL;
}

char*
RexReplace_apply(RexReplace *self, const char *text)
{
	size_t mcount;
	RegMatch *m = Regex_exec(self->regex, text, 0, &mcount);
	if (!m)
		return strdup(text);

	size_t full_len = strlen(text);
	size_t length = full_len;
	if (m[0].eo < m[0].so) {
		free(m);
		errno = EFAULT;
		return NULL;
	}
	size_t cut = (size_t)(m[0].eo - m[0].so);
	length = cut < length ? length-cut : 0;
	length += self->portion_length;
	REPortion **pit;
	Vector_foreach(&self->portions, pit) {
		REPortion *it = *pit;
		if (it->sub < 0 || (size_t)it->sub >= mcount)
			continue;
		const RegMatch *grp = &m[it->sub];
		if (grp->eo <= grp->so)
			continue;
		size_t grplen = (size_t)(grp->eo - grp->so);
		length += grplen;
	}
	char *out = malloc(length+1);
	if (!out) {
		int saved_errno = errno;
		free(m);
		errno = saved_errno;
		return NULL;
	}

	size_t at = (size_t)m[0].so;
	memcpy(out, text, at);
	Vector_foreach(&self->portions, pit) {
		REPortion *it = *pit;
		memcpy(&out[at], it->head, it->len);
		at += it->len;

		if (it->sub < 0 || (size_t)it->sub >= mcount)
			continue;
		const RegMatch *grp = &m[it->sub];
		if (grp->eo <= grp->so)
			continue;
		size_t grplen = (size_t)(grp->eo - grp->so);
		memcpy(&out[at], &text[grp->so], grplen);
		at += grplen;
	}
	size_t tail = full_len - (size_t)m[0].eo;
	memcpy(&out[at], &text[m[0].eo], tail);
	at += tail;
	out[at] = 0;
	free(m);
	return out;
}

static char*
do_apply_xform_vec(char *base, Vector *opt_xform)
{
	RexReplace **it;
	Vector_foreach(opt_xform, it) {
		char *newbase = RexReplace_apply(*it, base);
		if (!newbase) {
			int err = errno;
			free(base);
			errno = err;
			return NULL;
		}
		free(base);
		base = newbase;
	}
	return base;
}

char*
apply_xform_vec(const char *name, Vector *opt_xform)
{
	char *base = strdup(name);
	if (!base)
		return NULL;
	return do_apply_xform_vec(base, opt_xform);
}

char*
opt_apply_xform(const char *filename, Vector *opt_xform)
{
	char *dir, *base;
	int rc = path_parts(filename, &dir, &base, PATH_PARTS_ALWAYS);
	if (rc < 0) {
		errno = -rc;
		return NULL;
	}
	if (!base) {
		// FIXME: this also happens for paths with trailing slashes
		free(dir);
		errno = EINVAL;
		return NULL;
	}

	base = do_apply_xform_vec(base, opt_xform);
	if (!base) {
		int err = errno;
		free(dir);
		errno = err;
		return NULL;
	}
	if (!dir)
		return base;
	char *out = make_path(dir, base, NULL);
	int err = errno;
	free(dir);
	free(base);
	errno = err;
	return out;
}
