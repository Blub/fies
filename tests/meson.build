test_common = files('''
	test_common.cc
	extents.h
	test.h
	util.h
	cppwriter.cc cppwriter.h
	memwriter.cc memwriter.h
	cppfile.cc   cppfile.h
	cppreader.cc cppreader.h
	memreader.cc memreader.h
	testfile.cc  testfile.h
	checkfile.cc checkfile.h
'''.split())

t1 = executable('t1', [test_common, 't1.cc'], link_with : libfies)
test('t1', t1)
t2 = executable('t2', [test_common, 't2.c'], link_with : libfies)
test('t2', t2)
