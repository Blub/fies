#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <inttypes.h>

#include "../lib/vector.h"
#include "../lib/fies.h"

#include "cli_common.h"
#include "util.h"
#include "fies_regex.h"
#include "filematch.h"
#include "fies_cli.h"

#define ERR_SKIPMSG 1

static const char *usage_msg =
"usage: fies <action> [options] [files... patterns...]\n\
Actions:\n\
  -c : create:  arguments are files\n\
  -x : extract: arguments are file patterns\n\
  -t : list:    arguments are file patterns\n\
Options:\n"
#include "fies.options.h"
;

static _Noreturn void
usage(FILE *out, int exit_code)
{
	fprintf(out, "%s", usage_msg);
	exit(exit_code);
}

#define OPT_CHROOT             (0x1000+'c')
#define OPT_CLONE              (0x2000+'c')
#define OPT_STRIP_COMPONENTS   (0x1000+'s')
#define OPT_DEREFERENCE        (0x1100+'l')
#define OPT_NO_DEREFERENCE     (0x1000+'l')
#define OPT_HARDLINKS          (0x1100+'h')
#define OPT_NO_HARDLINKS       (0x1000+'h')
#define OPT_XATTRS             (0x1100+'x')
#define OPT_NO_XATTRS          (0x1000+'x')
#define OPT_ACLS               (0x1100+'a')
#define OPT_NO_ACLS            (0x1000+'a')
#define OPT_ONE_FS             (0xf100+'x')
#define OPT_NO_ONE_FS          (0xf000+'x')
#define OPT_UID                (0x1100+'u')
#define OPT_GID                (0x1000+'g')
#define OPT_EXCLUDE            (0x2000+'x')
#define OPT_INCLUDE            (0x2000+'i')
#define OPT_REXCLUDE           (0x2100+'x')
#define OPT_RINCLUDE           (0x2100+'i')
#define OPT_XATTR_EXCLUDE      (0x3000+'x')
#define OPT_XATTR_INCLUDE      (0x3000+'i')
#define OPT_XATTR_REXCLUDE     (0x3100+'x')
#define OPT_XATTR_RINCLUDE     (0x3100+'i')
#define OPT_INCREMENTAL        (0x1100+'i')
#define OPT_NO_INCREMENTAL     (0x1000+'i')
#define OPT_REF_FILE           (0x1000+'r')
#define OPT_WILDCARDS          (0x1100+'w')
#define OPT_NO_WILDCARDS       (0x2100+'w')
#define OPT_WILD_SLASH         (0x1200+'w')
#define OPT_NO_WILD_SLASH      (0x2200+'w')
#define OPT_NULL               (0x1000+'0')
#define OPT_NO_NULL            (0x1100+'0')
#define OPT_XFORM_FILES_FROM   (0x1000+'T')
#define OPT_REF_FILES_FROM     (0x2000+'r')
#define OPT_XFORM_REF_FILES_FROM (0x2000+'T')
#define OPT_WARNING            (0x1000+'w')
#define OPT_TIME               (0x4000+'T')
#define OPT_DEBUG              (0x4000+'d')

#define FIES_SHORTOPTS "hvctxrRC:f:s:T:"
static struct option longopts[] = {
	{ "help",                     no_argument, NULL, 'h' },
	{ "file",               required_argument, NULL, 'f' },
	{ "recurse",                  no_argument, NULL, 'r' },
	{ "norecurse",                no_argument, NULL, 'R' },
	{ "no-recurse",               no_argument, NULL, 'R' },
	{ "no-recursion",             no_argument, NULL, 'R' },
	{ "cd",                 required_argument, NULL, 'C' },
	{ "directory",          required_argument, NULL, 'C' },
	{ "chroot",             required_argument, NULL, OPT_CHROOT },
	{ "strip-components",   required_argument, NULL, OPT_STRIP_COMPONENTS },
	{ "transform",          required_argument, NULL, 's' },
	{ "xform",              required_argument, NULL, 's' },
	{ "dereference",              no_argument, NULL, OPT_DEREFERENCE },
	{ "nodereference",            no_argument, NULL, OPT_NO_DEREFERENCE },
	{ "no-dereference",           no_argument, NULL, OPT_NO_DEREFERENCE },
	{ "hard-dereference",         no_argument, NULL, OPT_NO_HARDLINKS },
	{ "no-hard-dereference",      no_argument, NULL, OPT_HARDLINKS },
	{ "xattrs",                   no_argument, NULL, OPT_XATTRS },
	{ "noxattrs",                 no_argument, NULL, OPT_NO_XATTRS },
	{ "no-xattrs",                no_argument, NULL, OPT_NO_XATTRS },
	{ "acls",                     no_argument, NULL, OPT_ACLS },
	{ "noacls",                   no_argument, NULL, OPT_NO_ACLS },
	{ "no-acls",                  no_argument, NULL, OPT_NO_ACLS },
	{ "one-filesystem",           no_argument, NULL, OPT_ONE_FS },
	{ "one-file-system",          no_argument, NULL, OPT_ONE_FS },
	{ "no-one-filesystem",        no_argument, NULL, OPT_NO_ONE_FS },
	{ "no-one-file-system",       no_argument, NULL, OPT_NO_ONE_FS },
	{ "uid",                required_argument, NULL, OPT_UID },
	{ "gid",                required_argument, NULL, OPT_GID },
	{ "exclude",            required_argument, NULL, OPT_EXCLUDE },
	{ "rexclude",           required_argument, NULL, OPT_REXCLUDE },
	{ "include",            required_argument, NULL, OPT_INCLUDE },
	{ "rinclude",           required_argument, NULL, OPT_RINCLUDE },
	{ "xattr-exclude",      required_argument, NULL, OPT_XATTR_EXCLUDE },
	{ "xattr-rexclude",     required_argument, NULL, OPT_XATTR_REXCLUDE },
	{ "xattr-include",      required_argument, NULL, OPT_XATTR_INCLUDE },
	{ "xattr-rinclude",     required_argument, NULL, OPT_XATTR_RINCLUDE },
	{ "incremental",              no_argument, NULL, OPT_INCREMENTAL },
	{ "noincremental",            no_argument, NULL, OPT_NO_INCREMENTAL },
	{ "no-incremental",           no_argument, NULL, OPT_NO_INCREMENTAL },
	{ "ref-file",           required_argument, NULL, OPT_REF_FILE },
	{ "wildcards",                no_argument, NULL, OPT_WILDCARDS },
	{ "no-wildcards",             no_argument, NULL, OPT_NO_WILDCARDS },
	{ "wildcards-match-slash",    no_argument, NULL, OPT_WILD_SLASH },
	{ "no-wildcards-match-slash", no_argument, NULL, OPT_NO_WILD_SLASH},
	{ "clone",              required_argument, NULL, OPT_CLONE },
	{ "files-from",         required_argument, NULL, 'T' },
	{ "transforming-files-from",
	                        required_argument, NULL, OPT_XFORM_FILES_FROM},
	{ "xforming-files-from",required_argument, NULL, OPT_XFORM_FILES_FROM},
	{ "ref-files-from",     required_argument, NULL, OPT_REF_FILES_FROM },
	{ "transforming-ref-files-from",
	                        required_argument, NULL, OPT_XFORM_REF_FILES_FROM},
	{ "xforming-ref-files-from",
	                        required_argument, NULL, OPT_XFORM_REF_FILES_FROM},
	{ "null",                     no_argument, NULL, OPT_NULL },
	{ "no-null",                  no_argument, NULL, OPT_NO_NULL },
	{ "time",               required_argument, NULL, OPT_TIME },
	{ "warning",            required_argument, NULL, OPT_WARNING },
	{ "verbose",                  no_argument, NULL, 'v' },
	{ "quiet",                    no_argument, NULL, 'q' },
	{ "debug",                    no_argument, NULL, OPT_DEBUG },
	{ NULL, 0, NULL, 0 }
};

static int                   opt_mode             = 0;
static int                   opt_debug            = 0;
static const char           *opt_file             = NULL;
bool                         opt_recurse          = true;
static const char           *opt_directory        = NULL;
static const char           *opt_chroot           = NULL;
size_t                       opt_strip_components = 0;
bool                         opt_dereference      = false;
bool                         opt_hardlinks        = true;
bool                         opt_incremental      = false;
static bool                  opt_xattrs           = false;
static bool                  opt_acls             = false;
bool                         opt_noxdev           = false;
long                         opt_uid              = -1;
long                         opt_gid              = -1;
static bool                  opt_wildcards        = false;
static bool                  opt_wildcards_slash  = false;
clone_mode_t                 opt_clone            = CLONE_AUTO;
VectorOf(RexReplace*)        opt_xform;
static VectorOf(FileMatch)   opt_exclude;
static VectorOf(FileMatch)   opt_include;
static VectorOf(const char*) opt_xattr_exclude;
static VectorOf(Regex*)      opt_xattr_rexclude;
static VectorOf(const char*) opt_xattr_include;
static VectorOf(Regex*)      opt_xattr_rinclude;
static VectorOf(const char*) opt_ref_files;
static bool                  opt_null             = false;
VectorOf(from_file_t)        opt_files_from_list;
VectorOf(from_file_t)        opt_ref_files_from_list;
static enum {
	TIME_LOCALE,
	TIME_RFC2822,
	TIME_RFC822,
	TIME_STAMP,
	TIME_NONE
}                            opt_time = TIME_LOCALE;

static bool option_error = false;

static int   stream_fd = -1;
uint32_t     fies_flags = 0;
clone_info_t clone_info = { 0, 0, 0 };

static void
handle_re_opt(const char *pattern, Vector *dest)
{
	char *err = NULL;
	Regex *re = Regex_parse(pattern, 0, &err);
	if (re) {
		Vector_push(dest, &re);
	} else {
		fprintf(stderr, "fies: regex error: %s\n", err);
		free(err);
		option_error = true;
	}
}

// Convert uppercase to lowercase and underscores to dashes to compare our
// enum values (WARN_ABSOLUTE_PATHS == "absolute-paths")
static inline char
name_tolower(char c)
{
	if (c >= 'A' && c <= 'Z')
		return (char)((int)c-'A'+'a');
	if (c == '_')
		return '-';
	return c;
}

static bool
str_equals_name(const char *str, const char *name)
{
	while (*str) {
		if (!*name || *str != name_tolower(*name))
			return false;
		++str;
		++name;
	}
	if (*name)
		return false;
	return true;
}

static void
handle_warning_opt(const char *w)
{
	bool off = strncmp(w, "no-", 3) == 0;
	if (off)
		w += 3;

	if (!strcmp(w, "help") || !strcmp(w, "?")) {
		char buffer[32];
		for (const char *const *wi = warn_names; *wi; ++wi) {
			size_t i = 0;
			for (; (*wi)[i]; ++i)
				buffer[i] = name_tolower((*wi)[i]);
			buffer[i] = 0;
			fprintf(stdout, "  --warning=%s\n", buffer);
		}
		exit(EXIT_SUCCESS);
	}

	unsigned int flag = 1;
	for (size_t i = 0; warn_names[i]; ++i, flag <<= 1) {
		if (str_equals_name(w, warn_names[i])) {
			if (off)
				common.warn_enable &= ~flag;
			else
				common.warn_enable |= flag;
			return;
		}
	}
	fprintf(stderr, "fies: unknown warning: '%s', use --warning=help\n",
	        w);
	exit(EXIT_FAILURE);
}

static void
handle_option(int c, int oopt, const char *oarg)
{
	switch (c) {
	case 'c':
	case 't':
	case 'x':
		opt_mode = c;
		break;

	case 'v': ++common.verbose; break;
	case 'q':
		--common.verbose;
		++common.quiet;
		break;

	case 'f': opt_file = oarg; break;
	case 'r': opt_recurse = true; break;
	case 'R': opt_recurse = false; break;
	case 'C': opt_directory = oarg; break;

	case OPT_CHROOT: opt_chroot = oarg; break;

	case OPT_STRIP_COMPONENTS: {
		long num = 0;
		if (!arg_stol(oarg, &num, "--strip-components", "fies"))
			option_error = true;
		else if (num < 0) {
			fprintf(stderr, "fies: --strip-components:"
			        " must be 0 or positive\n");
			option_error = true;
		}
		opt_strip_components = (size_t)num;
		break;
	}
	case 's': {
		char *errstr = NULL;
		// FIXME: Include the extra file related options here as well.
		RexReplace *xform = RexReplace_new(oarg, &errstr);
		if (!xform) {
			fprintf(stderr, "fies: %s\n", errstr);
			free(errstr);
			option_error = true;
		} else {
			Vector_push(&opt_xform, &xform);
		}
		break;
	}
	case OPT_DEREFERENCE:        opt_dereference = true; break;
	case OPT_NO_DEREFERENCE:     opt_dereference = false; break;
	case OPT_HARDLINKS:          opt_hardlinks = true; break;
	case OPT_NO_HARDLINKS:       opt_hardlinks = false; break;
	case OPT_XATTRS:             opt_xattrs = true; break;
	case OPT_NO_XATTRS:          opt_xattrs = false; break;
	case OPT_ACLS:               opt_acls = true; break;
	case OPT_NO_ACLS:            opt_acls = false; break;
	case OPT_ONE_FS:             opt_noxdev = true; break;
	case OPT_NO_ONE_FS:          opt_noxdev = false; break;
	case OPT_INCREMENTAL:        opt_incremental = true; break;
	case OPT_NO_INCREMENTAL:     opt_incremental = false; break;
	case OPT_WILDCARDS:          opt_wildcards = true; break;
	case OPT_NO_WILDCARDS:       opt_wildcards = false; break;
	case OPT_WILD_SLASH:         opt_wildcards_slash = true; break;
	case OPT_NO_WILD_SLASH:      opt_wildcards_slash = false; break;
	case OPT_NULL:               opt_null = true; break;
	case OPT_NO_NULL:            opt_null = false; break;
	case OPT_REF_FILE:
		Vector_push(&opt_ref_files, &oarg);
		break;
	case OPT_UID:
		if (!arg_stol(oarg, &opt_uid, "--uid", "fies"))
			option_error = true;
		break;
	case OPT_GID:
		if (!arg_stol(oarg, &opt_gid, "--gid", "fies"))
			option_error = true;
		break;
	case OPT_EXCLUDE: {
		FileMatch entry = {
			.flags = 0,
			.glob = oarg
		};
		Vector_push(&opt_exclude, &entry);
		break;
	}
	case OPT_REXCLUDE:
		if (!handle_file_re_opt(oarg, &opt_exclude, "fies"))
			option_error = true;
		break;
	case OPT_INCLUDE: {
		int flags = opt_wildcards_slash ? FMATCH_WILDCARD_SLASH : 0;
		FileMatch entry = {
			.flags = flags,
			.glob = oarg
		};
		Vector_push(&opt_include, &entry);
		break;
	}
	case OPT_RINCLUDE:
		if (!handle_file_re_opt(oarg, &opt_include, "fies"))
			option_error = true;
		break;
	case OPT_XATTR_EXCLUDE:
		Vector_push(&opt_xattr_exclude, &oarg);
		break;
	case OPT_XATTR_REXCLUDE:
		handle_re_opt(oarg, &opt_xattr_rexclude);
		break;
	case OPT_XATTR_INCLUDE:
		Vector_push(&opt_xattr_include, &oarg);
		break;
	case OPT_XATTR_RINCLUDE:
		handle_re_opt(oarg, &opt_xattr_rinclude);
		break;
	case OPT_CLONE:
		if (strcmp(oarg, "force") == 0)
			opt_clone = CLONE_FORCE;
		else if (strcmp(oarg, "auto") == 0)
			opt_clone = CLONE_AUTO;
		else if (strcmp(oarg, "never") == 0)
			opt_clone = CLONE_NEVER;
		else {
			fprintf(stderr,
			        "fies: invalid clone option: %s\n"
			        "      must be 'always', 'auto' or 'never'\n",
			        oarg);
			option_error = true;
		}
		break;
	case 'T': {
		from_file_t entry = { .file = oarg, .transforming = false };
		Vector_push(&opt_files_from_list, &entry);
		break;
	}
	case OPT_XFORM_FILES_FROM: {
		from_file_t entry = { .file = oarg, .transforming = true };
		Vector_push(&opt_files_from_list, &entry);
		break;
	}
	case OPT_REF_FILES_FROM: {
		from_file_t entry = { .file = oarg, .transforming = false };
		Vector_push(&opt_ref_files_from_list, &entry);
		break;
	}
	case OPT_XFORM_REF_FILES_FROM: {
		from_file_t entry = { .file = oarg, .transforming = true };
		Vector_push(&opt_ref_files_from_list, &entry);
		break;
	}

	case OPT_TIME:
		if (strcasecmp(oarg, "none") == 0)
			opt_time = TIME_NONE;
		else if (strcasecmp(oarg, "stamp") == 0)
			opt_time = TIME_STAMP;
		else if (strcasecmp(oarg, "local") == 0 ||
		         strcasecmp(oarg, "locale") == 0)
		{
			opt_time = TIME_LOCALE;
		}
		else if (strcasecmp(oarg, "rfc") == 0 ||
		         strcasecmp(oarg, "rfc2822") == 0 ||
		         strcasecmp(oarg, "2822") == 0)
		{
			opt_time = TIME_RFC2822;
		}
		else if (strcasecmp(oarg, "rfc") == 0 ||
		         strcasecmp(oarg, "rfc822") == 0 ||
		         strcasecmp(oarg, "822") == 0)
		{
			opt_time = TIME_RFC822;
		}
		else {
			fprintf(stderr, "fies: unrecognized time format: %s\n",
			        oarg);
			usage(stderr, EXIT_FAILURE);
		}
		break;

	case OPT_WARNING:
		handle_warning_opt(oarg);
		break;
	case OPT_DEBUG:
		opt_debug++;
		break;
	case 'h':
		usage(stdout, EXIT_SUCCESS);
	case '?':
		fprintf(stderr, "fies: unrecognized option: %c\n", oopt);
		usage(stderr, EXIT_FAILURE);
	default:
		fprintf(stderr, "fies: option error\n");
		usage(stderr, EXIT_FAILURE);
	}
}

size_t
display_time(char *buf, size_t bufsz, const struct fies_time *tm)
{
	time_t secs = (time_t)tm->secs;
	struct tm timedata;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcovered-switch-default"
	switch (opt_time) {
	case TIME_NONE:
		*buf = 0;
		return 0;
	case TIME_STAMP: {
		int rc = snprintf(buf, bufsz,
		                  "%" PRI_D_FIES_SECS
		                  ".%" PRI_D_FIES_NSECS,
		                  tm->secs, tm->nsecs);
		return rc < 0 ? 0 : (size_t)rc;
	}
	case TIME_RFC2822:
		if (!localtime_r(&secs, &timedata))
			return 0;
		return strftime(buf, bufsz, "%a, %d %b %Y %T %z", &timedata);
	case TIME_RFC822:
		if (!localtime_r(&secs, &timedata))
			return 0;
		return strftime(buf, bufsz, "%a, %d %b %y %T %z", &timedata);
	default:
	case TIME_LOCALE:
		if (!localtime_r(&secs, &timedata))
			return 0;
		return strftime(buf, bufsz, "%c", &timedata);
	}
#pragma clang diagnostic pop
}

static bool
open_stream(int mode, int default_file)
{
	if (!opt_file || !strcmp(opt_file, "-")) {
		stream_fd = default_file;
		return true;
	}
	stream_fd = open(opt_file, mode, 0666);
	if (stream_fd < 0) {
		fprintf(stderr, "fies: open(%s): %s\n",
		        opt_file, strerror(errno));
		return false;
	}
	return true;
}

static inline void
close_stream()
{
	if (stream_fd != -1)
		close(stream_fd);
}

static inline bool
change_root()
{
	if (!opt_chroot)
		return true;
	if (chroot(opt_chroot) != 0) {
		fprintf(stderr, "fies: chroot(%s): %s\n",
		        opt_chroot, strerror(errno));
		return false;
	}
	if (chdir("/") != 0) {
		fprintf(stderr, "fies: chdir(/) in chroot: %s\n",
		        strerror(errno));
		return false;
	}
	return true;
}

static inline bool
change_directory()
{
	if (!change_root())
		return false;
	if (!opt_directory)
		return true;
	if (chdir(opt_directory) != 0) {
		fprintf(stderr, "fies: chdir(%s): %s\n",
			opt_directory, strerror(errno));
		return false;
	}
	return true;
}

static bool
path_matches_file_match(char *path,
                        size_t pathlen,
                        mode_t st_mode,
                        Vector *vec,
                        bool head)
{
	FileMatch *it;
	Vector_foreach(vec, it) {
		const char *part;
		size_t pi = pathlen;
		if (head) {
			char *tok = strchr(path, '/');
			while (tok) {
				*tok = 0;
				if (FileMatch_matches(it, path, st_mode)) {
					*tok = '/';
					return true;
				}
				*tok = '/';
				tok = strchr(tok+1, '/');
			}
			if (FileMatch_matches(it, path, st_mode))
				return true;
		} else {
			while ( (part = next_tail_component(path, &pi)) ) {
				if (FileMatch_matches(it, part, st_mode))
					return true;
			}
		}
	}
	return false;
}

bool
opt_is_path_excluded(const char *in_path,
                     mode_t perms,
                     bool skipincludes,
                     bool head)
{
	char *path;
	char pathbuf[PATH_MAX];
	size_t len = strlen(in_path);

	if (len >= sizeof(pathbuf)) {
		fprintf(stderr, "fies: path too long: %s\n", in_path);
		return true;
	}

	if (head) {
		path = pathbuf;
		memcpy(pathbuf, in_path, len+1);
	} else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
		path = (char*)in_path;
#pragma clang diagnostic pop
	}

	if (!skipincludes &&
	    !Vector_empty(&opt_include) &&
	    !path_matches_file_match(path, len, perms, &opt_include, head))
	{
		return true;
	}
	return path_matches_file_match(path, len, perms, &opt_exclude, head);
}

static bool
xattr_matches(const char *name, Vector *fixed, Vector *res)
{
	const char **sit;
	Regex **rit;
	Vector_foreach(fixed, sit) {
		if (!strcmp(*sit, name))
			return true;
	}
	Vector_foreach(res, rit) {
		if (Regex_matches(*rit, name, 0))
			return true;
	}
	return false;
}

bool
opt_is_xattr_excluded(const char *name)
{
	if ((!Vector_empty(&opt_xattr_include) ||
	     !Vector_empty(&opt_xattr_rinclude)) &&
	    !xattr_matches(name, &opt_xattr_include, &opt_xattr_rinclude))
	{
		return true;
	}
	return xattr_matches(name, &opt_xattr_exclude, &opt_xattr_rexclude);
}

static void
main_cleanup()
{
	close_stream();
	Vector_destroy(&opt_files_from_list);
	Vector_destroy(&opt_ref_files_from_list);
	Vector_destroy(&opt_exclude);
	Vector_destroy(&opt_include);
	Vector_destroy(&opt_xattr_exclude);
	Vector_destroy(&opt_xattr_rexclude);
	Vector_destroy(&opt_xattr_include);
	Vector_destroy(&opt_xattr_rinclude);
	Vector_destroy(&opt_xform);
	Vector_destroy(&opt_ref_files);
}

static int
create_add_from_list(FiesWriter *fies, const char *listfile, bool xforming,
                     bool as_ref)
{
	int fd = open(listfile, O_RDONLY);
	if (fd < 0) {
		showerr("fies: open(%s): %s\n", listfile, strerror(errno));
		return ERR_SKIPMSG;
	}

	int rc = ERR_SKIPMSG;
	char frombuf[PATH_MAX];
	char buffer[PATH_MAX+2];
	char *from = xforming ? frombuf : buffer;
	size_t fill = 0;
	size_t bi = 0;
	while (true) {
		ssize_t got = read(fd, buffer+fill, sizeof(buffer)-fill);
		if (got < 0) {
			showerr("fies: read: %s\n", strerror(errno));
			goto out;
		}
		if (!got)
			break;

		fill += (size_t)got;
		while (true) {
			char *eol = memchr(buffer, opt_null ? 0 : '\n', fill);
			if (!eol)
				break;

			size_t len = (size_t)(eol - buffer);
			*eol = 0;
			if (len > PATH_MAX) {
				showerr("fies: path too long: %s\n", buffer);
				goto out;
			}

			if (!len && !bi)
				break;

			++bi;
			if (bi > 1 || !xforming) {
				const char *xform = NULL;
				if (xforming && len)
					xform = buffer;
				rc = do_create_add(fies, AT_FDCWD, from, from,
				                   0, xform, as_ref);
				if (rc < 0)
					goto out;
				rc = ERR_SKIPMSG; // reset error code
			} else if (xforming) {
				memcpy(frombuf, buffer, len+1);
			}
			memcpy(buffer, eol+1, fill-len-1);
			fill -= len+1;
			bi %= 2;
		}
	}
	if (fill) {
		if (xforming && !bi) {
			warn(WARN_TRUNCATED_FILE_LIST,
			     "fies: truncated file list\n");
		} else {
			buffer[fill] = 0;
			const char *xform = NULL;
			if (xforming && fill)
				xform = buffer;
			rc = do_create_add(fies, AT_FDCWD, from, from, 0,
			                   xform, as_ref);
			if (rc < 0)
				goto out;
			rc = ERR_SKIPMSG; // reset error code
		}
	}

	rc = 0;

out:
	close(fd);
	return rc;
}

static int
fies_cli_create(int argc, char **argv)
{
	create_init();
	if (argc < 1 && Vector_empty(&opt_files_from_list)) {
		fprintf(stderr, "fies: create: missing files\n");
		usage(stderr, EXIT_FAILURE);
	}

	if (!open_stream(O_WRONLY | O_CREAT | O_TRUNC, STDOUT_FILENO))
		return 1;

	if (!change_directory())
		return 1;

	struct FiesWriter *fies = FiesWriter_newFull(&create_writer_funcs,
	                                             &stream_fd,
	                                             FIES_F_WHOLE_FILES);
	if (!fies) {
		fprintf(stderr, "fies: failed to create fies writer: %s\n",
		        strerror(errno));
		return 1;
	}

	int rc = 0;
	const char *err;

	const char **refpp;
	Vector_foreach(&opt_ref_files, refpp) {
		rc = create_add(fies, *refpp, true);
		if (rc < 0)
			goto out_errmsg;
	}

	from_file_t *fit;
	Vector_foreach(&opt_ref_files_from_list, fit) {
		rc = create_add_from_list(fies, fit->file, fit->transforming,
		                          true);
		if (rc == ERR_SKIPMSG)
			goto out;
		if (rc < 0)
			goto out_errmsg;
	}
	Vector_foreach(&opt_files_from_list, fit) {
		rc = create_add_from_list(fies, fit->file, fit->transforming,
		                          false);
		if (rc == ERR_SKIPMSG)
			goto out;
		if (rc < 0)
			goto out_errmsg;
	}

	for (int i = 0; i != argc; ++i) {
		rc = create_add(fies, argv[i], false);
		if (rc < 0)
			goto out_errmsg;
	}

	goto out;

out_errmsg:
	err = FiesWriter_getError(fies);
	if (!err)
		err = strerror(-rc);
	fprintf(stderr, "fies: %s\n", err);

out:
	FiesWriter_delete(fies);

	return rc == 0 ? 0 : 1;
}

static void
debug_show_packet(void *opaque, const struct fies_packet *packet)
{
	(void)opaque;
	(void)packet;
	fprintf(stderr, "packet type=0x%04x size=%" PRIu64"\n",
	        (unsigned)packet->type,
	        packet->size);
}

static int
fies_cli_extract(int argc, char **argv, bool list_only)
{
	int matchflags = 0;
	if (opt_wildcards) {
		if (opt_wildcards_slash)
			matchflags = FMATCH_WILDCARD_SLASH;
	} else {
		matchflags = FMATCH_FIXED_TEXT;
	}
	for (int i = 0; i != argc; ++i) {
		FileMatch entry = {
			.flags = matchflags,
			.glob = argv[i]
		};
		Vector_push(&opt_include, &entry);
	}

	if (!open_stream(O_RDONLY, STDIN_FILENO))
		return 1;

	if (!change_directory())
		return 1;

	// FIXME: We need to honor the incremental flag instead of rejecting it
	struct FiesReader_Funcs *funcs = list_only ? &list_reader_funcs
	                                           : &extract_reader_funcs;
	if (opt_debug) {
		funcs->dbg_packet = debug_show_packet;
	}
	struct FiesReader *fies =
		FiesReader_newFull(funcs, &stream_fd, 0, 0);
	if (!fies) {
		fprintf(stderr, "fies: failed to create fies reader: %s\n",
		        strerror(errno));
		return 1;
	}

	int rc = FiesReader_readHeader(fies);
	if (!rc) {
		fies_flags = FiesReader_flags(fies);
		rc = 1;
	}
	while (rc > 0) {
		rc = FiesReader_iterate(fies);
	}

	if (rc < 0) {
		const char *err = FiesReader_getError(fies);
		if (!err)
			err = strerror(-rc);
		fprintf(stderr, "fies: %s\n", err);
	}
	FiesReader_delete(fies);

	if (rc != 0)
		return 1;

	if (!common.quiet && clone_info.unshared) {
		char ssh[32];
		char sh[32];
		char cp[32];
		format_size((unsigned long long)clone_info.stream_shared,
		            ssh, sizeof(ssh));
		format_size((unsigned long long)clone_info.shared,
		            sh, sizeof(sh));
		format_size((unsigned long long)clone_info.unshared,
		            cp, sizeof(cp));
		fprintf(stderr, "fies: data shared in the stream: %s\n", ssh);
		fprintf(stderr, "fies:       successfully cloned: %s\n", sh);
		fprintf(stderr, "fies:                    copied: %s\n", cp);
	}

	return 0;
}

// support for tar's weird special case: if no -c, -t, or -x have yet been
// used parse the first argument as a list of options, potentially eating up
// another option argument (eg. `fies cf foo.fies ...`).
static int
parse_compat_option(int argc, char **argv)
{
	if (!argc)
		return 0;

	if (!argv[0][0] || argv[0][0] == '-') {
		// Empty string as parameter, or we have a '-' which would be
		// weird because this should have been parsed already...
		usage(stderr, EXIT_FAILURE);
	}

	// Call handle_option on all the letters, if they take a parameter
	// (followed by a ':' in the FIES_SHORTOPTS string) we need to either
	// eat the rest of the argument, or eat an additional one...
	int consumed = 0;
	const char *argstr = argv[consumed];
	while (!consumed) {
		char *opt = strchr(FIES_SHORTOPTS, argstr[0]);
		if (!opt)
			handle_option('?', argstr[0], NULL);
		if (opt[1] == ':') {
			if (argstr[1]) {
				// `fNAME`
				handle_option(*opt, *opt, argstr+1);
			} else if (++consumed == argc) {
				// `f` (no further arguments)
				fprintf(stderr, "fies: missing parameter\n");
				usage(stderr, EXIT_FAILURE);
			} else {
				// `f NAME` (name as separate argv entry)
				handle_option(*opt, *opt, argv[consumed]);
			}
			argstr = argv[++consumed];
		} else {
			handle_option(*opt, *opt, NULL);
			++argstr;
		}
	}
	return consumed;
}

int
main(int argc, char **argv)
{
	Vector_init_type(&opt_exclude, FileMatch);
	Vector_set_destructor(&opt_exclude,
	                      (Vector_dtor*)FileMatch_destroy);

	Vector_init_type(&opt_include, FileMatch);
	Vector_set_destructor(&opt_include,
	                      (Vector_dtor*)FileMatch_destroy);

	Vector_init_type(&opt_xattr_exclude, const char*);

	Vector_init_type(&opt_xattr_rexclude, Regex*);
	Vector_set_destructor(&opt_xattr_rexclude,
	                      (Vector_dtor*)Regex_pdestroy);

	Vector_init_type(&opt_xattr_include, const char*);

	Vector_init_type(&opt_xattr_rinclude, Regex*);
	Vector_set_destructor(&opt_xattr_rinclude,
	                      (Vector_dtor*)Regex_pdestroy);

	Vector_init_type(&opt_xform, RexReplace*);
	Vector_set_destructor(&opt_xform, (Vector_dtor*)RexReplace_pdestroy);

	Vector_init_type(&opt_files_from_list, from_file_t);
	Vector_init_type(&opt_ref_files_from_list, from_file_t);

	Vector_init_type(&opt_ref_files, const char*);

	atexit(main_cleanup);

	while (true) {
		int index = 0;
		int c = getopt_long(argc, argv, FIES_SHORTOPTS, longopts,
		                    &index);
		if (c == -1)
			break;
		handle_option(c, optopt, optarg);
	}

	if (option_error)
		usage(stderr, EXIT_FAILURE);

	if (optind > argc)
		optind = argc;
	argc -= optind;
	argv += optind;

	if (!opt_mode) {
		int consumed = parse_compat_option(argc, argv);
		argc -= consumed;
		argv += consumed;
	}

	if (opt_mode != 'c') {
		if (!Vector_empty(&opt_ref_files)) {
			fprintf(stderr,
			        "fies: --ref-file option can only be used "
			        " when creating an archive\n");
			return 1;
		}
	}

	int rc;
	if (opt_mode == 'c')
		rc = fies_cli_create(argc, argv);
	else if (opt_mode == 'x') {
		rc = fies_cli_extract(argc, argv, false);
	} else if (opt_mode == 't') {
		rc = fies_cli_extract(argc, argv, true);
	} else {
		fprintf(stderr, "fies: missing mode of operation\n");
		usage(stderr, EXIT_FAILURE);
	}
	return rc ? rc : common.error_count ? 1 : 0;
}
