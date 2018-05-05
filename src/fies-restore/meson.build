restore_sources = files('''
	fies_restore.c
'''.split())

restore_options_src = join_paths(doc_path, 'fies-restore.options')
restore_options_h = gen_options_header.process(restore_options_src)
restore_options_dep = declare_dependency(sources : restore_options_h)

restore = executable(
	'fies-restore',
	[restore_sources],
	link_with : [libcommon, libfies],
	dependencies : [restore_options_dep],
	install : true)
