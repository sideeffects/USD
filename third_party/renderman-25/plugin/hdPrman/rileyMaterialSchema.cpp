//
// Copyright 2023 Pixar
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
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* **                                                                      ** */
/* ** This file is generated by a script.                                  ** */
/* **                                                                      ** */
/* ** Do not edit it directly (unless it is within a CUSTOM CODE section)! ** */
/* ** Edit hdSchemaDefs.py instead to make changes.                        ** */
/* **                                                                      ** */
/* ************************************************************************** */

#include "hdPrman/rileyMaterialSchema.h"

#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/base/trace/trace.h"

// --(BEGIN CUSTOM CODE: Includes)--
// --(END CUSTOM CODE: Includes)--

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdPrmanRileyMaterialSchemaTokens,
    HD_PRMAN_RILEY_MATERIAL_SCHEMA_TOKENS);

// --(BEGIN CUSTOM CODE: Schema Methods)--
// --(END CUSTOM CODE: Schema Methods)--

HdPrmanRileyShadingNodeVectorSchema
HdPrmanRileyMaterialSchema::GetBxdf()
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{
    return HdPrmanRileyShadingNodeVectorSchema(_GetTypedDataSource<HdVectorDataSource>(
        HdPrmanRileyMaterialSchemaTokens->bxdf));
}

HdPrmanRileyParamListSchema
HdPrmanRileyMaterialSchema::GetAttributes()
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{
    return HdPrmanRileyParamListSchema(_GetTypedDataSource<HdContainerDataSource>(
        HdPrmanRileyMaterialSchemaTokens->attributes));
}

/*static*/
HdContainerDataSourceHandle
HdPrmanRileyMaterialSchema::BuildRetained(
        const HdVectorDataSourceHandle &bxdf,
        const HdContainerDataSourceHandle &attributes
)
{
    TfToken _names[2];
    HdDataSourceBaseHandle _values[2];

    size_t _count = 0;

    if (bxdf) {
        _names[_count] = HdPrmanRileyMaterialSchemaTokens->bxdf;
        _values[_count++] = bxdf;
    }

    if (attributes) {
        _names[_count] = HdPrmanRileyMaterialSchemaTokens->attributes;
        _values[_count++] = attributes;
    }
    return HdRetainedContainerDataSource::New(_count, _names, _values);
}

HdPrmanRileyMaterialSchema::Builder &
HdPrmanRileyMaterialSchema::Builder::SetBxdf(
    const HdVectorDataSourceHandle &bxdf)
{
    _bxdf = bxdf;
    return *this;
}

HdPrmanRileyMaterialSchema::Builder &
HdPrmanRileyMaterialSchema::Builder::SetAttributes(
    const HdContainerDataSourceHandle &attributes)
{
    _attributes = attributes;
    return *this;
}

HdContainerDataSourceHandle
HdPrmanRileyMaterialSchema::Builder::Build()
{
    return HdPrmanRileyMaterialSchema::BuildRetained(
        _bxdf,
        _attributes
    );
}

/*static*/
HdPrmanRileyMaterialSchema
HdPrmanRileyMaterialSchema::GetFromParent(
        const HdContainerDataSourceHandle &fromParentContainer)
{
    return HdPrmanRileyMaterialSchema(
        fromParentContainer
        ? HdContainerDataSource::Cast(fromParentContainer->Get(
                HdPrmanRileyMaterialSchemaTokens->rileyMaterial))
        : nullptr);
}

/*static*/
const TfToken &
HdPrmanRileyMaterialSchema::GetSchemaToken()
{
    return HdPrmanRileyMaterialSchemaTokens->rileyMaterial;
}

/*static*/
const HdDataSourceLocator &
HdPrmanRileyMaterialSchema::GetDefaultLocator()
{
    static const HdDataSourceLocator locator(GetSchemaToken());
    return locator;
}

/* static */
const HdDataSourceLocator &
HdPrmanRileyMaterialSchema::GetBxdfLocator()
{
    static const HdDataSourceLocator locator =
        GetDefaultLocator().Append(
            HdPrmanRileyMaterialSchemaTokens->bxdf);
    return locator;
}

/* static */
const HdDataSourceLocator &
HdPrmanRileyMaterialSchema::GetAttributesLocator()
{
    static const HdDataSourceLocator locator =
        GetDefaultLocator().Append(
            HdPrmanRileyMaterialSchemaTokens->attributes);
    return locator;
} 

PXR_NAMESPACE_CLOSE_SCOPE