//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/pxr.h"

#include "pxr/imaging/hdSt/bufferArrayRangeGL.h"
#include "pxr/imaging/hdSt/bufferResourceGL.h"
#include "pxr/imaging/hdSt/drawItem.h"
#include "pxr/imaging/hdSt/extCompGpuComputation.h"
#include "pxr/imaging/hdSt/flatNormals.h"
#include "pxr/imaging/hdSt/geometricShader.h"
#include "pxr/imaging/hdSt/glUtils.h"
#include "pxr/imaging/hdSt/instancer.h"
#include "pxr/imaging/hdSt/material.h"
#include "pxr/imaging/hdSt/mesh.h"
#include "pxr/imaging/hdSt/meshShaderKey.h"
#include "pxr/imaging/hdSt/meshTopology.h"
#include "pxr/imaging/hdSt/package.h"
#include "pxr/imaging/hdSt/quadrangulate.h"
#include "pxr/imaging/hdSt/resourceRegistry.h"
#include "pxr/imaging/hdSt/rprimUtils.h"
#include "pxr/imaging/hdSt/smoothNormals.h"
#include "pxr/imaging/hdSt/surfaceShader.h"
#include "pxr/imaging/hdSt/tokens.h"

#include "pxr/base/arch/hash.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/tf/envSetting.h"

#include "pxr/imaging/hd/bufferSource.h"
#include "pxr/imaging/hd/computation.h"
#include "pxr/imaging/hd/flatNormals.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/repr.h"
#include "pxr/imaging/hd/selection.h"
#include "pxr/imaging/hd/smoothNormals.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/vertexAdjacency.h"
#include "pxr/imaging/hd/vtBufferSource.h"

#include "pxr/imaging/hf/diagnostic.h"

#include "pxr/imaging/pxOsd/tokens.h"

#include "pxr/base/vt/value.h"

#include <iostream>
#include <limits>

PXR_NAMESPACE_OPEN_SCOPE


// for debugging
TF_DEFINE_ENV_SETTING(HD_ENABLE_FORCE_QUADRANGULATE, 0,
                      "Apply quadrangulation for all meshes for debug");

// default to use packed normals
TF_DEFINE_ENV_SETTING(HD_ENABLE_PACKED_NORMALS, 1,
                      "Use packed normals");

HdStMesh::HdStMesh(SdfPath const& id,
                   SdfPath const& instancerId)
    : HdMesh(id, instancerId)
    , _topology()
    , _vertexAdjacency()
    , _topologyId(0)
    , _vertexPrimvarId(0)
    , _customDirtyBitsInUse(0)
    , _sceneNormalsInterpolation()
    , _cullStyle(HdCullStyleDontCare)
    , _doubleSided(false)
    , _flatShadingEnabled(false)
    , _displacementEnabled(true)
    , _limitNormals(false)
    , _sceneNormals(false)
    , _hasVaryingTopology(false)
{
    /*NOTHING*/
}

HdStMesh::~HdStMesh()
{
    /*NOTHING*/
}

void
HdStMesh::Sync(HdSceneDelegate *delegate,
               HdRenderParam   *renderParam,
               HdDirtyBits     *dirtyBits,
               TfToken const   &reprToken)
{
    TF_UNUSED(renderParam);

    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        _SetMaterialId(delegate->GetRenderIndex().GetChangeTracker(),
                       delegate->GetMaterialId(GetId()));

        _sharedData.materialTag = _GetMaterialTag(delegate->GetRenderIndex());
    }

    // Check if either the material or geometric shaders need updating for
    // draw items of all the reprs.
    bool updateMaterialShader = false;
    if (*dirtyBits & (HdChangeTracker::DirtyMaterialId|
                      HdChangeTracker::NewRepr)) {
        updateMaterialShader = true;
    }

    bool updateGeometricShader = false;
    if (*dirtyBits & (HdChangeTracker::DirtyDisplayStyle|
                      HdChangeTracker::DirtyCullStyle|
                      HdChangeTracker::DirtyDoubleSided|
                      HdChangeTracker::DirtyMaterialId|
                      HdChangeTracker::DirtyTopology| // topological visibility
                      HdChangeTracker::NewRepr)) {
        updateGeometricShader = true;
    }

    _UpdateRepr(delegate, reprToken, dirtyBits);

    if (updateMaterialShader || updateGeometricShader) {
        _UpdateShadersForAllReprs(delegate,
                                  updateMaterialShader, updateGeometricShader);
    }

    // This clears all the non-custom dirty bits. This ensures that the rprim
    // doesn't have pending dirty bits that add it to the dirty list every
    // frame.
    // XXX: GetInitialDirtyBitsMask sets certain dirty bits that aren't
    // reset (e.g. DirtyExtent, DirtyPrimID) that make this necessary.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

HdMeshTopologySharedPtr
HdStMesh::GetTopology() const
{
    return _topology;
}

static bool
_IsEnabledForceQuadrangulate()
{
    static bool enabled = (TfGetEnvSetting(HD_ENABLE_FORCE_QUADRANGULATE) == 1);
    return enabled;
}

/* static */
bool
HdStMesh::IsEnabledPackedNormals()
{
    static bool enabled = (TfGetEnvSetting(HD_ENABLE_PACKED_NORMALS) == 1);
    return enabled;
}

int
HdStMesh::_GetRefineLevelForDesc(const HdMeshReprDesc &desc) const
{
    if (desc.geomStyle == HdMeshGeomStyleHull         ||
        desc.geomStyle == HdMeshGeomStyleHullEdgeOnly ||
        desc.geomStyle == HdMeshGeomStyleHullEdgeOnSurf) {
        return 0;
    }
    if (!TF_VERIFY(_topology)) return 0;
    return _topology->GetRefineLevel();
}

void
HdStMesh::_PopulateTopology(HdSceneDelegate *sceneDelegate,
                            HdStDrawItem *drawItem,
                            HdDirtyBits *dirtyBits,
                            const HdMeshReprDesc &desc)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();
    HdStResourceRegistrySharedPtr const& resourceRegistry = 
        std::static_pointer_cast<HdStResourceRegistry>(
        sceneDelegate->GetRenderIndex().GetResourceRegistry());

    // note: there's a potential optimization if _topology is already registered
    // and it's not shared across prims, it can be updated without inserting new
    // entry into the topology registry. But in mose case topology varying prim
    // requires range resizing (reallocation), so for code simplicity we always
    // register as a new topology (it still can be shared if possible) and
    // allocate a new range for varying topology (= dirty topology)
    // for the time being. In other words, each range of index buffer is
    // immutable.
    // 
    bool dirtyTopology = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);

    if (dirtyTopology ||
        HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id) ||
        HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id)) {
        // make a shallow copy and the same time expand the topology to a
        // stream extended representation
        // note: if we add topologyId computation in delegate,
        // we can move this copy into topologyInstance.IsFirstInstance() block
        HdDisplayStyle const displayStyle = GetDisplayStyle(sceneDelegate);

        int refineLevel = displayStyle.refineLevel;
        HdSt_MeshTopology::RefineMode refineMode =
                HdSt_MeshTopology::RefineModeUniform;
        _limitNormals = false;

        _flatShadingEnabled = displayStyle.flatShadingEnabled;
        _displacementEnabled = displayStyle.displacementEnabled;

        HdMeshTopology meshTopology = HdMesh::GetMeshTopology(sceneDelegate);

        // Topological visibility (of points, faces) comes in as DirtyTopology.
        // We encode this information in a separate BAR.
        if (dirtyTopology) {
            HdStProcessTopologyVisibility(
                meshTopology.GetInvisibleFaces(),
                meshTopology.GetNumFaces(),
                meshTopology.GetInvisiblePoints(),
                meshTopology.GetNumPoints(),
                &_sharedData,
                drawItem,
                &(sceneDelegate->GetRenderIndex().GetChangeTracker()),
                resourceRegistry,
                id);    
        }

        // If flat shading is enabled for this prim, make sure we're computing
        // flat normals. It's ok to set the dirty bit here because it's a
        // custom (non-scene) dirty bit, and DirtyTopology will propagate to
        // DirtyPoints if we're computing CPU normals (since flat normals
        // computation requires points data).
        if (_flatShadingEnabled) {
            if (!(_customDirtyBitsInUse & DirtyFlatNormals)) {
                _customDirtyBitsInUse |= DirtyFlatNormals;
                *dirtyBits |= DirtyFlatNormals;
            }
        }
              
        // If the topology requires none subdivision scheme then force
        // refinement level to be 0 since we do not want subdivision.
        if (meshTopology.GetScheme() == PxOsdOpenSubdivTokens->none) {
            refineLevel = 0;
        }

        // If the topology supports adaptive refinement and that's what this
        // prim wants, note that and also that our normals will be generated
        // in the shader.
        if (meshTopology.GetScheme() != PxOsdOpenSubdivTokens->bilinear &&
            meshTopology.GetScheme() != PxOsdOpenSubdivTokens->none &&
            refineLevel > 0 &&
            _UseLimitRefinement(sceneDelegate->GetRenderIndex())) {
            refineMode = HdSt_MeshTopology::RefineModePatches;
            _limitNormals = true;
        }

        HdSt_MeshTopologySharedPtr topology =
                HdSt_MeshTopology::New(meshTopology, refineLevel, refineMode);
        if (refineLevel > 0) {
            // add subdiv tags before compute hash
            topology->SetSubdivTags(GetSubdivTags(sceneDelegate));
        }

        // Compute id here. In the future delegate can provide id directly without
        // hashing.
        _topologyId = topology->ComputeHash();

        // Salt the hash with refinement level and useQuadIndices.
        // (refinement level is moved into HdMeshTopology)
        //
        // Specifically for quad indices, we could do better here because all we
        // really need is the ability to compute quad indices late, however
        // splitting the topology shouldn't be a huge cost either.
        bool useQuadIndices = _UseQuadIndices(sceneDelegate->GetRenderIndex(), topology);
        _topologyId = ArchHash64((const char*)&useQuadIndices,
            sizeof(useQuadIndices), _topologyId);

        {
            // ask registry if there's a sharable mesh topology
            HdInstance<HdSt_MeshTopologySharedPtr> topologyInstance =
                resourceRegistry->RegisterMeshTopology(_topologyId);

            if (topologyInstance.IsFirstInstance()) {
                // if this is the first instance, set this topology to registry.
                topologyInstance.SetValue(topology);

                // if refined, we submit a subdivision preprocessing
                // no matter what desc says
                // (see the lengthy comment in PopulateVertexPrimvar)
                if (refineLevel > 0) {
                    // OpenSubdiv preprocessing
                    HdBufferSourceSharedPtr
                        topologySource = topology->GetOsdTopologyComputation(id);
                    resourceRegistry->AddSource(topologySource);
                }

                // we also need quadinfo if requested.
                // Note that this is needed even if refineLevel > 0, in case
                // HdMeshGeomStyleHull is going to be used.
                if (useQuadIndices) {
                    // Quadrangulate preprocessing
                    HdSt_QuadInfoBuilderComputationSharedPtr quadInfoBuilder =
                        topology->GetQuadInfoBuilderComputation(
                            HdStGLUtils::IsGpuComputeEnabled(),
                            id, resourceRegistry.get());
                    resourceRegistry->AddSource(quadInfoBuilder);
                }
            }
            _topology = topologyInstance.GetValue();
        }
        TF_VERIFY(_topology);

        // hash collision check
        if (TfDebug::IsEnabled(HD_SAFE_MODE)) {
            TF_VERIFY(*topology == *_topology);
        }

        _vertexAdjacency.reset();
    }

    // here, we have _topology up-to-date.

    int refineLevelForDesc = _GetRefineLevelForDesc(desc);
    TfToken indexToken;  // bar-instance identifier

    // bail out if the index bar is already synced
    if (drawItem->GetDrawingCoord()->GetTopologyIndex() == HdStMesh::HullTopology) {
        if ((*dirtyBits & DirtyHullIndices) == 0) return;
        *dirtyBits &= ~DirtyHullIndices;
        indexToken = HdTokens->hullIndices;
    } else if (drawItem->GetDrawingCoord()->GetTopologyIndex() == HdStMesh::PointsTopology) {
        if ((*dirtyBits & DirtyPointsIndices) == 0) return;
        *dirtyBits &= ~DirtyPointsIndices;
        indexToken = HdTokens->pointsIndices;
    } else {
        if ((*dirtyBits & DirtyIndices) == 0) return;
        *dirtyBits &= ~DirtyIndices;
        indexToken = HdTokens->indices;
    }

    // note: don't early out even if the topology has no faces,
    // otherwise codegen takes inconsistent configuration and
    // fails to compile ( or even segfaults: filed as nvidia-bug 1719609 )

    {
        // ask again registry if there's a shareable buffer range for the topology
        HdInstance<HdBufferArrayRangeSharedPtr> rangeInstance =
            resourceRegistry->RegisterMeshIndexRange(_topologyId, indexToken);

        if (rangeInstance.IsFirstInstance()) {
            // if not exists, update actual topology buffer to range.
            // Allocate new one if necessary.
            HdBufferSourceSharedPtr source;

            if (desc.geomStyle == HdMeshGeomStylePoints) {
                // create coarse points indices
                source = _topology->GetPointsIndexBuilderComputation();
            } else if (refineLevelForDesc > 0) {
                // create refined indices, primitiveParam and edgeIndices
                source = _topology->GetOsdIndexBuilderComputation();
            } else if (_UseQuadIndices(sceneDelegate->GetRenderIndex(), _topology)) {
                // not refined = quadrangulate
                // create quad indices, primitiveParam and edgeIndices
                source = _topology->GetQuadIndexBuilderComputation(GetId());
            } else {
                // create triangle indices, primitiveParam and edgeIndices
                source = _topology->GetTriangleIndexBuilderComputation(GetId());
            }
            HdBufferSourceSharedPtrVector sources = { source };

            // initialize buffer array
            //   * indices
            //   * primitiveParam
            HdBufferSpecVector bufferSpecs;
            HdBufferSpec::GetBufferSpecs(sources, &bufferSpecs);

            // Set up the usage hints to mark topology as varying if
            // there is a previously set range
            HdBufferArrayUsageHint usageHint;
            usageHint.value = 0;
            usageHint.bits.sizeVarying =
                                 ((bool)(drawItem->GetTopologyRange())) ? 1 : 0;

            // allocate new range
            HdBufferArrayRangeSharedPtr range =
                resourceRegistry->AllocateNonUniformBufferArrayRange(
                    HdTokens->topology, bufferSpecs, usageHint);

            // add sources to update queue
            resourceRegistry->AddSources(range, std::move(sources));

            // save new range to registry
            rangeInstance.SetValue(range);
        }

        // If we are updating an existing topology, notify downstream
        // systems of the change
        HdBufferArrayRangeSharedPtr const& orgRange =
            drawItem->GetTopologyRange();
        HdBufferArrayRangeSharedPtr newRange = rangeInstance.GetValue();

        if (HdStIsValidBAR(orgRange) && (newRange != orgRange)) {
            TF_DEBUG(HD_RPRIM_UPDATED).Msg("%s has varying topology"
                " (topology index = %d).\n", id.GetText(),
                drawItem->GetDrawingCoord()->GetTopologyIndex());
            
            // Setup a flag to say this mesh's topology is varying
            _hasVaryingTopology = true;
        }

        HdStUpdateDrawItemBAR(
            newRange,
            drawItem->GetDrawingCoord()->GetTopologyIndex(),
            &_sharedData,
            sceneDelegate->GetRenderIndex());

    }  // Release regLock
}

void
HdStMesh::_PopulateAdjacency(HdStResourceRegistrySharedPtr const &resourceRegistry)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // The topology may be null in the event that it has zero faces.
    if (!_topology) return;

    // ask registry if there's a sharable vertex adjacency
    HdInstance<Hd_VertexAdjacencySharedPtr> adjacencyInstance =
        resourceRegistry->RegisterVertexAdjacency(_topologyId);

    if (adjacencyInstance.IsFirstInstance()) {
        Hd_VertexAdjacencySharedPtr adjacency(new Hd_VertexAdjacency());

        // create adjacency table for smooth normals
        HdBufferSourceSharedPtr adjacencyComputation =
            adjacency->GetSharedAdjacencyBuilderComputation(_topology.get());

        resourceRegistry->AddSource(adjacencyComputation);

        if (HdStGLUtils::IsGpuComputeEnabled()) {
            // also send adjacency table to gpu
            HdBufferSourceSharedPtr adjacencyForGpuComputation =
                HdBufferSourceSharedPtr(
                    new Hd_AdjacencyBufferSource(
                        adjacency.get(), adjacencyComputation));

            HdBufferSpecVector bufferSpecs;
            adjacencyForGpuComputation->GetBufferSpecs(&bufferSpecs);

            HdBufferArrayRangeSharedPtr adjRange =
                resourceRegistry->AllocateNonUniformBufferArrayRange(
                    HdTokens->topology, bufferSpecs, HdBufferArrayUsageHint());

            adjacency->SetAdjacencyRange(adjRange);
            resourceRegistry->AddSource(adjRange,
                                        adjacencyForGpuComputation);
        }

        adjacencyInstance.SetValue(adjacency);
    }
    _vertexAdjacency = adjacencyInstance.GetValue();
}

static HdBufferSourceSharedPtr
_QuadrangulatePrimvar(HdBufferSourceSharedPtr const &source,
                      HdComputationSharedPtrVector *computations,
                      HdSt_MeshTopologySharedPtr const &topology,
                      SdfPath const &id,
                      HdStResourceRegistrySharedPtr const &resourceRegistry)
{
    if (!TF_VERIFY(computations)) return source;

    if (!HdStGLUtils::IsGpuComputeEnabled()) {
        // CPU quadrangulation
        // set quadrangulation as source instead of original source.
        HdBufferSourceSharedPtr quadsource =
            topology->GetQuadrangulateComputation(source, id);

        if (quadsource) {
            // don't transfer source to gpu, it needs to be quadrangulated.
            // It will be resolved as a pre-chained source.
            return quadsource;
        } else {
            return source;
        }
    } else {
        // GPU quadrangulation computation needs original vertices to be
        // transfered
        HdComputationSharedPtr computation =
            topology->GetQuadrangulateComputationGPU(
                source->GetName(), source->GetTupleType().type, id);
        // computation can be null for all quad mesh.
        if (computation) {
            computations->push_back(computation);
        }
        return source;
    }
}

static HdBufferSourceSharedPtr
_QuadrangulateFaceVaryingPrimvar(HdBufferSourceSharedPtr const &source,
                                 HdSt_MeshTopologySharedPtr const &topology,
                                 SdfPath const &id,
                                 HdStResourceRegistrySharedPtr const &resourceRegistry)
{
    // note: currently we don't support GPU facevarying quadrangulation.

    // set quadrangulation as source instead of original source.
    HdBufferSourceSharedPtr quadSource =
        topology->GetQuadrangulateFaceVaryingComputation(source, id);

    // don't transfer source to gpu, it needs to be quadrangulated.
    // but it still has to be resolved, so add it to registry.
    resourceRegistry->AddSource(source);

    return quadSource;
}

static HdBufferSourceSharedPtr
_TriangulateFaceVaryingPrimvar(HdBufferSourceSharedPtr const &source,
                               HdSt_MeshTopologySharedPtr const &topology,
                               SdfPath const &id,
                               HdStResourceRegistrySharedPtr const &resourceRegistry)
{
    HdBufferSourceSharedPtr triSource =
        topology->GetTriangulateFaceVaryingComputation(source, id);

    // don't transfer source to gpu, it needs to be triangulated.
    // but it still has to be resolved, so add it to registry.
    resourceRegistry->AddSource(source);

    return triSource;
}

static HdBufferSourceSharedPtr
_RefinePrimvar(HdBufferSourceSharedPtr const &source,
               bool varying,
               HdComputationSharedPtrVector *computations,
               HdSt_MeshTopologySharedPtr const &topology)
{
    if (!TF_VERIFY(computations)) return source;

    if (!HdStGLUtils::IsGpuComputeEnabled()) {
        // CPU subdivision
        // note: if the topology is empty, the source will be returned
        //       without change. We still need the type of buffer
        //       to get the codegen work even for empty meshes
        return topology->GetOsdRefineComputation(source, varying);
    } else {
        // GPU subdivision
        HdComputationSharedPtr computation =
            topology->GetOsdRefineComputationGPU(
                source->GetName(),
                source->GetTupleType().type);
        // computation can be null for empty mesh
        if (computation)
            computations->push_back(computation);
    }

    return source;
}

void
HdStMesh::_PopulateVertexPrimvars(HdSceneDelegate *sceneDelegate,
                                  HdStDrawItem *drawItem,
                                  HdDirtyBits *dirtyBits,
                                  bool requireSmoothNormals,
                                  HdBufferSourceSharedPtr *outPoints)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();
    HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();

    HdStResourceRegistrySharedPtr const &resourceRegistry =
        std::static_pointer_cast<HdStResourceRegistry>(
        renderIndex.GetResourceRegistry());

    // The "points" attribute is expected to be in this list.
    HdPrimvarDescriptorVector primvars =
        HdStGetPrimvarDescriptors(this, drawItem, sceneDelegate,
                                  HdInterpolationVertex);

    // Track the last vertex index to distinguish between vertex and varying
    // while processing.
    const int vertexPartitionIndex = int(primvars.size()-1);

    // Add varying primvars so we can process them all together, below.
    HdPrimvarDescriptorVector varyingPvs =
        HdStGetPrimvarDescriptors(this, drawItem, sceneDelegate,
                                  HdInterpolationVarying);
    primvars.insert(primvars.end(), varyingPvs.begin(), varyingPvs.end());

    HdExtComputationPrimvarDescriptorVector compPrimvars =
        sceneDelegate->GetExtComputationPrimvarDescriptors(id,
            HdInterpolationVertex);

    HdBufferSourceSharedPtrVector sources;
    HdBufferSourceSharedPtrVector reserveOnlySources;
    HdBufferSourceSharedPtrVector separateComputationSources;
    HdComputationSharedPtrVector computations;
    sources.reserve(primvars.size());

    int numPoints = _topology ? _topology->GetNumPoints() : 0;
    int refineLevel = _topology ? _topology->GetRefineLevel() : 0;

    bool cpuNormals = (!HdStGLUtils::IsGpuComputeEnabled());

    // Don't call _GetRefineLevelForDesc(desc) instead of GetRefineLevel(). Why?
    //
    // We share the vertex BAR from both refined and hull topologies so that
    // the change tracker doesn't have to keep track the refined primvars.
    //
    // The hull topology refers coarse vertices that are placed on the beginning
    // of the vertex bar (this is a nature of OpenSubdiv adaptive/uniform
    // refinement). The refined topology refers entire vertex bar.
    //
    // If we only update the coarse vertices for the hull repr, and if we also
    // have a refined repr which stucks in an old state, DirtyPoints gets
    // cleared/ just updating coarse vertices and we lost a chance of updating
    // refined primvars. This state discrepancy could happen over frame, so
    // somebody has to maintain the versioning of each buffers.
    //
    // For topology, _indicesValid and _hullIndicesValid are used for that
    // purpose and it's possible because mesh topology is cached and shared in
    // the instance registry. We don't need to ask sceneDelegate, thus
    // individual (hull and refined) change trackings aren't needed.
    //
    // For vertex primvars, here we simply force to update all vertices at the
    // prim's authored refine level. Then both hull and refined topology can
    // safely access all valid data without having separate change tracking.
    //
    // This could be a performance concern, where a prim has higher refine level
    // and a Storm client keeps drawing only hull repr for some reason.
    // Currently we assume it's not likely a use-case, but we may revisit later
    // and optimize if necessary.
    //

    HdSt_GetExtComputationPrimvarsComputations(
        id,
        sceneDelegate,
        compPrimvars,
        *dirtyBits,
        &sources,
        &reserveOnlySources,
        &separateComputationSources,
        &computations);
    
    HdBufferSourceSharedPtr points;

    // Schedule refinement/quadrangulation of computed primvars.
    for (HdBufferSourceSharedPtr const & source: reserveOnlySources) {
        HdBufferSourceSharedPtr compSource; 
        if (refineLevel > 0) {
            compSource = _RefinePrimvar(source, false, // Should support varying
                                    &computations, _topology);
        } else if (_UseQuadIndices(renderIndex, _topology)) {
            compSource = _QuadrangulatePrimvar(source, &computations, _topology,
                                           GetId(), resourceRegistry);
        }
        // Don't schedule compSource for commit

        // See if points are being produced by gpu computations
        if (source->GetName() == HdTokens->points) {
            points = source;
        }
        // See if normals are being produced by gpu computations
        if (source->GetName() == HdTokens->normals) {
            _sceneNormalsInterpolation = HdInterpolationVertex;
            _sceneNormals = true;
        }
    }

    // Track index to identify varying primvars.
    int i = 0;
    for (HdPrimvarDescriptor const& primvar: primvars) {
        // If the index is greater than the last vertex index, isVarying=true.
        bool isVarying = i++ > vertexPartitionIndex;

        if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, primvar.name)) {
            continue;
        }

        // TODO: We don't need to pull primvar metadata every time a
        // value changes, but we need support from the delegate.

        VtValue value =  GetPrimvar(sceneDelegate, primvar.name);

        if (!value.IsEmpty()) {
            HdBufferSourceSharedPtr source(
                new HdVtBufferSource(primvar.name, value));

            // verify primvar length -- it is alright to have more data than we
            // index into; the inverse is when we issue a warning and skip
            // update.
            if ((int)source->GetNumElements() < numPoints) {
                HF_VALIDATION_WARN(id, 
                    "Vertex primvar %s has only %d elements, while"
                    " its topology expects at least %d elements. Skipping "
                    " primvar update.",
                    primvar.name.GetText(),
                    (int)source->GetNumElements(), numPoints);

                if (primvar.name == HdTokens->points) {
                    // If points data is invalid, it pretty much invalidates
                    // the whole prim.  Drop the Bar, to invalidate the prim and
                    // stop further processing.
                    _sharedData.barContainer.Set(
                           drawItem->GetDrawingCoord()->GetVertexPrimvarIndex(),
                           HdBufferArrayRangeSharedPtr());

                    HF_VALIDATION_WARN(id, 
                      "Skipping prim because its points data is insufficient.");

                    return;
                }

                continue;

            } else if ((int)source->GetNumElements() > numPoints) {
                HF_VALIDATION_WARN(id,
                    "Vertex primvar %s has %d elements, while"
                    " its topology references only upto element index %d.",
                    primvar.name.GetText(),
                    (int)source->GetNumElements(), numPoints);

                // If the primvar has more data than needed, we issue a warning,
                // but don't skip the primvar update. Truncate the buffer to
                // the expected length.
                std::static_pointer_cast<HdVtBufferSource>(source)
                    ->Truncate(numPoints);
            }

            if (source->GetName() == HdTokens->normals) {
                _sceneNormalsInterpolation =
                    isVarying ? HdInterpolationVarying : HdInterpolationVertex;
                _sceneNormals = true;
            }

            if (refineLevel > 0) {
                source = _RefinePrimvar(source, isVarying,
                                        &computations, _topology);
            } else if (_UseQuadIndices(renderIndex, _topology)) {
                source = _QuadrangulatePrimvar(source, &computations, _topology,
                                               GetId(), resourceRegistry);
            }

            // Special handling of points primvar.
            // We need to capture state about the points primvar
            // for use with smooth normal computation.
            if (primvar.name == HdTokens->points) {
                if (!TF_VERIFY(points == nullptr)) {
                    HF_VALIDATION_WARN(id, 
                        "'points' specified as both computed and authored primvar."
                        " Skipping authored value.");
                    continue;
                }
                points = source; // For CPU Smooth Normals
            }

            sources.push_back(source);
        }
    }

    if (requireSmoothNormals && (*dirtyBits & DirtySmoothNormals)) {
        // note: normals gets dirty when points are marked as dirty,
        // at changetracker.

        // clear DirtySmoothNormals (this is not a scene dirtybit)
        *dirtyBits &= ~DirtySmoothNormals;

        TF_VERIFY(_vertexAdjacency);
        bool doRefine = (refineLevel > 0);
        bool doQuadrangulate = _UseQuadIndices(renderIndex, _topology);

        // we can't use packed normals for refined/quad,
        // let's migrate the buffer to full precision
        bool usePackedSmoothNormals =
            IsEnabledPackedNormals() && !(doRefine || doQuadrangulate);

        TfToken normalsName = usePackedSmoothNormals ? 
            HdStTokens->packedSmoothNormals : HdStTokens->smoothNormals;
        
        // The smooth normals computation uses the points primvar as a source.
        //
        if (cpuNormals) {
            // CPU smooth normals require the points source data
            // So it is expected to be dirty.  So if the
            // points variable is not set it means the points primvar is
            // missing or invalid, so we skip smooth normals.
            if (points) {
                // CPU smooth normals depends on CPU adjacency.
                //
                HdBufferSourceSharedPtr normal =
                    HdBufferSourceSharedPtr(
                        new Hd_SmoothNormalsComputation(
                            _vertexAdjacency.get(), points, normalsName,
                            _vertexAdjacency->GetSharedAdjacencyBuilderComputation(
                                _topology.get()),
                            usePackedSmoothNormals));

                if (doRefine) {
                    normal = _RefinePrimvar(normal, /*varying=*/false,
                                                      &computations, _topology);
                } else if (doQuadrangulate) {
                    normal = _QuadrangulatePrimvar(normal,
                                                   &computations,
                                                   _topology,
                                                   id,
                                                   resourceRegistry);
                }

                sources.push_back(normal);
            }
        } else {
            // If we don't have the buffer source, we can get the points
            // data type from the bufferspec in the vertex bar. We need it
            // so we know what type normals should be.
            HdType pointsDataType = HdTypeInvalid;
            if (points) {
                pointsDataType = points->GetTupleType().type;
            } else {
                pointsDataType = _GetPointsDataTypeFromBar(drawItem);
            }

            if (pointsDataType != HdTypeInvalid) {
                // Smooth normals will compute normals as the same datatype
                // as points, unless we ask for packed normals.
                // This is unfortunate; can we force them to be float?
                HdComputationSharedPtr smoothNormalsComputation(
                    new HdSt_SmoothNormalsComputationGPU(
                        _vertexAdjacency.get(),
                        HdTokens->points,
                        normalsName,
                        pointsDataType,
                        usePackedSmoothNormals));
                computations.push_back(smoothNormalsComputation);

                // note: we haven't had explicit dependency for GPU
                // computations just yet. Currently they are executed
                // sequentially, so the dependency is expressed by
                // registering order.
                //
                // note: we can use "pointsDataType" as the normals data type
                // because, if we decided to refine/quadrangulate, we will have
                // forced unpacked normals.
                if (doRefine) {
                    HdComputationSharedPtr computation =
                        _topology->GetOsdRefineComputationGPU(
                            HdStTokens->smoothNormals, pointsDataType);

                    // computation can be null for empty mesh
                    if (computation) {
                        computations.push_back(computation);
                    }
                } else if (doQuadrangulate) {
                    HdComputationSharedPtr computation =
                        _topology->GetQuadrangulateComputationGPU(
                            HdStTokens->smoothNormals,
                            pointsDataType, GetId());

                    // computation can be null for all-quad mesh
                    if (computation) {
                            computations.push_back(computation);
                    }
                }
            }
        }
    }

    *outPoints = points;

    HdBufferArrayRangeSharedPtr const &bar = drawItem->GetVertexPrimvarRange();
    
    if (HdStCanSkipBARAllocationOrUpdate(
            sources, computations, bar,*dirtyBits)) {
        return;
    }

    // XXX: This should be based off the DirtyPrimvarDesc bit.
    bool hasDirtyPrimvarDesc = (*dirtyBits & HdChangeTracker::DirtyPrimvar);
    HdBufferSpecVector removedSpecs;
    if (hasDirtyPrimvarDesc) {
        static TfTokenVector internallyGeneratedPrimvars =
            { HdStTokens->packedSmoothNormals, HdStTokens->smoothNormals };

        removedSpecs = HdStGetRemovedPrimvarBufferSpecs(bar, primvars,
            compPrimvars, internallyGeneratedPrimvars, id);
    }

    HdBufferSpecVector bufferSpecs;
    HdBufferSpec::GetBufferSpecs(sources, &bufferSpecs);
    HdBufferSpec::GetBufferSpecs(reserveOnlySources, &bufferSpecs);
    HdBufferSpec::GetBufferSpecs(computations, &bufferSpecs);

    HdBufferSourceSharedPtrVector allSources(sources);
    for (HdBufferSourceSharedPtr& src : reserveOnlySources) {
        allSources.emplace_back(src);
    }

    HdBufferArrayRangeSharedPtr range;

    if (_IsEnabledSharedVertexPrimvar()) {
        // When primvar sharing is enabled, we have the following scenarios:
        // (a) BAR hasn't been allocated,
        //    - See if an existing immutable BAR may be shared.
        //    - If one cannot be found, allocate an immutable BAR and register
        //      it, so other prims may benefit from sharing it.
        //
        // (b) BAR has been allocated and is immutable
        //    (b1) If the topology is varying OR data in the existing buffers is
        //      changing (e.g. points are being updated) OR if primvar(s) were
        //      removed, it is expensive to recompute a hash over the contents
        //      to check if an existing immutable BAR may be shared.
        //          - Transition to a mutable BAR by migrating existing contents
        //      
        //    (b2) Else if we have new sources (e.g smoothNormals), follow 
        //      the same steps as in (a) to use/allocate an immutable BAR.
        //          - This is done to avoid transitioning to a mutable BAR (and 
        //            thus prevent sharing) when changing reprs. This also
        //            handles authored primvars that were added, which may not
        //            be something we want.
        //    
        //    (b3) No new sources: Use the existing BAR.
        //          
        // (c) BAR has been allocated and is mutable
        //    - This means we transitioned to a mutable BAR (b1) earlier, and
        //      can handle it as though primvar sharing wasn't enabled.

        // (a)
        if (!HdStIsValidBAR(bar)) {
            // see if we can share an immutable primvar range
            // include topology and other topological computations
            // in the sharing id so that we can take into account
            // sharing of computed primvar data.
            _vertexPrimvarId = _ComputeSharedPrimvarId(_topologyId,
                                                       allSources,
                                                       computations);
            
            bool isFirstInstance = true;
            range = _GetSharedPrimvarRange(_vertexPrimvarId,
                                           /*updatedOrAddedSpecs*/bufferSpecs,
                                           /*removedSpecs*/removedSpecs,
                                           /*curRange*/bar,
                                           &isFirstInstance,
                                           resourceRegistry);
            if (!isFirstInstance) {
                TF_DEBUG(HD_RPRIM_UPDATED).Msg(
                    "%s: Found an immutable BAR (%p) for sharing.\n",
                    id.GetText(), (void*)range.get());

                // this is not the first instance, skip redundant
                // sources and computations.
                sources.clear();
                computations.clear();
            } else {
                TF_DEBUG(HD_RPRIM_UPDATED).Msg(
                    "%s: Allocated an immutable BAR (%p).\n",
                    id.GetText(), (void*)range.get());
            }
        } else {
            if (bar->IsImmutable()) {
                HdBufferSpecVector barSpecs;
                bar->GetBufferSpecs(&barSpecs);

                bool updatingExistingBuffers =
                    !bufferSpecs.empty() &&
                    HdBufferSpec::IsSubset(bufferSpecs, /*superSet*/barSpecs);
                bool notNewRepr = !(*dirtyBits & HdChangeTracker::NewRepr);

                bool transitionToMutableBAR =
                    _hasVaryingTopology ||
                    (updatingExistingBuffers && notNewRepr) ||
                    !removedSpecs.empty();
                
                if (transitionToMutableBAR) {
                    // (b1)
                    HdBufferArrayUsageHint newUsageHint = bar->GetUsageHint();
                    newUsageHint.bits.immutable = 0;
                    _vertexPrimvarId = 0;

                    range = resourceRegistry->UpdateNonUniformBufferArrayRange(
                        HdTokens->primvar, bar, bufferSpecs, removedSpecs,
                        newUsageHint);
                    
                    TF_DEBUG(HD_RPRIM_UPDATED).Msg(
                        "Transitioning from immutable to mutable BAR\n");

                } else if (!bufferSpecs.empty()) {
                    // (b2) Continue to use an immutable BAR (even if it means
                    // allocating a new one)

                    // See if we can share an immutable buffer primvar range
                    // include our existing sharing id so that we can take
                    // into account previously committed sources along
                    // with our new sources and computations.
                    _vertexPrimvarId = _ComputeSharedPrimvarId(_vertexPrimvarId,
                                                           allSources,
                                                           computations);

                    bool isFirstInstance = true;
                    range = _GetSharedPrimvarRange(_vertexPrimvarId,
                                           /*updatedOrAddedSpecs*/bufferSpecs,
                                           /*removedSpecs*/removedSpecs,
                                           /*curRange*/bar,
                                           &isFirstInstance,
                                           resourceRegistry);
                    
                    if (!isFirstInstance) {
                        sources.clear();
                        computations.clear();
                    }

                    TF_DEBUG(HD_RPRIM_UPDATED).Msg(
                        "Migrating from immutable to another immutable BAR\n");
                } else {
                    // No changes are being made to the existing immutable BAR.
                    range = bar;
                }

            } else {
                // (c) Exiting BAR is a mutable one.
                range = resourceRegistry->UpdateNonUniformBufferArrayRange(
                    HdTokens->primvar, bar, bufferSpecs, removedSpecs,
                    HdBufferArrayUsageHint());
            }
        }
    } else {
        // When primvar sharing is disabled, a mutable BAR is allocated/updated/
        // migrated as necessary.
        range = resourceRegistry->UpdateNonUniformBufferArrayRange(
                HdTokens->primvar, bar, bufferSpecs, removedSpecs,
                HdBufferArrayUsageHint());
    }

    HdStUpdateDrawItemBAR(
        range,
        drawItem->GetDrawingCoord()->GetVertexPrimvarIndex(),
        &_sharedData,
        sceneDelegate->GetRenderIndex());

    // if we've identified a "points" buffer, but sources is empty, this means
    // we're using primvar sharing and not computing our own points. flat
    // normals still needs the point data, so add it as a dependent source (i.e.
    // not scheduled for upload to a bar).
    if (points && sources.empty()) {
        resourceRegistry->AddSource(points);
    }

    // schedule buffer sources
    if (!sources.empty()) {
        // add sources to update queue
        resourceRegistry->AddSources(drawItem->GetVertexPrimvarRange(),
                                     std::move(sources));
    }
    if (!computations.empty()) {
        // add gpu computations to queue.
        for (auto const& comp : computations) {
            resourceRegistry->AddComputation(
                drawItem->GetVertexPrimvarRange(), comp);
        }
    }
    if (!separateComputationSources.empty()) {
        for (auto const& src : separateComputationSources) {
            resourceRegistry->AddSource(src);
        }
    }
}

void
HdStMesh::_PopulateFaceVaryingPrimvars(HdSceneDelegate *sceneDelegate,
                                       HdStDrawItem *drawItem,
                                       HdDirtyBits *dirtyBits,
                                       const HdMeshReprDesc &desc)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();
    HdPrimvarDescriptorVector primvars =
        HdStGetPrimvarDescriptors(this, drawItem, sceneDelegate,
                                  HdInterpolationFaceVarying);
    if (primvars.empty()) return;

    HdStResourceRegistrySharedPtr const& resourceRegistry = 
        std::static_pointer_cast<HdStResourceRegistry>(
        sceneDelegate->GetRenderIndex().GetResourceRegistry());

    HdBufferSourceSharedPtrVector sources;
    sources.reserve(primvars.size());

    int refineLevel = _GetRefineLevelForDesc(desc);
    int numFaceVaryings = _topology ? _topology->GetNumFaceVaryings() : 0;

    for (HdPrimvarDescriptor const& primvar: primvars) {
        // note: facevarying primvars don't have to be refined.
        if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, primvar.name)) {
            continue;
        }

        VtValue value = GetPrimvar(sceneDelegate, primvar.name);
        if (!value.IsEmpty()) {

            HdBufferSourceSharedPtr source(
                new HdVtBufferSource(primvar.name, value));

            // verify primvar length
            if ((int)source->GetNumElements() != numFaceVaryings) {
                HF_VALIDATION_WARN(id, 
                    "# of facevaryings mismatch (%d != %d)"
                    " for primvar %s",
                    (int)source->GetNumElements(), numFaceVaryings,
                    primvar.name.GetText());
                continue;
            }

            if (source->GetName() == HdTokens->normals) {
                _sceneNormalsInterpolation = HdInterpolationFaceVarying;
                _sceneNormals = true;
            }

            // FaceVarying primvar requires quadrangulation or triangulation,
            // depending on the subdivision scheme, but refinement of the
            // primvar is not needed even if the repr is refined, since we only
            // support linear interpolation until OpenSubdiv 3.1 supports it.

            //
            // XXX: there is a bug of quad and tris confusion. see bug 121414
            //
            if (_UseQuadIndices(sceneDelegate->GetRenderIndex(), _topology) ||
                 (refineLevel > 0 && !_topology->RefinesToTriangles())) {
                source = _QuadrangulateFaceVaryingPrimvar(source, _topology,
                    GetId(), resourceRegistry);
            } else {
                source = _TriangulateFaceVaryingPrimvar(source, _topology,
                    GetId(), resourceRegistry);
            }
            sources.push_back(source);
        }
    }

    HdBufferArrayRangeSharedPtr const &bar =
        drawItem->GetFaceVaryingPrimvarRange();

    if (HdStCanSkipBARAllocationOrUpdate(sources, bar, *dirtyBits)) {
        return;
    }

    // XXX: This should be based off the DirtyPrimvarDesc bit.
    bool hasDirtyPrimvarDesc = (*dirtyBits & HdChangeTracker::DirtyPrimvar);
    HdBufferSpecVector removedSpecs;
    if (hasDirtyPrimvarDesc) {
        // no internally generated facevarying primvars
        TfTokenVector internallyGeneratedPrimvars; // empty
        removedSpecs = HdStGetRemovedPrimvarBufferSpecs(bar, primvars, 
            internallyGeneratedPrimvars, id);
    }

    HdBufferSpecVector bufferSpecs;
    HdBufferSpec::GetBufferSpecs(sources, &bufferSpecs);

    HdBufferArrayRangeSharedPtr range =
        resourceRegistry->UpdateNonUniformBufferArrayRange(
            HdTokens->primvar, bar, bufferSpecs, removedSpecs,
            HdBufferArrayUsageHint());
    
    HdStUpdateDrawItemBAR(
        range,
        drawItem->GetDrawingCoord()->GetFaceVaryingPrimvarIndex(),
        &_sharedData,
        sceneDelegate->GetRenderIndex());

    TF_VERIFY(drawItem->GetFaceVaryingPrimvarRange()->IsValid());

    if (!sources.empty()) {
        resourceRegistry->AddSources(
            drawItem->GetFaceVaryingPrimvarRange(), std::move(sources));
    }
}

void
HdStMesh::_PopulateElementPrimvars(HdSceneDelegate *sceneDelegate,
                                   HdStDrawItem *drawItem,
                                   HdDirtyBits *dirtyBits,
                                   bool requireFlatNormals,
                                   HdBufferSourceSharedPtr const& points)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();
    HdStResourceRegistrySharedPtr const& resourceRegistry = 
        std::static_pointer_cast<HdStResourceRegistry>(
        sceneDelegate->GetRenderIndex().GetResourceRegistry());

    HdPrimvarDescriptorVector primvars =
        HdStGetPrimvarDescriptors(this, drawItem, sceneDelegate,
                                  HdInterpolationUniform);

    HdBufferSourceSharedPtrVector sources;
    sources.reserve(primvars.size());

    int numFaces = _topology ? _topology->GetNumFaces() : 0;

    for (HdPrimvarDescriptor const& primvar: primvars) {
        if (!HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, primvar.name))
            continue;

        VtValue value = GetPrimvar(sceneDelegate, primvar.name);
        if (!value.IsEmpty()) {
            HdBufferSourceSharedPtr source(
                new HdVtBufferSource(primvar.name, value));

            // verify primvar length
            if ((int)source->GetNumElements() != numFaces) {
                HF_VALIDATION_WARN(id,
                    "# of faces mismatch (%d != %d) for primvar %s",
                    (int)source->GetNumElements(), numFaces, 
                    primvar.name.GetText());
                continue;
            }

            if (source->GetName() == HdTokens->normals) {
                _sceneNormalsInterpolation = HdInterpolationUniform;
                _sceneNormals = true;
            }
            sources.push_back(source);
        }
    }

    HdComputationSharedPtrVector computations;

    if (requireFlatNormals && (*dirtyBits & DirtyFlatNormals))
    {
        *dirtyBits &= ~DirtyFlatNormals;
        TF_VERIFY(_topology);

        bool usePackedNormals = IsEnabledPackedNormals();
        TfToken normalsName = usePackedNormals ?
            HdStTokens->packedFlatNormals : HdStTokens->flatNormals;

        bool cpuNormals = (!HdStGLUtils::IsGpuComputeEnabled());
        // the flat normals computation uses the points primvar as a source.
        if (cpuNormals) {
            if (points) {
                HdBufferSourceSharedPtr normal(
                    new Hd_FlatNormalsComputation(
                        _topology.get(),
                        points, normalsName, usePackedNormals));
                sources.push_back(normal);
            }
        } else {
            // If we don't have the buffer source, we can get the points
            // data type from the bufferspec in the vertex bar. We need it
            // so we know what type normals should be.
            HdType pointsDataType = HdTypeInvalid;
            if (points) {
                pointsDataType = points->GetTupleType().type;
            } else {
                pointsDataType = _GetPointsDataTypeFromBar(drawItem);
            }

            if (pointsDataType != HdTypeInvalid) {
                // Flat normals will compute normals as the same datatype
                // as points, unless we ask for packed normals.
                // This is unfortunate; can we force them to be float?
                HdComputationSharedPtr flatNormalsComputation(
                    new HdSt_FlatNormalsComputationGPU(
                        drawItem->GetTopologyRange(),
                        drawItem->GetVertexPrimvarRange(),
                        numFaces,
                        HdTokens->points,
                        normalsName,
                        pointsDataType,
                        usePackedNormals));
                computations.push_back(flatNormalsComputation);
            }
        }
    }

    HdBufferArrayRangeSharedPtr const &bar = drawItem->GetElementPrimvarRange();
    
    if (HdStCanSkipBARAllocationOrUpdate(sources, computations, bar,
            *dirtyBits)) {
        return;
    }

    // XXX: This should be based off the DirtyPrimvarDesc bit.
    bool hasDirtyPrimvarDesc = (*dirtyBits & HdChangeTracker::DirtyPrimvar);
    HdBufferSpecVector removedSpecs;
    if (hasDirtyPrimvarDesc) {
        static TfTokenVector internallyGeneratedPrimvars =
            { HdStTokens->flatNormals, HdStTokens->packedFlatNormals };
        removedSpecs = HdStGetRemovedPrimvarBufferSpecs(bar, primvars, 
            internallyGeneratedPrimvars, id);
    }

    HdBufferSpecVector bufferSpecs;
    HdBufferSpec::GetBufferSpecs(sources, &bufferSpecs);
    HdBufferSpec::GetBufferSpecs(computations, &bufferSpecs);

    HdBufferArrayRangeSharedPtr range =
        resourceRegistry->UpdateNonUniformBufferArrayRange(
            HdTokens->primvar, bar, bufferSpecs, removedSpecs,
            HdBufferArrayUsageHint());

    HdStUpdateDrawItemBAR(
        range,
        drawItem->GetDrawingCoord()->GetElementPrimvarIndex(),
        &_sharedData,
        sceneDelegate->GetRenderIndex());

    TF_VERIFY(drawItem->GetElementPrimvarRange()->IsValid());

    if (!sources.empty()) {
        resourceRegistry->AddSources(
            drawItem->GetElementPrimvarRange(), std::move(sources));
    }
    if (!computations.empty()) {
        // add gpu computations to queue.
        for (auto const& comp : computations) {
            resourceRegistry->AddComputation(
                drawItem->GetElementPrimvarRange(), comp);
        }
    }
}

HdType
HdStMesh::_GetPointsDataTypeFromBar(HdStDrawItem *drawItem) const
{
    // GPU normal computations can read points data out of the vertex bar,
    // but they need to know the type to generate buffer layouts and to
    // properly type the normal output.
    //
    // We can read the type data from the GPU resource, but one gotcha is that
    // the topology might have changed, such that the GPU resource no longer
    // matches the topology. Therefore, the code needs to check that the gpu
    // buffer is valid for the current topology before using it.

    HdType pointsDataType = HdTypeInvalid;

    if (HdBufferArrayRangeSharedPtr const &bar =
            drawItem->GetVertexPrimvarRange()) {
        if (bar->IsValid()) {
            HdStBufferArrayRangeGLSharedPtr bar_ =
                std::static_pointer_cast<HdStBufferArrayRangeGL>
                (bar);
            HdStBufferResourceGLSharedPtr pointsResource =
                bar_->GetResource(HdTokens->points);
            if (pointsResource) {
                pointsDataType = pointsResource->GetTupleType().type;
            }
        }
    }

    return pointsDataType;
}

bool
HdStMesh::_UseQuadIndices(
        const HdRenderIndex &renderIndex,
        HdSt_MeshTopologySharedPtr const & topology) const
{
    // We should never quadrangulate for subdivision schemes
    // which refine to triangles (like Loop)
    if (topology->RefinesToTriangles()) {
        return false;
    }

    const HdStMaterial *material = static_cast<const HdStMaterial *>(
                                  renderIndex.GetSprim(HdPrimTypeTokens->material,
                                                       GetMaterialId()));
    if (material && material->HasPtex()) {
        return true;
    }

    // Fallback to the environment variable, which allows forcing of
    // quadrangulation for debugging/testing.
    return _IsEnabledForceQuadrangulate();
}

bool
HdStMesh::_UseLimitRefinement(const HdRenderIndex &renderIndex) const
{
    const HdStMaterial *material =
        static_cast<const HdStMaterial *>(
                renderIndex.GetSprim(HdPrimTypeTokens->material,
                                     GetMaterialId()));

    if (material && material->HasLimitSurfaceEvaluation()) {
        return true;
    }

    return false;
}

bool
HdStMesh::_UseSmoothNormals(HdSt_MeshTopologySharedPtr const& topology) const
{
    if (_flatShadingEnabled || _limitNormals ||
        topology->GetScheme() == PxOsdOpenSubdivTokens->none ||
        topology->GetScheme() == PxOsdOpenSubdivTokens->bilinear) {
        return false;
    }
    return true;
}

bool
HdStMesh::_UseFlatNormals(const HdMeshReprDesc &desc) const
{
    if (_GetRefineLevelForDesc(desc) > 0 ||
        desc.geomStyle == HdMeshGeomStylePoints) {
        return false;
    }
    return true;
}

const TfToken&
HdStMesh::_GetMaterialTag(const HdRenderIndex &renderIndex) const
{
    const HdStMaterial *material =
        static_cast<const HdStMaterial *>(
                renderIndex.GetSprim(HdPrimTypeTokens->material,
                                     GetMaterialId()));

    if (material) {
        return material->GetMaterialTag();
    }

    // A material may have been unbound, we should clear the old tag
    return HdMaterialTagTokens->defaultMaterialTag;
}

static std::string
_GetMixinShaderSource(TfToken const &shaderStageKey)
{
    if (shaderStageKey.IsEmpty()) {
        return std::string("");
    }

    // TODO: each delegate should provide their own package of mixin shaders
    // the lighting mixins are fallback only.
    static std::once_flag firstUse;
    static std::unique_ptr<HioGlslfx> mixinFX;
   
    std::call_once(firstUse, [](){
        std::string filePath = HdStPackageLightingIntegrationShader();
        mixinFX.reset(new HioGlslfx(filePath));
    });

    return mixinFX->GetSource(shaderStageKey);
}

static HdStShaderCodeSharedPtr
_GetMaterialShader(
    HdStMesh const * mesh,
    HdSceneDelegate * sceneDelegate)
{
    TfToken mixinKey = 
        mesh->GetShadingStyle(sceneDelegate).GetWithDefault<TfToken>();
    std::string mixinSource = _GetMixinShaderSource(mixinKey);
    return HdStGetMaterialShader(mesh, sceneDelegate, mixinSource);
}

HdBufferArrayRangeSharedPtr
HdStMesh::_GetSharedPrimvarRange(uint64_t primvarId,
    HdBufferSpecVector const &updatedOrAddedSpecs,
    HdBufferSpecVector const &removedSpecs,
    HdBufferArrayRangeSharedPtr const &curRange,
    bool * isFirstInstance,
    HdStResourceRegistrySharedPtr const &resourceRegistry) const
{
    HdInstance<HdBufferArrayRangeSharedPtr> barInstance =
        resourceRegistry->RegisterPrimvarRange(primvarId);

    HdBufferArrayRangeSharedPtr range;

    if (barInstance.IsFirstInstance()) {
        range = resourceRegistry->UpdateNonUniformImmutableBufferArrayRange(
                    HdTokens->primvar,
                    curRange,
                    updatedOrAddedSpecs,
                    removedSpecs,
                    HdBufferArrayUsageHint());

        barInstance.SetValue(range);
    } else {
        range = barInstance.GetValue();
    }

    if (isFirstInstance) {
        *isFirstInstance = barInstance.IsFirstInstance();
    }
    return range;
}

void
HdStMesh::_UpdateDrawItem(HdSceneDelegate *sceneDelegate,
                          HdStDrawItem *drawItem,
                          HdDirtyBits *dirtyBits,
                          const HdMeshReprDesc &desc,
                          bool requireSmoothNormals,
                          bool requireFlatNormals)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();

    HdStResourceRegistrySharedPtr const& resourceRegistry = 
        std::static_pointer_cast<HdStResourceRegistry>(
        sceneDelegate->GetRenderIndex().GetResourceRegistry());

    /* VISIBILITY */
    _UpdateVisibility(sceneDelegate, dirtyBits);

    /* MATERIAL SHADER (may affect subsequent primvar population) */
    if ((*dirtyBits & (HdChangeTracker::DirtyInstancer |
                       HdChangeTracker::NewRepr)) ||
        HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id)) {
        drawItem->SetMaterialShader(_GetMaterialShader(this, sceneDelegate));
    }

    /* TOPOLOGY */
    // XXX: _PopulateTopology should be split into two phase
    //      for scene dirtybits and for repr dirtybits.
    if (*dirtyBits & (HdChangeTracker::DirtyTopology
                    | HdChangeTracker::DirtyDisplayStyle
                    | HdChangeTracker::DirtySubdivTags
                                     | DirtyIndices
                                     | DirtyHullIndices
                                     | DirtyPointsIndices)) {
        _PopulateTopology(sceneDelegate, drawItem, dirtyBits, desc);
    }

    if (*dirtyBits & HdChangeTracker::DirtyDoubleSided) {
        _doubleSided = IsDoubleSided(sceneDelegate);
    }
    if (*dirtyBits & HdChangeTracker::DirtyCullStyle) {
        _cullStyle = GetCullStyle(sceneDelegate);
    }

    // If it's impossible for this mesh to use smooth normals, we can clear
    // the dirty bit without computing them.  This is ok because the
    // conditions that are checked (topology, display style) will forward their
    // invalidation to smooth normals in PropagateDirtyBits.
    if (!_UseSmoothNormals(_topology)) {
        requireSmoothNormals = false;
        *dirtyBits &= ~DirtySmoothNormals;
    }

    // If the subdivision scheme is "none", disable flat normal generation.
    if (_topology->GetScheme() == PxOsdOpenSubdivTokens->none) {
        requireFlatNormals = false;
        *dirtyBits &= ~DirtyFlatNormals;
    }
    // Flat shading is based on whether the repr wants flat shading (captured
    // in the passed-in requireFlatNormals), whether the prim wants flat
    // shading, and whether the repr desc allows it.
    requireFlatNormals |= _flatShadingEnabled;
    if (!_UseFlatNormals(desc)) {
        requireFlatNormals = false;
    }

    if (requireSmoothNormals && !_vertexAdjacency) {
        _PopulateAdjacency(resourceRegistry);
    }

    /* CONSTANT PRIMVARS, TRANSFORM, EXTENT AND PRIMID */
    if (HdStShouldPopulateConstantPrimvars(dirtyBits, id)) {
        HdPrimvarDescriptorVector constantPrimvars =
            HdStGetPrimvarDescriptors(this, drawItem, sceneDelegate,
                                        HdInterpolationConstant);

        HdStPopulateConstantPrimvars(this, &_sharedData, sceneDelegate, 
            drawItem, dirtyBits, constantPrimvars);

        // Check if normals are provided as a constant primvar
        for (const HdPrimvarDescriptor& pv : constantPrimvars) {
            if (pv.name == HdTokens->normals) {
                _sceneNormalsInterpolation = HdInterpolationConstant;
                _sceneNormals = true;
            }
        }
    }

    /* INSTANCE PRIMVARS */
    if (!GetInstancerId().IsEmpty()) {
        HdStInstancer *instancer = static_cast<HdStInstancer*>(
            sceneDelegate->GetRenderIndex().GetInstancer(GetInstancerId()));
        if (TF_VERIFY(instancer)) {
            instancer->PopulateDrawItem(this, drawItem,
                                        &_sharedData, *dirtyBits);
        }
    }

    HdBufferSourceSharedPtr points;

    /* VERTEX PRIMVARS */
    if ((*dirtyBits & HdChangeTracker::NewRepr) ||
        HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id)) {
        _PopulateVertexPrimvars(sceneDelegate, drawItem, dirtyBits,
                                requireSmoothNormals, &points);
    }

    /* FACEVARYING PRIMVARS */
    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id)) {
        _PopulateFaceVaryingPrimvars(sceneDelegate, drawItem, dirtyBits, desc);
    }

    /* ELEMENT PRIMVARS */
    if ((requireFlatNormals && (*dirtyBits & DirtyFlatNormals)) ||
        HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id)) {
        _PopulateElementPrimvars(sceneDelegate, drawItem, dirtyBits,
                                 requireFlatNormals, points);
    }

    // When we have multiple drawitems for the same mesh we need to clean the
    // bits for all the data fields touched in this function, otherwise it
    // will try to extract topology (for instance) twice, and this won't
    // work with delegates that don't keep information around once extracted.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;

    TF_VERIFY(drawItem->GetConstantPrimvarRange());
    // Topology and VertexPrimvar may be null, if the mesh has zero faces.
    // Element primvar, Facevarying primvar and Instance primvar are optional
}

void
HdStMesh::_UpdateDrawItemGeometricShader(HdSceneDelegate *sceneDelegate,
                                         HdStDrawItem *drawItem,
                                         const HdMeshReprDesc &desc,
                                         size_t drawItemIdForDesc)
{
    HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();

    bool hasFaceVaryingPrimvars =
        (bool)drawItem->GetFaceVaryingPrimvarRange();

    int refineLevel = _GetRefineLevelForDesc(desc);

    using PrimitiveType = HdSt_GeometricShader::PrimitiveType;
    PrimitiveType primType = PrimitiveType::PRIM_MESH_COARSE_TRIANGLES;

    if (desc.geomStyle == HdMeshGeomStylePoints) {
        primType = PrimitiveType::PRIM_POINTS;
    } else if (refineLevel > 0) {
        if (_topology->RefinesToBSplinePatches()) {
            primType = PrimitiveType::PRIM_MESH_BSPLINE;
        } else if (_topology->RefinesToBoxSplineTrianglePatches()) {
            primType = PrimitiveType::PRIM_MESH_BOXSPLINETRIANGLE;
        } else if (_topology->RefinesToTriangles()) {
            // uniform loop subdivision generates triangles.
            primType = PrimitiveType::PRIM_MESH_REFINED_TRIANGLES;
        } else {
            // uniform catmark/bilinear subdivision generates quads.
            primType = PrimitiveType::PRIM_MESH_REFINED_QUADS;
        }
    } else if (_UseQuadIndices(renderIndex, _topology)) {
        // quadrangulate coarse mesh (e.g. for ptex)
        primType = PrimitiveType::PRIM_MESH_COARSE_QUADS;
    }

    // resolve geom style, cull style
    HdCullStyle cullStyle = desc.cullStyle;
    HdMeshGeomStyle geomStyle = desc.geomStyle;

    // Should the geometric shader expect computed smooth normals for this mesh?
    bool hasGeneratedSmoothNormals = !_limitNormals &&
        _topology->GetScheme() != PxOsdOpenSubdivTokens->none &&
        _topology->GetScheme() != PxOsdOpenSubdivTokens->bilinear;

    // Should the geometric shader expect computed flat normals for this mesh?
    bool hasGeneratedFlatNormals = _UseFlatNormals(desc) &&
        _topology->GetScheme() != PxOsdOpenSubdivTokens->none;

    // Has the draw style been forced to flat-shading?
    bool forceFlatShading =
        _flatShadingEnabled || desc.flatShadingEnabled;

    // Resolve normals interpolation.
    HdInterpolation normalsInterpolation = HdInterpolationVertex;
    if (_sceneNormals) {
        normalsInterpolation = _sceneNormalsInterpolation;
    }

    // Resolve normals source.
    HdSt_MeshShaderKey::NormalSource normalsSource;
    if (forceFlatShading) {
        if (hasGeneratedFlatNormals) {
            normalsSource = HdSt_MeshShaderKey::NormalSourceFlat;
        } else {
            normalsSource = HdSt_MeshShaderKey::NormalSourceGeometryShader;
        }
    } else if (_limitNormals) {
        normalsSource = HdSt_MeshShaderKey::NormalSourceLimit;
    } else if (hasGeneratedSmoothNormals) {
        normalsSource = HdSt_MeshShaderKey::NormalSourceSmooth;
    } else if (_sceneNormals) {
        normalsSource = HdSt_MeshShaderKey::NormalSourceScene;
    } else {
        normalsSource = HdSt_MeshShaderKey::NormalSourceGeometryShader;
    }

    // if the repr doesn't have an opinion about cullstyle, use the
    // prim's default (it could also be DontCare, then renderPass's
    // cullStyle is going to be used).
    //
    // i.e.
    //   Repr CullStyle > Rprim CullStyle > RenderPass CullStyle
    //
    if (cullStyle == HdCullStyleDontCare) {
        cullStyle = _cullStyle;
    }

    bool blendWireframeColor = desc.blendWireframeColor;

    // Check if the shader bound to this mesh has a custom displacement
    // terminal, or uses ptex, so that we know whether to include the geometry
    // shader.
    const HdStMaterial *material = static_cast<const HdStMaterial *>(
            renderIndex.GetSprim(HdPrimTypeTokens->material, GetMaterialId()));

    bool hasCustomDisplacementTerminal =
        material && material->HasDisplacement();
    bool hasPtex = material && material->HasPtex();

    bool hasTopologicalVisibility =
        (bool) drawItem->GetTopologyVisibilityRange();

    // Enable displacement shading only if the repr enables it, and the
    // entrypoint exists.
    bool useCustomDisplacement = hasCustomDisplacementTerminal &&
        desc.useCustomDisplacement && _displacementEnabled;

    // The edge geomstyles below are rasterized as lines.
    // See HdSt_GeometricShader::BindResources()
    bool rasterizedAsLines = 
         (desc.geomStyle == HdMeshGeomStyleEdgeOnly ||
         desc.geomStyle == HdMeshGeomStyleHullEdgeOnly);
    bool discardIfNotActiveSelected = rasterizedAsLines && 
                                     (drawItemIdForDesc == 1);
    bool discardIfNotRolloverSelected = rasterizedAsLines && 
                                     (drawItemIdForDesc == 2);

    // create a shaderKey and set to the geometric shader.
    HdSt_MeshShaderKey shaderKey(primType,
                                 desc.shadingTerminal,
                                 useCustomDisplacement,
                                 normalsSource,
                                 normalsInterpolation,
                                 _doubleSided || desc.doubleSided,
                                 hasFaceVaryingPrimvars || hasPtex,
                                 hasTopologicalVisibility,
                                 blendWireframeColor,
                                 cullStyle,
                                 geomStyle,
                                 desc.lineWidth,
                                 desc.enableScalarOverride,
                                 discardIfNotActiveSelected,
                                 discardIfNotRolloverSelected);

    HdStResourceRegistrySharedPtr resourceRegistry =
        std::static_pointer_cast<HdStResourceRegistry>(
            renderIndex.GetResourceRegistry());

    HdSt_GeometricShaderSharedPtr geomShader =
        HdSt_GeometricShader::Create(shaderKey, resourceRegistry);

    TF_VERIFY(geomShader);

    if (geomShader != drawItem->GetGeometricShader())
    {
        drawItem->SetGeometricShader(geomShader);

        // If the gometric shader changes, we need to do a deep validation of
        // batches, so they can be rebuilt if necessary.
        renderIndex.GetChangeTracker().MarkBatchesDirty();
    }
}

// virtual
HdDirtyBits
HdStMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    // If subdiv tags are dirty, topology needs to be recomputed.
    // The latter implies we'll need to recompute all primvar data.
    // Any data fetched by the scene delegate should be marked dirty here.
    if (bits & HdChangeTracker::DirtySubdivTags) {
        bits |= (HdChangeTracker::DirtyPoints   |
                HdChangeTracker::DirtyNormals  |
                HdChangeTracker::DirtyPrimvar  |
                HdChangeTracker::DirtyTopology |
                HdChangeTracker::DirtyDisplayStyle);
    } else if (bits & HdChangeTracker::DirtyTopology) {
        // Unlike basis curves, we always request refineLevel when topology is
        // dirty
        bits |= HdChangeTracker::DirtySubdivTags |
                HdChangeTracker::DirtyDisplayStyle;
    }

    // A change of material means that the Quadrangulate state may have
    // changed.
    if (bits & HdChangeTracker::DirtyMaterialId) {
        bits |= (HdChangeTracker::DirtyPoints   |
                HdChangeTracker::DirtyNormals  |
                HdChangeTracker::DirtyPrimvar  |
                HdChangeTracker::DirtyTopology);
    }

    // If points, display style, or topology changed, recompute normals.
    if (bits & (HdChangeTracker::DirtyPoints |
                HdChangeTracker::DirtyDisplayStyle |
                HdChangeTracker::DirtyTopology)) {
        bits |= _customDirtyBitsInUse &
            (DirtySmoothNormals | DirtyFlatNormals);
    }

    // If the topology is dirty, recompute custom indices resources.
    if (bits & HdChangeTracker::DirtyTopology) {
        bits |= _customDirtyBitsInUse &
                   (DirtyIndices      |
                    DirtyHullIndices  |
                    DirtyPointsIndices);
    }

    // If normals are dirty and we are doing CPU normals
    // then the normals computation needs the points primvar
    // so mark points as dirty, so that the scene delegate will provide
    // the data.
    if ((bits & (DirtySmoothNormals | DirtyFlatNormals)) &&
        !HdStGLUtils::IsGpuComputeEnabled()) {
        bits |= HdChangeTracker::DirtyPoints;
    }

    return bits;
}

static
size_t _GetNumDrawItemsForDesc(const HdMeshReprDesc& reprDesc)
{
    // By default, each repr desc item maps to 1 draw item
    size_t numDrawItems = 1;
    switch (reprDesc.geomStyle) {
    case HdMeshGeomStyleInvalid:
        numDrawItems = 0;
        break;

    // The edge geomstyles (below) result in geometry rasterized as lines.
    // This has an interesting and unfortunate limitation in that a
    // shared edge corresponds to the face that was drawn first/last
    // (depending on the depth test), and hence, cannot be uniquely
    // identified.
    // For face selection highlighting, this means that only a subset of the
    // edges of a selected face may be highlighted.
    // In order to support correct face selection highlighting, we draw the
    // geometry two more times (one for each selection mode), discarding
    // fragments that don't correspond to a selected face in that mode.
    case HdMeshGeomStyleHullEdgeOnly:
    case HdMeshGeomStyleEdgeOnly:
        numDrawItems += HdSelection::HighlightModeCount;
        break;

    default:
        break;
    }

    return numDrawItems;
}

void
HdStMesh::_InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits)
{
    _ReprVector::iterator it = std::find_if(_reprs.begin(), _reprs.end(),
                                            _ReprComparator(reprToken));
    bool isNew = it == _reprs.end();
    if (isNew) {
        // add new repr
        _reprs.emplace_back(reprToken, std::make_shared<HdRepr>());
        HdReprSharedPtr &repr = _reprs.back().second;

        // set dirty bit to say we need to sync a new repr (buffer array
        // ranges may change)
        *dirtyBits |= HdChangeTracker::NewRepr;

        _MeshReprConfig::DescArray descs = _GetReprDesc(reprToken);

        // allocate all draw items
        for (size_t descIdx = 0; descIdx < descs.size(); ++descIdx) {
            const HdMeshReprDesc &desc = descs[descIdx];

            size_t numDrawItems = _GetNumDrawItemsForDesc(desc);
            if (numDrawItems == 0) continue;

            for (size_t itemId = 0; itemId < numDrawItems; itemId++) {
                HdRepr::DrawItemUniquePtr drawItem =
                    std::make_unique<HdStDrawItem>(&_sharedData);
                HdDrawingCoord *drawingCoord = drawItem->GetDrawingCoord();
                repr->AddDrawItem(std::move(drawItem));

                switch (desc.geomStyle) {
                case HdMeshGeomStyleHull:
                case HdMeshGeomStyleHullEdgeOnly:
                case HdMeshGeomStyleHullEdgeOnSurf:
                {
                    drawingCoord->SetTopologyIndex(HdStMesh::HullTopology);
                    if (!(_customDirtyBitsInUse & DirtyHullIndices)) {
                        _customDirtyBitsInUse |= DirtyHullIndices;
                        *dirtyBits |= DirtyHullIndices;
                    }
                    break;
                }

                case HdMeshGeomStylePoints:
                {
                    // in the current implementation, we use topology
                    // for points too, to draw a subset of vertex primvars
                    // (note that the points may be followed by the refined
                    // vertices)
                    drawingCoord->SetTopologyIndex(HdStMesh::PointsTopology);
                    if (!(_customDirtyBitsInUse & DirtyPointsIndices)) {
                        _customDirtyBitsInUse |= DirtyPointsIndices;
                        *dirtyBits |= DirtyPointsIndices;
                    }
                    break;
                }

                default:
                {
                    if (!(_customDirtyBitsInUse & DirtyIndices)) {
                        _customDirtyBitsInUse |= DirtyIndices;
                        *dirtyBits |= DirtyIndices;
                    }
                }
                }

                // Set up drawing coord instance primvars.
                drawingCoord->SetInstancePrimvarBaseIndex(
                    HdStMesh::InstancePrimvar);
            } // for each draw item

            if (desc.flatShadingEnabled) {
                if (!(_customDirtyBitsInUse & DirtyFlatNormals)) {
                    _customDirtyBitsInUse |= DirtyFlatNormals;
                    *dirtyBits |= DirtyFlatNormals;
                }
            } else {
                if (!(_customDirtyBitsInUse & DirtySmoothNormals)) {
                    _customDirtyBitsInUse |= DirtySmoothNormals;
                    *dirtyBits |= DirtySmoothNormals;
                }
            }
        } // for each repr desc for the repr
    } // if new repr
}

void
HdStMesh::_UpdateRepr(HdSceneDelegate *sceneDelegate,
                      TfToken const &reprToken,
                      HdDirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdReprSharedPtr const &curRepr = _GetRepr(reprToken);
    if (!curRepr) {
        return;
    }

    if (TfDebug::IsEnabled(HD_RPRIM_UPDATED)) {
        std::cout << "HdStMesh::GetRepr " << GetId()
                  << " Repr = " << reprToken << "\n";
        HdChangeTracker::DumpDirtyBits(*dirtyBits);
    }

    _MeshReprConfig::DescArray reprDescs = _GetReprDesc(reprToken);

    // Iterate through all reprdescs for the current repr to figure out if any 
    // of them requires smooth normals or flat normals. If either (or both)
    // are required, we will calculate them once and clean the bits.
    bool requireSmoothNormals = false;
    bool requireFlatNormals =  false;
    for (size_t descIdx = 0; descIdx < reprDescs.size(); ++descIdx) {
        const HdMeshReprDesc &desc = reprDescs[descIdx];
        if (desc.flatShadingEnabled) {
            requireFlatNormals = true;
        } else {
            requireSmoothNormals = true;
        }
    }

    // For each relevant draw item, update dirty buffer sources.
    int drawItemIndex = 0;
    for (size_t descIdx = 0; descIdx < reprDescs.size(); ++descIdx) {
        const HdMeshReprDesc &desc = reprDescs[descIdx];
        size_t numDrawItems = _GetNumDrawItemsForDesc(desc);
        if (numDrawItems == 0) continue;
        
        for (size_t itemId = 0; itemId < numDrawItems; itemId++) {
            HdStDrawItem *drawItem = static_cast<HdStDrawItem*>(
                curRepr->GetDrawItem(drawItemIndex++));

            if (HdChangeTracker::IsDirty(*dirtyBits)) {
                _UpdateDrawItem(sceneDelegate, drawItem, dirtyBits, desc,
                        requireSmoothNormals, requireFlatNormals);
            } 
        }
    }

    *dirtyBits &= ~HdChangeTracker::NewRepr;
}

void
HdStMesh::_UpdateShadersForAllReprs(HdSceneDelegate *sceneDelegate,
                                    bool updateMaterialShader,
                                    bool updateGeometricShader)
{
    TF_DEBUG(HD_RPRIM_UPDATED). Msg(
        "(%s) - Updating geometric and material shaders for draw "
        "items of all reprs.\n", GetId().GetText());

    // Look up the mixin source if necessary. This is a per-rprim glsl
    // snippet, to be mixed into the surface shader.
    HdStShaderCodeSharedPtr materialShader;
    if (updateMaterialShader) {
        materialShader = _GetMaterialShader(this, sceneDelegate);
    }

    for (auto const& reprPair : _reprs) {
        const TfToken &reprToken = reprPair.first;
        _MeshReprConfig::DescArray descs = _GetReprDesc(reprToken);
        HdReprSharedPtr repr = reprPair.second;

        int drawItemIndex = 0;
        for (size_t descIdx = 0; descIdx < descs.size(); ++descIdx) {
            size_t numDrawItems = _GetNumDrawItemsForDesc(descs[descIdx]);
            if (numDrawItems == 0) continue;

            for (size_t itemId = 0; itemId < numDrawItems; itemId++) {
                HdStDrawItem *drawItem = static_cast<HdStDrawItem*>(
                    repr->GetDrawItem(drawItemIndex++));

                if (updateMaterialShader) {
                    drawItem->SetMaterialShader(materialShader);
                }
                if (updateGeometricShader) {
                    _UpdateDrawItemGeometricShader(sceneDelegate,
                        drawItem, descs[descIdx], itemId);
                }
            }
        }
    }
}

HdDirtyBits
HdStMesh::GetInitialDirtyBitsMask() const
{
    HdDirtyBits mask = HdChangeTracker::Clean
        | HdChangeTracker::InitRepr
        | HdChangeTracker::DirtyCullStyle
        | HdChangeTracker::DirtyDoubleSided
        | HdChangeTracker::DirtyExtent
        | HdChangeTracker::DirtyNormals
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyPrimID
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyDisplayStyle
        | HdChangeTracker::DirtyRepr
        | HdChangeTracker::DirtyMaterialId
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        ;

    if (!GetInstancerId().IsEmpty()) {
        mask |= HdChangeTracker::DirtyInstancer;
    }

    return mask;
}

PXR_NAMESPACE_CLOSE_SCOPE

