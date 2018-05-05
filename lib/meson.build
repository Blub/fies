libfies_sources = files('''
	fies.h
	fies_writer.c
	fies_writer.h
	linux_file.c
	fies_linux.h
	fies_reader.c
	fies_reader.h
	vector.c
	vector.h
	map.c
	map.h
	emap.c
	emap.h
	util.c
	util.h
'''.split())

libfies = shared_library(
	'fies',
	libfies_sources,
	version : libfies_version,
	install : true)

libfies_pc = configure_file(
	input : 'fies.pc.in',
	output : 'fies.pc',
	configuration : substs)

install_data(libfies_pc,
             install_dir : pkgconfiglibdir)
