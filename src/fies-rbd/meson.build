rbd_sources = files('''
	fies_rbd.c
'''.split())

rbd_options_src = join_paths(doc_path, 'fies-rbd.options')
rbd_options_h = gen_options_header.process(rbd_options_src)
rbd_options_dep = declare_dependency(sources : rbd_options_h)

rbd = executable(
	'fies-rbd',
	[rbd_sources],
	link_with : [libcommon, libfies],
	dependencies : [rbd_options_dep, libglib2, librbd, librados],
	install : true)
