#!/pxrpythonsubst
#
# Copyright 2018 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
from pxr import UsdUtils
from pxr import Sdf

def Test(usdFile, outFile, fn, keepEmptyPathsInArrays = False):
    layer = Sdf.Layer.FindOrOpen(usdFile)
    UsdUtils.ModifyAssetPaths(layer, fn, keepEmptyPathsInArrays)
    layer.Export(outFile)

def TestBasic():
    # Test basic substitution
    def fn(s):
        return 'SUCCESS_' + s

    Test('layer.usda', 'modified.usda', fn)

TestBasic()

def TestDuplicates():
    # Test behavior when modify callback yields duplicate
    # asset paths.
    def fn(s):
        return "baz.usd"

    Test('layer.usda', 'duplicates.usda', fn)

TestDuplicates()

def TestRemoval():
    # Tests behavior when modify callback returns empty asset
    # paths.
    def fn(s):
        return ''
        
    Test('layer.usda', 'removal.usda', fn)

TestRemoval()

def TestRemovalPreserveLength():
    # Tests behavior when modify callback returns empty asset
    # paths and preserve array length is enabled.
    def fn(s):
        return ''
        
    Test('layer.usda', 'removal_preserve_length.usda', fn, 
         keepEmptyPathsInArrays=True)

TestRemovalPreserveLength()

def TestDoesNotRecurseDeps():
    # Tests reference paths are not traversed during asset path modification
    def fn(s):
        return 'MOD_' + s
    
    refLayer = Sdf.Layer.FindOrOpen('ref.usda')
    Test('ref_layer.usda', 'ref_layer_mod.usda', fn)
    refLayer.Export("ref_unmodified.usda")

TestDoesNotRecurseDeps()

def TestDoesNotStripMetadata():
    # Ensure that metadata is retained.
    def fn(s):
        return s

    Test('metadata.usda', 'preserve_metadata.usda', fn)

TestDoesNotStripMetadata()

def TestExpressionsArePassedUnevaluated():
    # The callback receives the authored asset paths, with variable
    # expressions unevaluated, so returning them unchanged leaves the
    # expressions intact.
    authored = [
        '`"${ROOT}/sublayer.usda"`',
        '`"${ROOT}/payload.usda"`',
        '`"${ROOT}/ref.usda"`',
        '`"${ROOT}/tex.png"`',
        '`"${ROOT}/a.png"`',
        'plain.png',
    ]

    received = []
    def fn(s):
        received.append(s)
        return s

    layer = Sdf.Layer.FindOrOpen('expressions.usda')
    UsdUtils.ModifyAssetPaths(layer, fn)

    assert sorted(received) == sorted(authored), received

    prim = layer.GetPrimAtPath('/Prim')
    assert list(layer.subLayerPaths) == ['`"${ROOT}/sublayer.usda"`']
    assert [r.assetPath for r in prim.referenceList.GetAddedOrExplicitItems()] \
        == ['`"${ROOT}/ref.usda"`']
    assert [p.assetPath for p in prim.payloadList.GetAddedOrExplicitItems()] \
        == ['`"${ROOT}/payload.usda"`']
    assert prim.attributes['tex'].default.path == '`"${ROOT}/tex.png"`'
    assert [a.path for a in prim.attributes['texs'].default] \
        == ['`"${ROOT}/a.png"`', 'plain.png']

TestExpressionsArePassedUnevaluated()
