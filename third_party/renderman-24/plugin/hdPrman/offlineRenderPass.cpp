//
// Copyright 2021 Pixar
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
#include "hdPrman/offlineRenderPass.h"
#include "hdPrman/offlineRenderParam.h"

PXR_NAMESPACE_OPEN_SCOPE

HdPrman_OfflineRenderPass::HdPrman_OfflineRenderPass(
    HdRenderIndex *index,
    HdRprimCollection const &collection,
    std::shared_ptr<HdPrman_RenderParam> renderParam)
: HdRenderPass(index, collection)
, _converged(false)
{
    _offlineRenderParam =
        std::dynamic_pointer_cast<HdPrman_OfflineRenderParam>(renderParam);
}

HdPrman_OfflineRenderPass::~HdPrman_OfflineRenderPass() = default;

bool
HdPrman_OfflineRenderPass::IsConverged() const
{
    return _converged;
}

void
HdPrman_OfflineRenderPass::_Execute(
    HdRenderPassStateSharedPtr const& renderPassState,
    TfTokenVector const &renderTags)
{
    _offlineRenderParam->Render();
    _converged = true;
}

PXR_NAMESPACE_CLOSE_SCOPE
