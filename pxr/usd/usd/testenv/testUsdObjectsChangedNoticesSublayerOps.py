#!/pxrpythonsubst
#
# Copyright 2025 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.


import unittest
from pxr import Kind, Sdf, Tf, Usd

class TestUsdObjectsChangedNoticesSublayerOps(unittest.TestCase):
    def _listenForNotices(self, stage):
        # Each ObjectsChanged notice is converted to a dictionary and appended
        # to self._objectsChangedNotices.
        self._objectsChangedNotices = []
        self._objectsChangedNoticeKey = Tf.Notice.Register(
            Usd.Notice.ObjectsChanged,
            self._onObjectsChanged,
            stage)
        
        self._stageContentsChangedCount = 0
        self._stageContentsChangedKey = Tf.Notice.Register(
            Usd.Notice.StageContentsChanged,
            self._onStageContentsChanged,
            stage)
        
    def _onStageContentsChanged(self, *args):
        self._stageContentsChangedCount += 1

    def _onObjectsChanged(self, notice, sender):
        asDict = {}
        resynced = notice.GetResyncedPaths()
        changedInfoOnly = notice.GetChangedInfoOnlyPaths()
        resolvedAssetPathsResynced = notice.GetResolvedAssetPathsResyncedPaths()
        if resynced:
            asDict['Resynced'] = {
                str(path): notice.GetChangedFields(path)
                for path in resynced
            }
        if changedInfoOnly:
            asDict['ChangedInfoOnly'] = {
                str(path): notice.GetChangedFields(path)
                for path in changedInfoOnly
            }
        if resolvedAssetPathsResynced:
            asDict['ResolvedAssetPathsResynced'] = {
                str(path): notice.GetChangedFields(path)
                for path in resolvedAssetPathsResynced
            }
<<<<<<< HEAD
        self._objectsChangedNotices.append(asDict)
=======
        self._notices.append(asDict)
>>>>>>> 886e410dd (This change fixes a coding error that is triggered when performing a sublayer operation on a layer whose file format is a package.  The current implementation which generates fine grained change lists makes use of an anonymous layer to compute a diff against.  It is an error to create such a layer with a package file format.  To workaround this limitation with the current implementation, we send packages down the "big bang" invalidation path.)

    def test_InsertPackageSublayer(self):
        stage = Usd.Stage.CreateInMemory()
        self._listenForNotices(stage)
        stage.GetRootLayer().subLayerPaths = ["./package.usdz"]
<<<<<<< HEAD
        self.assertEqual(self._stageContentsChangedCount, 1)
        self.assertEqual(len(self._objectsChangedNotices), 1)
        self.assertDictEqual(self._objectsChangedNotices[0], {
=======
        self.assertEqual(len(self._notices), 1)
        self.assertDictEqual(self._notices[0], {
>>>>>>> 886e410dd (This change fixes a coding error that is triggered when performing a sublayer operation on a layer whose file format is a package.  The current implementation which generates fine grained change lists makes use of an anonymous layer to compute a diff against.  It is an error to create such a layer with a package file format.  To workaround this limitation with the current implementation, we send packages down the "big bang" invalidation path.)
            'Resynced': {
                '/': [] 
            }
        })

    def test_RemovePackageSublayer(self):
        root = Sdf.Layer.CreateAnonymous('root.usda')
        root.ImportFromString("""#usda 1.0
                                (
                                    subLayers = [@./package.usdz@]
                                )
                            """)
        stage = Usd.Stage.Open(root)
        self._listenForNotices(stage)
        stage.GetRootLayer().subLayerPaths = []
<<<<<<< HEAD
        self.assertEqual(self._stageContentsChangedCount, 1)
        self.assertEqual(len(self._objectsChangedNotices), 1)
        self.assertDictEqual(self._objectsChangedNotices[0], {
=======
        self.assertEqual(len(self._notices), 1)
        self.assertDictEqual(self._notices[0], {
>>>>>>> 886e410dd (This change fixes a coding error that is triggered when performing a sublayer operation on a layer whose file format is a package.  The current implementation which generates fine grained change lists makes use of an anonymous layer to compute a diff against.  It is an error to create such a layer with a package file format.  To workaround this limitation with the current implementation, we send packages down the "big bang" invalidation path.)
            'Resynced': {
                '/': [] 
            }
        })

    def test_MutePackageSublayer(self):
        root = Sdf.Layer.CreateAnonymous('root.usda')
        root.ImportFromString("""#usda 1.0
                                (
                                    subLayers = [@./package.usdz@]
                                )
                            """)
        stage = Usd.Stage.Open(root)
        self._listenForNotices(stage)
        stage.MuteLayer('./package.usdz')
<<<<<<< HEAD
        self.assertEqual(self._stageContentsChangedCount, 1)
        self.assertEqual(len(self._objectsChangedNotices), 1)
        self.assertDictEqual(self._objectsChangedNotices[0], {
=======
        self.assertEqual(len(self._notices), 1)
        self.assertDictEqual(self._notices[0], {
>>>>>>> 886e410dd (This change fixes a coding error that is triggered when performing a sublayer operation on a layer whose file format is a package.  The current implementation which generates fine grained change lists makes use of an anonymous layer to compute a diff against.  It is an error to create such a layer with a package file format.  To workaround this limitation with the current implementation, we send packages down the "big bang" invalidation path.)
            'Resynced': {
                '/': [] 
            }
        })

    def test_UnmutePackageSublayer(self):
        stage = Usd.Stage.CreateInMemory()
        stage.MuteLayer('./package.usdz')
        stage.GetRootLayer().subLayerPaths = ["./package.usdz"]
        self._listenForNotices(stage)
        stage.UnmuteLayer('./package.usdz')
<<<<<<< HEAD
        self.assertEqual(self._stageContentsChangedCount, 1)
        self.assertEqual(len(self._objectsChangedNotices), 1)
        self.assertDictEqual(self._objectsChangedNotices[0], {
=======
        self.assertEqual(len(self._notices), 1)
        self.assertDictEqual(self._notices[0], {
>>>>>>> 886e410dd (This change fixes a coding error that is triggered when performing a sublayer operation on a layer whose file format is a package.  The current implementation which generates fine grained change lists makes use of an anonymous layer to compute a diff against.  It is an error to create such a layer with a package file format.  To workaround this limitation with the current implementation, we send packages down the "big bang" invalidation path.)
            'Resynced': {
                '/': [] 
            }
        })

<<<<<<< HEAD
    def test_muteEmptyLayer(self):
        """Tests that a StageContentsChanged and an empty ObjectsChanged notice
        are triggered when an empty layer is muted"""

        stage = Usd.Stage.CreateInMemory()
        l1 = Sdf.Layer.CreateAnonymous()

        stage.GetRootLayer().subLayerPaths.append(l1.identifier)
        self._listenForNotices(stage)
        stage.MuteLayer(l1.identifier)
        self.assertEqual(self._stageContentsChangedCount, 1)
        self.assertEqual(len(self._objectsChangedNotices), 1)
        self.assertDictEqual(self._objectsChangedNotices[0], {})

    def test_UnmuteEmptyLayer(self):
        """Tests that a StageContentsChanged and an empty ObjectsChanged notice
        are triggered when an empty layer is unmuted"""

        stage = Usd.Stage.CreateInMemory()
        l1 = Sdf.Layer.CreateAnonymous()

        stage.GetRootLayer().subLayerPaths.append(l1.identifier)
        stage.MuteLayer(l1.identifier)
        self._listenForNotices(stage)
        stage.UnmuteLayer(l1.identifier)
        self.assertEqual(self._stageContentsChangedCount, 1)
        self.assertEqual(len(self._objectsChangedNotices), 1)
        self.assertDictEqual(self._objectsChangedNotices[0], {})

=======
>>>>>>> 886e410dd (This change fixes a coding error that is triggered when performing a sublayer operation on a layer whose file format is a package.  The current implementation which generates fine grained change lists makes use of an anonymous layer to compute a diff against.  It is an error to create such a layer with a package file format.  To workaround this limitation with the current implementation, we send packages down the "big bang" invalidation path.)
if __name__ == "__main__":
    unittest.main()
