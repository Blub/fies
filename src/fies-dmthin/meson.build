dmthin_sources = files('''
	fies_dmthin.c
	fies_dmthin.h
	dmthin_meta.c
'''.split())

dmthin_options_src = join_paths(doc_path, 'fies-dmthin.options')
dmthin_options_h = gen_options_header.process(dmthin_options_src)
dmthin_options_dep = declare_dependency(sources : dmthin_options_h)

dmthin = executable(
	'fies-dmthin',
	[dmthin_sources],
	link_with : [libcommon, libfies],
	dependencies : [dmthin_options_dep, libglib2, libdevmapper],
	install : true)
