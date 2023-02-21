//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2023, Cinesite VFX Ltd. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "GafferSceneUI/Private/MetadataValueParameterInspector.h"

#include "Gaffer/Metadata.h"

#include "IECore/StringAlgo.h"

using namespace IECore;
using namespace IECoreScene;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferSceneUI::Private;

MetadataValueParameterInspector::MetadataValueParameterInspector(
	const ScenePlugPtr &scene,
	const PlugPtr &editScope,
	const std::string &attributePattern,
	const InternedString &metadataKey
) :
	ParameterInspector( scene, editScope, "", ShaderNetwork::Parameter() ),
	m_attributePattern( attributePattern ),
	m_metadataKey( metadataKey )
{

}

InternedString MetadataValueParameterInspector::attributeToQuery( const ScenePlug *scene ) const
{
	auto attributes = scene->attributesPlug()->getValue();

	for( const auto &[attributeName, value] : attributes->members() )
	{
		if( StringAlgo::match( attributeName, m_attributePattern ) && value->typeId() == (IECore::TypeId)ShaderNetworkTypeId )
		{
			return attributeName;
		}
	}

	return "";
}

const ShaderNetwork::Parameter MetadataValueParameterInspector::parameterToQuery( const ScenePlug *scene ) const
{
	auto attributes = scene->attributesPlug()->getValue();
	if( auto shaderNetwork = attributes->member<ShaderNetwork>( attributeToQuery( scene ) ) )
	{
		const IECoreScene::Shader *shader = shaderNetwork->outputShader();
		InternedString metadataTarget = shader->getType() + ":" + shader->getName();

		if( auto parameterData = Metadata::value<StringData>( metadataTarget, m_metadataKey ) )
		{
			return ShaderNetwork::Parameter( "", parameterData->readable() );
		}
	}

	return ShaderNetwork::Parameter();
}