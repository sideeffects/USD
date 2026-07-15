#!/pxrpythonsubst
#
# Copyright 2026 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
from pxr import Sdf, UsdGeom
from pxr.UsdAppUtils.complexityArgs import RefinementComplexities

def _modifySettings(appController):
    appController._dataModel.viewSettings.showBBoxes = False
    appController._dataModel.viewSettings.showHUD = False
    appController._dataModel.viewSettings.autoComputeClippingPlanes = True
    appController._dataModel.viewSettings.complexity = \
        RefinementComplexities.VERY_HIGH
    appController._dataModel._viewSettingsDataModel.cameraPath = \
        Sdf.Path('/main_cam')
    appController._stageView.updateView()

# Apply UsdGeomModelAPI to /asset and drive its draw mode, then remove it.
def testUsdviewInputFunction(appController):
    _modifySettings(appController)

    stage = appController._dataModel.stage
    stage.SetEditTarget(stage.GetSessionLayer())

    appController._takeShot("start.png")

    asset = stage.GetPrimAtPath('/asset')
    api = UsdGeom.ModelAPI.Apply(asset)
    api.GetModelDrawModeAttr().Set('bounds')
    appController._takeShot("bounds.png")

    api.GetModelDrawModeAttr().Set('cards')
    appController._takeShot("cards.png")

    stage.GetSessionLayer().Clear()
    appController._takeShot("api-removed.png")
