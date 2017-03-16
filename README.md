fies - A File Extent Streaming Tool
===================================

fies is a library and command line tool to stream file extents in a sparse and
clone aware fashion, with utilities to export block devices with snapshots from
various storages into fiestreams and restore them elsewhere.

For licensing information see the COPYING file.
For information on how to contribute see the CONTRIBUTING file.

Tools
-----

* fies
    create, list and extract contents of a fiestream
* fies-restore
    restore a volume/snapshot fiestream to a file or device with snapshots
* fies-dmthin
    create a fiestream from dmthin volumes and their snapshots
* fies-rbd
    create a fiestream from rbd devices with snapshots
* fies-zvol
    create a fiestream from zvols with snapshots
* libfies
    library to handle the fies format

Installation
------------

The usual procedure, with the usual parameters and variables.

See `./configure --help` for detailed information about the various optional
parts of the project.

The general procedure is:

    ./configure --prefix=/usr
    make

and either:

    make install

or, when building a package using a staging directory:

    make DESTDIR=$pkgdir install
