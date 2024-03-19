#!/pxrpythonsubst
#
# Copyright 2016 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#

from __future__ import print_function
import sys
import pxr.Usdviewq as Usdviewq

from PySide2 import QtCore
import os

libpaths = list(QtCore.QCoreApplication.libraryPaths())
# Get the dso path on Linux and Mac, or HB on Windows.
if os.name == 'nt':
    libpath = os.environ.get('HB')
else:
    libpath = os.environ.get('HDSO')

# Add the Houdini Qt_plugins directory so we find the plugin for the
# current platform.
if libpath is not None:
    libpaths.append(libpath + '/Qt_plugins')
    QtCore.QCoreApplication.setLibraryPaths(libpaths)

# Register the Houdini fonts directory with Qt.
HFS = os.environ.get("HFS")
if HFS is not None:
    os.environ["QT_QPA_FONTDIR"] = "{}/houdini/fonts".format(HFS)

if __name__ == '__main__':
    # Let Ctrl-C kill the app.
    import signal
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    try:
        Usdviewq.Launcher().Run()
    except Usdviewq.InvalidUsdviewOption as e:
        print("ERROR: " + str(e), file=sys.stderr)
        sys.exit(1)
