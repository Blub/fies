libfies_dmthin_sources = files('''
	dmthin.c
	dmthin.h
	crc.c
	crc.h
	main.c
	main.h
'''.split())

libfies_dmthin = shared_library(
	'fies-dmthin',
	libfies_dmthin_sources,
	version : libfies_dmthin_version,
	install : true)

libfies_dmthin_pc = configure_file(
	input : 'fies-dmthin.pc.in',
	output : 'fies-dmthin.pc',
	configuration : substs)

install_data(libfies_dmthin_pc,
             install_dir : pkgconfiglibdir)
