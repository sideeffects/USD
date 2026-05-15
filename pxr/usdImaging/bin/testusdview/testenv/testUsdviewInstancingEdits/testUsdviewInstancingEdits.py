#!/pxrpythonsubst
#
# Copyright 2020 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#

from pxr.Usdviewq.qt import QtWidgets
from pxr.Usdviewq.common import SelectionHighlightModes

def _waitForRefresh():
    import time
    time.sleep(0.5)
    QtWidgets.QApplication.processEvents()

# Remove any unwanted visuals from the view.
def _modifySettings(appController):
    appController._dataModel.viewSettings.showBBoxes = False
    appController._dataModel.viewSettings.showHUD = False
    appController._dataModel.viewSettings.selHighlightMode = (
        SelectionHighlightModes.NEVER)


#
# Test a case where we move an instanced root that contains strictly
# non-instancable-by-Hydra prims (i.e., no rprims).
#
def _testInstancingEdits6146(appController):
    from pxr import Sdf, Usd

    testALayer = Sdf.Layer.FindOrOpen("usd-6146/testA.usda")
    appController._dataModel.stage.GetRootLayer().TransferContent(testALayer)
    _waitForRefresh()

    testBLayer = Sdf.Layer.FindOrOpen("usd-6146/testB.usda")
    appController._dataModel.stage.GetRootLayer().TransferContent(testBLayer)
    _waitForRefresh()

    # If we get this far without crashing, we're good for now.

# Tests that when a prim goes from being a "prototype" to no longer, we image
# it properly.
# 
# Since we will sometimes use a type-less "over" to store prototypes, that is
# included here.
def _testMakePrototypeNotAPrototype(appController):
    from pxr import Sdf

    # The scene has 4 cubes:
    # "left", "Over", "PointInstancer", "right"
    stageRootLayer = appController._dataModel.stage.GetRootLayer()
    stageRootLayer.TransferContent(Sdf.Layer.FindOrOpen("usd-12098/test.usda"))

    # This image should contain just the other "left" and "right" cubes.
    # The cubes under the "Over" and "PointInstancer" prims do not draw
    # since they are considered prototypes (without any instances).
    appController._takeShot("unprototyping-before.png")

    # Turn the over to a def.  This should result in the prim underneath
    # getting added.
    stageRootLayer.GetPrimAtPath("/Over").specifier = Sdf.SpecifierDef

    # Turn the prototype into a Scope.  Ensure it's children get imaged.
    stageRootLayer.GetPrimAtPath("/PointInstancer").typeName = "Scope"

    # This image should contain all 4 cubes.
    appController._takeShot("unprototyping-after.png")



def testUsdviewInputFunction(appController):
    _modifySettings(appController)
    _testInstancingEdits6146(appController)
    _testMakePrototypeNotAPrototype(appController)
