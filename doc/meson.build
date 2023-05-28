perl = find_program('perl')

doc_path = meson.current_source_dir()

section_pl = join_paths(doc_path, 'section.pl')
section_fullcmd = [ '-I'+doc_path, '--', section_pl ]
gen_options_header = generator(
    perl,
    output : ['@PLAINNAME@.h'],
    arguments : section_fullcmd + [
        'format-options', 'c', '@INPUT@', '@OUTPUT@' ],
    )

manpages_gen = []
if want_doc
	foreach manentry : [
	        ['fies',         true],
	        ['fies-restore', true],
	        ['fies-dmthin',  want_dmthin],
	        ['fies-rbd',     want_rbd],
	        ['fies-zvol',    want_zvol],
	    ]
		if manentry[1]
			page = manentry[0]
			out_file = page + '.1'
			rst_file = page + '.rst'
			dep_file = rst_file + '.d'
			in_file  = rst_file + '.in'

			rst = custom_target(rst_file,
			    input : in_file,
			    output : rst_file,
			    depfile : dep_file,
			    command : [ perl, section_fullcmd, 'parse',
			                '-M@DEPFILE@', '@INPUT@', '@OUTPUT@' ],
			)

			manpages_gen += custom_target('manpage'+page,
			    input : rst,
			    output : out_file,
			    command : [ rst2man, '@INPUT@', '@OUTPUT@' ],
			    install_dir : man1dir,
			    install : true,
			)
		endif
	endforeach
endif
