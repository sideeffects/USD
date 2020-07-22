#!/pxrpythonsubst
#
# Copyright 2016 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "Apache License")
# with the following modification; you may not use this file except in
# compliance with the Apache License and the following modification to it:
# Section 6. Trademarks. is deleted and replaced with:
#
# 6. Trademarks. This License does not grant permission to use the trade
#    names, trademarks, service marks, or product names of the Licensor
#    and its affiliates, except as required to comply with Section 4(c) of
#    the License and to reproduce the content of the NOTICE file.
#
# You may obtain a copy of the Apache License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the Apache License with the above modification is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the Apache License for the specific
# language governing permissions and limitations under the Apache License.
#
from __future__ import print_function
from pxr import Usdviewq
from PySide2 import QtCore
import sys
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
        print("ERROR: " + e.message, file=sys.stderr)
        sys.exit(1)

