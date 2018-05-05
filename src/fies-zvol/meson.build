zvol_sources = files('''
	fies_zvol.c
'''.split())

zvol_options_src = join_paths(doc_path, 'fies-zvol.options')
zvol_options_h = gen_options_header.process(zvol_options_src)
zvol_options_dep = declare_dependency(sources : zvol_options_h)

zvol = executable(
	'fies-zvol',
	[zvol_sources],
	link_with : [libcommon, libfies],
	dependencies : [zvol_options_dep, libzfs, libzpool, libnvpair],
	install : true)
