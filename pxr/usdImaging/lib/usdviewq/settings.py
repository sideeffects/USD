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
"""A module for persisting usdview settings
"""

from __future__ import print_function

import sys

if sys.version_info.major >= 3:
    from pickle import dump, load
else:
    from cPickle import dump, load

def EmitWarning(filePath):
    """Send a warning because the settings file should never fail to load
    """
    import traceback
    import sys
    msg = sys.stderr
    print("------------------------------------------------------------", file=msg)
    print("WARNING: Unknown problem while trying to access settings:", file=msg)
    print("------------------------------------------------------------", file=msg)
    print("This message is being sent because the settings file (%s) " \
                  "could not be read" % filePath, file=msg)
    print("--", file=msg)
    traceback.print_exc(file=msg)
    print("--", file=msg)
    print("Please file a bug if this warning persists", file=msg)
    print("Attempting to continue... ", file=msg)
    print("------------------------------------------------------------", file=msg)

class Settings(dict):
    """A small wrapper around the standard Python dictionary.

    See help(dict) for initialization arguments This class uses python naming
    conventions, because it inherits from dict.
    """

    def __init__(self, filename, seq=None, ephemeral=False, **kwargs):
        self._filename = filename

        # Ephemeral settings objects are created in the presence of
        # file system failures, such as the inability to create a .usdview
        # directory to store our settings. In these cases we won't perform
        # and save or load operations.
        self._ephemeral = ephemeral
        if self._ephemeral:
            return

        if seq:
            dict.__init__(self, seq)
        elif kwargs:
            dict.__init__(self, **kwargs)

    def save(self, ignoreErrors=False):
        """Write the settings out to the file at filename
        """
        if self._ephemeral:
            return
        try:
            f = open(self._filename, "wb")
            dump(self, f)
            f.close()
        except:
            if ignoreErrors:
                return False
            raise
        return True

    def load(self, ignoreErrors=False):
        """Load the settings from the file at filename
        """
        if self._ephemeral:
            return
        try:
            f = open(self._filename, "rb")
            self.update(load(f))
            f.close()

        except:
            if ignoreErrors:
                return False
            raise
        return True

    def setAndSave(self, **kwargs):
        """Sets keyword arguments as settings and quietly saves
        """
        if self._ephemeral:
            return

        self.update(kwargs)
        self.save(ignoreErrors=True)

