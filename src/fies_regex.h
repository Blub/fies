#ifndef FIES_SRC_CLI_REGEX_H
#define FIES_SRC_CLI_REGEX_H

#include "../lib/vector.h"

#define REGEX_F_ICASE   0x001
#define REGEX_F_NOSUB   0x002
#define REGEX_F_NEWLINE 0x004
#define REGEX_XF_NOTBOL 0x008
#define REGEX_XF_NOTEOL 0x010

// Parse a user-provided regular expression enclosed in delimiters and deal
// with escape sequences. eg: /a\/b/ becomes "a/b", so does @a/b@.
char* parse_regex(const char *pattern, const char **endptr);

typedef int Regex_flag_cb(void *opaque, char flag);
// Parse flags after a pattern, eg: /foo/i should call this on the 'i' portion.
// (the endptr returned by parse_regex()).
// This parses the following flags (captials reverse the flag)
//   i => REGEX_F_ICASE
//   n => REGEX_F_NOSUB
//   m => REGEX_F_NEWLINE
int parse_regex_flags(const char *pattern, const char **endptr, int init);
int parse_regex_flags_full(const char *pattern, const char **endptr, int init,
                           Regex_flag_cb *flag_cb,
                           void *opaque);

typedef struct {
	size_t so, eo;
} RegMatch;

typedef struct Regex Regex;
Regex*    Regex_new       (const char *pattern, int flags, char **out_err);
Regex*    Regex_parse     (const char *pattern, int flags, char **out_err);
Regex*    Regex_parse_full(const char *pattern, int flags, char **out_err,
                           Regex_flag_cb *for_unknown_flag,
                           void *opaque);
void      Regex_destroy   (Regex*);
void      Regex_pdestroy  (Regex**);
bool      Regex_matches   (Regex*, const char *text, int flags);
RegMatch* Regex_exec      (Regex*, const char *text, int flags, size_t *count);

typedef struct RexReplace RexReplace;
RexReplace* RexReplace_new     (const char *in_pattern, char **out_errstr);
void        RexReplace_destroy (RexReplace *self);
void        RexReplace_pdestroy(RexReplace **pself);
char*       RexReplace_apply   (RexReplace *self, const char *text);

char* apply_xform_vec(const char *base, Vector *opt_xform);
char *opt_apply_xform(const char *filename, VectorOf(RexReplace*) *opt_xform);

#endif
