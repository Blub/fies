libcommon_sources = files('''
	cli_common.c
	util.c
	regex.c
	filematch.c
'''.split())

libcommon = static_library(
	'common',
	libcommon_sources,
	install : false)

fies_sources = files('''
	fies.c
	fies_create.c
	fies_extract.c
	warnlist.h
'''.split())

fies_options_src = join_paths(doc_path, 'fies.options')
fies_options_h = gen_options_header.process(fies_options_src)
fies_options_dep = declare_dependency(sources : fies_options_h)

fies = executable(
	'fies',
	[fies_sources],
	link_with : [libcommon, libfies],
	dependencies : fies_options_dep,
	install : true)

subdir('fies-restore')
if want_dmthin
	subdir('fies-dmthin')
endif
if want_zvol
	subdir('fies-zvol')
endif
if want_rbd
	subdir('fies-rbd')
endif
