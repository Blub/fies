project(
	'fies', 'c',
	version : '0.0.6',
	default_options : [
		'c_std=c17',
		'prefix=/usr',
	],
	meson_version : '>= 0.40',
	)

libfies_version = '0.1.0'
libfies_dmthin_version = '0.1.0'

add_global_arguments('-D_GNU_SOURCE', language : 'c')

conf = configuration_data()
conf.set_quoted('PACKAGE_VERSION', meson.project_version())

prefixdir = get_option('prefix')
bindir = join_paths(prefixdir, get_option('bindir'))
libdir = join_paths(prefixdir, get_option('libdir'))
datadir = join_paths(prefixdir, get_option('datadir'))
mandir = join_paths(prefixdir, get_option('mandir'))
man1dir = join_paths(mandir, 'man1')
pkgconfiglibdir = join_paths(libdir, 'pkgconfig')
includedir = join_paths(prefixdir, get_option('includedir'))

# ... hmm
substs = configuration_data()
substs.set_quoted('PACKAGE_VERSION', meson.project_version())
substs.set('PREFIX',     prefixdir)
substs.set('BINDIR',     bindir)
substs.set('LIBDIR',     libdir)
substs.set('INCLUDEDIR', includedir)

cc = meson.get_compiler('c')
pkgconfig = import('pkgconfig')

use_cxx = false
cxx = []
want_tests = get_option('tests')
if want_tests != 'false'
	add_languages('cpp')
	cxx = meson.get_compiler('cpp')
	use_cxx = true
endif

foreach arg : [
    '-Wall',
    '-Wextra',
    '-Weverything',
    '-Werror',
    ]
	if cc.has_argument(arg)
		add_project_arguments(arg, language : 'c')
	endif
	if use_cxx and cxx.has_argument(arg)
		add_project_arguments(arg, language : 'cpp')
	endif
endforeach

foreach arg : [
    'unknown-pragmas',
    'pragmas',
    'c++98-compat',
    'c++98-compat-pedantic',
    'disabled-macro-expansion',
    'declaration-after-statement',
    ]
	if cc.has_argument('-W' + arg)
		add_project_arguments('-Wno-' + arg, language : 'c')
	endif
	if use_cxx and cxx.has_argument('-W' + arg)
		add_project_arguments('-Wno-' + arg, language : 'cpp')
	endif
endforeach

if use_cxx
	foreach arg : [
	    'weak-vtables',
	]
		if cxx.has_argument('-W' + arg)
			add_project_arguments('-Wno-' + arg, language : 'cpp')
		endif
	endforeach
endif

if not cc.has_header_symbol('sys/types.h', 'major')
	if cc.has_header_symbol('sys/mkdev.h', 'major')
		conf.set('MAJOR_IN_MKDEV', true)
		conf.set('MAJOR_IN_SYSMACROS', false)
	elif cc.has_header_symbol('sys/sysmacros.h', 'major')
		conf.set('MAJOR_IN_SYSMACROS', true)
		conf.set('MAJOR_IN_MKDEV', false)
	else
		error('failed to find "major" macro')
	endif
endif

if host_machine.system() == 'linux'
	conf.set('USE_READDIR_R', false)
	conf.set('READLINKAT_EMPTY_PATH', true)
else
	conf.set('USE_READDIR_R', true)
	conf.set('READLINKAT_EMPTY_PATH', false)
endif
#  /* #undef CONFIG_BIG_ENDIAN */

want_dmthin = get_option('dmthin')
want_rbd = get_option('rbd')
want_zvol = get_option('zvol')

libglib2 = dependency('glib-2.0',
	required : ( want_dmthin == 'true' or
	             want_rbd == 'true'
	           )
	)

want_doc = get_option('doc')
if want_doc != 'false'
	rst2man = find_program('rst2man', required : want_doc == 'true')
	have = rst2man.found()
else
	rst2man = []
	have = rst2man.found()
endif
want_doc = have

if want_dmthin != 'false'
	libdevmapper = dependency('devmapper',
	                          required : want_dmthin == 'true')
	have = libdevmapper.found()
else
	have = false
	libdevmapper = []
endif
want_dmthin = have
conf.set10('HAVE_DEVMAPPER', have)

if want_rbd != 'false'
	required = want_rbd == 'true'
	librbd = cc.find_library('rbd', required : required)
	librados = cc.find_library('rados', required : required)
	if required and not (librbd.found() and librados.found())
		error('librbd support not available')
	endif
	have = librbd.found()
else
	have = false
	librbd = []
	librados = []
endif
want_rbd = have
conf.set10('HAVE_RBD', have)

if want_zvol != 'false'
	libzfs = dependency('libzfs', required : want_zvol == 'true')
	have = libzfs.found()
	if have
		libzpool = cc.find_library('zpool')
		libnvpair = cc.find_library('nvpair')
	endif
else
	have = false
	libzfs = []
	libzpool = []
	libnvpair = []
endif
want_zvol = have
conf.set10('HAVE_ZFS', have)

config_h = configure_file(
    output : 'config.h',
    configuration : conf)
add_project_arguments('-include', 'config.h', language : 'c')

subdir('doc')
subdir('include')
subdir('lib')
subdir('libfies-dmthin')
subdir('src')
subdir('tests')
