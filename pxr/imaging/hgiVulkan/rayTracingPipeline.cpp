//
// Copyright 2020 Pixar
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
#include "pxr/base/tf/diagnostic.h"

#include "pxr/imaging/hgiVulkan/rayTracingPipeline.h"
#include "pxr/imaging/hgiVulkan/device.h"
#include "pxr/imaging/hgiVulkan/diagnostic.h"
#include "pxr/imaging/hgiVulkan/pipelineCache.h"
#include "pxr/imaging/hgiVulkan/shaderFunction.h"
#include "pxr/imaging/hgiVulkan/shaderProgram.h"
#include "pxr/imaging/hgiVulkan/conversions.h"

PXR_NAMESPACE_OPEN_SCOPE

HgiVulkanRayTracingPipeline::HgiVulkanRayTracingPipeline(
    HgiVulkanDevice* device,
    HgiRayTracingPipelineDesc const& desc)
    : HgiRayTracingPipeline(desc)
    , _device(device)
    , _inflightBits(0)
    , _vkPipeline(nullptr)
    , _vkPipelineLayout(nullptr)
{

    for (int i = 0; i < desc.descriptorSetLayouts.size(); i++) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for(int j=0;j< desc.descriptorSetLayouts[i].resourceBinding.size();j++)
        {
            HgiRayTracingPipelineResourceBindingDesc bindingDesc = desc.descriptorSetLayouts[i].resourceBinding[j];

            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = bindingDesc.bindingIndex;
            layoutBinding.descriptorType = HgiVulkanConversions::GetDescriptorType(bindingDesc.resourceType);
            layoutBinding.descriptorCount = bindingDesc.count;
            layoutBinding.stageFlags = HgiVulkanConversions::GetShaderStages(bindingDesc.stageUsage);
            bindings.push_back(layoutBinding);
        }

        VkDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCI{};
        descriptorSetlayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetlayoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
        descriptorSetlayoutCI.pBindings = bindings.data();
        TF_VERIFY(vkCreateDescriptorSetLayout(_device->GetVulkanDevice(), &descriptorSetlayoutCI, nullptr, &descriptorSetLayout)== VK_SUCCESS);
        _vkDescriptorSetLayouts.push_back(descriptorSetLayout);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.setLayoutCount = _vkDescriptorSetLayouts.size();
    pipelineLayoutCI.pSetLayouts = _vkDescriptorSetLayouts.data();
    TF_VERIFY(vkCreatePipelineLayout(_device->GetVulkanDevice(), &pipelineLayoutCI, nullptr, &_vkPipelineLayout)==VK_SUCCESS);

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
   
    for (int i = 0; i < desc.shaders.size(); i++) {
        HgiShaderFunctionHandle shader = desc.shaders[i].shader;
        VkPipelineShaderStageCreateInfo shaderStage;
        shaderStage = {};
        shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage = (VkShaderStageFlagBits) HgiVulkanConversions::GetShaderStages(shader->GetDescriptor().shaderStage);;
        shaderStage.module = (VkShaderModule)shader->GetRawResource();
        shaderStage.pName = desc.shaders[i].entryPoint.c_str();
        shaderStages.push_back(shaderStage);
    }
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;

    for (int i = 0; i < desc.groups.size(); i++) 
    {
        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type = HgiVulkanConversions::GetRayTracingShaderGroupType(desc.groups[i].type);
        shaderGroup.generalShader = desc.groups[i].generalShader == 0xFFFF ? VK_SHADER_UNUSED_KHR : desc.groups[i].generalShader;
        shaderGroup.closestHitShader = desc.groups[i].closestHitShader == 0xFFFF ? VK_SHADER_UNUSED_KHR : desc.groups[i].closestHitShader;
        shaderGroup.anyHitShader = desc.groups[i].anyHitShader == 0xFFFF ? VK_SHADER_UNUSED_KHR : desc.groups[i].anyHitShader;
        shaderGroup.intersectionShader = desc.groups[i].intersectionShader == 0xFFFF ? VK_SHADER_UNUSED_KHR : desc.groups[i].intersectionShader;
        shaderGroups.push_back(shaderGroup);
    }

    VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
    rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    rayTracingPipelineCI.pStages = shaderStages.data();
    rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
    rayTracingPipelineCI.pGroups = shaderGroups.data();
    rayTracingPipelineCI.maxPipelineRayRecursionDepth = desc.maxRayRecursionDepth;
    rayTracingPipelineCI.layout = _vkPipelineLayout;
    TF_VERIFY(_device->vkCreateRayTracingPipelinesKHR(_device->GetVulkanDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &_vkPipeline)== VK_SUCCESS);

    // Debug label
    if (!desc.debugName.empty()) {
        std::string debugLabel = "Pipeline " + desc.debugName;
        HgiVulkanSetDebugName(
            device,
            (uint64_t)_vkPipeline,
            VK_OBJECT_TYPE_PIPELINE,
            debugLabel.c_str());
    }
}

HgiVulkanRayTracingPipeline::~HgiVulkanRayTracingPipeline()
{
}

void
HgiVulkanRayTracingPipeline::BindPipeline(VkCommandBuffer cb)
{
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _vkPipeline);
}

VkPipelineLayout
HgiVulkanRayTracingPipeline::GetVulkanPipelineLayout() const
{
    return _vkPipelineLayout;
}

VkPipeline
HgiVulkanRayTracingPipeline::GetVulkanPipeline() const
{
    return _vkPipeline;
}

HgiVulkanDevice*
HgiVulkanRayTracingPipeline::GetDevice() const
{
    return _device;
}

uint64_t &
HgiVulkanRayTracingPipeline::GetInflightBits()
{
    return _inflightBits;
}

PXR_NAMESPACE_CLOSE_SCOPE
