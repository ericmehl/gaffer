//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2024, Cinesite VFX Ltd. All rights reserved.
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

#include "IECoreRenderMan/ShaderNetworkAlgo.h"

#include "ParamListAlgo.h"

#include "IECoreScene/ShaderNetworkAlgo.h"

#include "IECore/DataAlgo.h"
#include "IECore/LRUCache.h"
#include "IECore/MessageHandler.h"
#include "IECore/SearchPath.h"

#include "OSL/oslquery.h"

#include "boost/container/flat_map.hpp"
#include "boost/property_tree/xml_parser.hpp"

#include "fmt/format.h"

#include <unordered_set>

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreRenderMan;

//////////////////////////////////////////////////////////////////////////
// Internal utilities
//////////////////////////////////////////////////////////////////////////

namespace
{

struct ShaderInfo
{
	riley::ShadingNode::Type type = riley::ShadingNode::Type::k_Invalid;
	using ParameterTypeMap = std::unordered_map<InternedString, pxrcore::DataType>;
	ParameterTypeMap parameterTypes;
};

using ConstShaderInfoPtr = std::shared_ptr<const ShaderInfo>;

void loadParameterTypes( const boost::property_tree::ptree &tree, ShaderInfo::ParameterTypeMap &typeMap )
{
	for( const auto &child : tree )
	{
		if( child.first == "param" )
		{
			const string name = child.second.get<string>( "<xmlattr>.name" );
			const string type = child.second.get<string>( "<xmlattr>.type" );
			if( type == "int" )
			{
				typeMap[name] = pxrcore::DataType::k_integer;
			}
			else if( type == "float" )
			{
				typeMap[name] = pxrcore::DataType::k_float;
			}
			else if( type == "color" )
			{
				typeMap[name] = pxrcore::DataType::k_color;
			}
			else if( type == "point" )
			{
				typeMap[name] = pxrcore::DataType::k_point;
			}
			else if( type == "vector" )
			{
				typeMap[name] = pxrcore::DataType::k_vector;
			}
			else if( type == "normal" )
			{
				typeMap[name] = pxrcore::DataType::k_normal;
			}
			else if( type == "matrix" )
			{
				typeMap[name] = pxrcore::DataType::k_matrix;
			}
			else if( type == "string" )
			{
				typeMap[name] = pxrcore::DataType::k_string;
			}
			else if( type == "bxdf" )
			{
				typeMap[name] = pxrcore::DataType::k_bxdf;
			}
			else if( type == "lightfilter" )
			{
				typeMap[name] = pxrcore::DataType::k_lightfilter;
			}
			else if( type == "samplefilter" )
			{
				typeMap[name] = pxrcore::DataType::k_samplefilter;
			}
			else if( type == "displayfilter" )
			{
				typeMap[name] = pxrcore::DataType::k_displayfilter;
			}
			else if( type == "struct" )
			{
				typeMap[name] = pxrcore::DataType::k_struct;
			}
			else
			{
				IECore::msg( IECore::Msg::Warning, "IECoreRenderMan", fmt::format( "Unknown type `{}` for parameter \"{}\".", type, name ) );
			}
		}
		else if( child.first == "page" )
		{
			loadParameterTypes( child.second, typeMap );
		}
	}
}

ConstShaderInfoPtr shaderInfoFromArgsFile( const boost::filesystem::path file )
{
	std::ifstream argsStream( file.string() );

	boost::property_tree::ptree tree;
	boost::property_tree::read_xml( argsStream, tree );

	auto result = std::make_shared<ShaderInfo>();

	// Get type

	const string shaderType = tree.get<string>( "args.shaderType.tag.<xmlattr>.value" );
	if( shaderType == "pattern" )
	{
		result->type = riley::ShadingNode::Type::k_Pattern;
	}
	else if( shaderType == "bxdf" )
	{
		result->type = riley::ShadingNode::Type::k_Bxdf;
	}
	else if( shaderType == "integrator" )
	{
		result->type = riley::ShadingNode::Type::k_Integrator;
	}
	else if( shaderType == "light" )
	{
		result->type = riley::ShadingNode::Type::k_Light;
	}
	else if( shaderType == "lightfilter" )
	{
		result->type = riley::ShadingNode::Type::k_LightFilter;
	}
	else if( shaderType == "projection" )
	{
		result->type = riley::ShadingNode::Type::k_Projection;
	}
	else if( shaderType == "displacement" )
	{
		result->type = riley::ShadingNode::Type::k_Displacement;
	}
	else if( shaderType == "samplefilter" )
	{
		result->type = riley::ShadingNode::Type::k_SampleFilter;
	}
	else if( shaderType == "displayfilter" )
	{
		result->type = riley::ShadingNode::Type::k_DisplayFilter;
	}

	// Load parameters

	loadParameterTypes( tree.get_child( "args" ), result->parameterTypes );

	return result;
}

ConstShaderInfoPtr shaderInfoFromOSLQuery( OSL::OSLQuery &query )
{
	auto result = std::make_shared<ShaderInfo>();
	result->type = riley::ShadingNode::Type::k_Pattern;

	for( const auto &parameter : query )
	{
		if( parameter.type == OIIO::TypeInt )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_integer;
		}
		else if( parameter.type == OIIO::TypeFloat )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_float;
		}
		else if( parameter.type == OIIO::TypeColor )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_color;
		}
		else if( parameter.type == OIIO::TypePoint )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_point;
		}
		else if( parameter.type == OIIO::TypeVector )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_vector;
		}
		else if( parameter.type == OIIO::TypeNormal )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_normal;
		}
		else if( parameter.type == OIIO::TypeMatrix44 )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_matrix;
		}
		else if( parameter.type == OIIO::TypeString )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_string;
		}
		else if( parameter.isstruct )
		{
			result->parameterTypes[parameter.name.c_str()] = pxrcore::DataType::k_struct;
		}
		else
		{
			IECore::msg(
				IECore::Msg::Warning, "IECoreRenderMan",
				fmt::format(
					"Unknown type `{}` for parameter \"{}\" on shader \"{}\".",
					parameter.type, parameter.name, query.shadername()
				)
			);
		}
	}

	return result;
}

using ShaderInfoCache = IECore::LRUCache<string, ConstShaderInfoPtr>;

ShaderInfoCache g_shaderInfoCache(

	[]( const std::string &shaderName, size_t &cost ) -> ConstShaderInfoPtr {

		cost = 1;

		const char *rixPluginPath = getenv( "RMAN_RIXPLUGINPATH" );
		SearchPath rixSearchPath( rixPluginPath ? rixPluginPath : "" );
		boost::filesystem::path argsFileName = rixSearchPath.find( "Args/" + shaderName + ".args" );
		if( !argsFileName.empty() )
		{
			return shaderInfoFromArgsFile( argsFileName );
		}

		const char *oslSearchPath = getenv( "OSL_SHADER_PATHS" );
		OSL::OSLQuery oslQuery;
		if( oslQuery.open( shaderName, oslSearchPath ? oslSearchPath : "" ) )
		{
			return shaderInfoFromOSLQuery( oslQuery );
		}

		IECore::msg( IECore::Msg::Warning, "IECoreRenderMan", fmt::format( "Unable to find shader \"{}\".", shaderName ) );
		return nullptr;
	},

	/* maxCost = */ 10000

);

void convertConnection( const IECoreScene::ShaderNetwork::Connection &connection, const ShaderInfo *shaderInfo, RtParamList &paramList )
{
	auto it = shaderInfo->parameterTypes.find( connection.destination.name );
	if( it == shaderInfo->parameterTypes.end() )
	{
		IECore::msg(
			IECore::Msg::Warning, "IECoreRenderMan",
			fmt::format(
				"Unable to translate connection to `{}.{}` because its type is not known",
				connection.destination.shader.string(), connection.destination.name.string()
			)
		);
		return;
	}

	std::string reference = connection.source.shader;
	if( !connection.source.name.string().empty() )
	{
		reference += ":" + connection.source.name.string();
	}

	const RtUString referenceU( reference.c_str() );

	RtParamList::ParamInfo const info = {
		RtUString( connection.destination.name.c_str() ),
		it->second,
		pxrcore::DetailType::k_reference,
		1,
		false,
		false,
		false
	};

	paramList.SetParam( info, &referenceU );
}

using HandleSet = std::unordered_set<InternedString>;

void convertShaderNetworkWalk( const ShaderNetwork::Parameter &outputParameter, const IECoreScene::ShaderNetwork *shaderNetwork, vector<riley::ShadingNode> &shadingNodes, HandleSet &visited )
{
	if( !visited.insert( outputParameter.shader ).second )
	{
		return;
	}

	const IECoreScene::Shader *shader = shaderNetwork->getShader( outputParameter.shader );
	ConstShaderInfoPtr shaderInfo = g_shaderInfoCache.get( shader->getName() );
	if( !shaderInfo )
	{
		return;
	}

	riley::ShadingNode node = {
		shaderInfo->type,
		RtUString( shader->getName().c_str() ),
		RtUString( outputParameter.shader.c_str() ),
		RtParamList()
	};

	ParamListAlgo::convertParameters( shader->parameters(), node.params );

	for( const auto &connection : shaderNetwork->inputConnections( outputParameter.shader ) )
	{
		convertShaderNetworkWalk( connection.source, shaderNetwork, shadingNodes, visited );
		convertConnection( connection, shaderInfo.get(), node.params );
	}

	shadingNodes.push_back( node );
}

//////////////////////////////////////////////////////////////////////////
// USD conversion code
//////////////////////////////////////////////////////////////////////////

template<typename T>
T parameterValue( const Shader *shader, InternedString parameterName, const T &defaultValue )
{
	if( auto d = shader->parametersData()->member<TypedData<T>>( parameterName ) )
	{
		return d->readable();
	}

	if constexpr( is_same_v<remove_cv_t<T>, Color3f > )
	{
		// Correction for USD files which author `float3` instead of `color3f`.
		// See `ShaderNetworkAlgoTest.testConvertUSDFloat3ToColor3f()`.
		if( auto d = shader->parametersData()->member<V3fData>( parameterName ) )
		{
			return d->readable();
		}
		// Conversion of Color4 to Color3, for cases like converting `UsdUVTexture.scale`
		// to `PxrTexture.colorScale`.
		if( auto d = shader->parametersData()->member<Color4fData>( parameterName ) )
		{
			const Color4f &c = d->readable();
			return Color3f( c[0], c[1], c[2] );
		}
	}
	else if constexpr( is_same_v<remove_cv_t<T>, std::string> )
	{
		// Support for USD `token`, which will be loaded as `InternedString`, but which
		// we want to translate to `string`.
		if( auto d = shader->parametersData()->member<InternedStringData>( parameterName ) )
		{
			return d->readable().string();
		}
	}

	return defaultValue;
}

// Traits class to handle the GeometricTypedData fiasco.
template<typename T>
struct DataTraits
{

	using DataType = IECore::TypedData<T>;

};

template<typename T>
struct DataTraits<Vec2<T> >
{

	using DataType = IECore::GeometricTypedData<Vec2<T>>;

};

template<typename T>
struct DataTraits<Vec3<T> >
{

	using DataType = IECore::GeometricTypedData<Vec3<T>>;

};

template<typename T>
void transferUSDParameter( ShaderNetwork *network, InternedString shaderHandle, const Shader *usdShader, InternedString usdName, Shader *shader, InternedString name, const T &defaultValue )
{
	shader->parameters()[name] = new typename DataTraits<T>::DataType( parameterValue( usdShader, usdName, defaultValue ) );

	if( ShaderNetwork::Parameter input = network->input( { shaderHandle, usdName } ) )
	{
		if( name != usdName )
		{
			network->addConnection( { input, { shaderHandle, name } } );
			network->removeConnection( { input, { shaderHandle, usdName } } );
		}
	}
}

const InternedString g_aParameter( "a" );
const InternedString g_bParameter( "b" );
const InternedString g_clearcoatParameter( "clearcoat" );
const InternedString g_clearcoatFacingParameter( "clearcoatFacing" );
const InternedString g_clearcoatEdgeParameter( "clearcoatEdge" );
const InternedString g_clearcoatIorParameter( "clearcoatIor" );
const InternedString g_clearcoatRoughnessParameter( "clearcoatRoughness" );
const InternedString g_diffuseColorParameter( "diffuseColor" );
const InternedString g_diffuseGainParameter( "diffuseGain" );
const InternedString g_emissiveColorParameter( "emissiveColor" );
const InternedString g_gParameter( "g" );
const InternedString g_glowColorParameter( "glowColor" );
const InternedString g_glowGainParameter( "glowGain" );
const InternedString g_iorParameter( "ior" );
const InternedString g_metallicParameter( "metallic" );
const InternedString g_rParameter( "r" );
const InternedString g_roughnessParameter( "roughness" );
const InternedString g_specularColorParameter( "specularColor" );
const InternedString g_specularEdgeColorParameter( "specularEdgeColor" );
const InternedString g_specularFaceColorParameter( "specularFaceColor" );
const InternedString g_specularFresnelModeParameter( "specularFresnelMode" );
const InternedString g_specularIorParameter( "specularIor" );
const InternedString g_specularRoughnessParameter( "specularRoughness" );
const InternedString g_useSpecularWorkflowParameter( "useSpecularWorkflow" );
// const InternedString g_resultParameter( "result" );

// Map of USD shaders with `result` parameters to the output of their equivalent RenderMan shader.

/// \todo Update the shader names for RenderMan (these are copied from 3dl)
// const std::unordered_map<std::string, InternedString> g_resultParameterMap = {
// 	{ "UsdPrimvarReader_int", g_valueParameter },
// 	{ "UsdPrimvarReader_float", g_valueParameter },
// 	{ "UsdPrimvarReader_float2", g_oUVParameter },
// 	{ "UsdPrimvarReader_float3", g_valueParameter },
// 	{ "UsdPrimvarReader_float4", g_valueParameter },
// 	{ "UsdPrimvarReader_normal", g_valueParameter },
// 	{ "UsdPrimvarReader_point", g_valueParameter },
// 	{ "UsdPrimvarReader_vector", g_valueParameter },
// 	{ "UsdTransform2d", g_outUVParameter },
// };

const InternedString remapOutputParameterName( const InternedString name, const InternedString shaderName )
{
	// if( name == g_resultParameter )
	// {
	// 	// `result` parameters are remapped based on the shader name
	// 	const auto it = g_resultParameterMap.find( shaderName );
	// 	if( it != g_resultParameterMap.end() )
	// 	{
	// 		return it->second;
	// 	}
	// }

	return InternedString();
}

void replaceUSDShader( ShaderNetwork *network, InternedString handle, ShaderPtr &&newShader )
{
	const InternedString shaderName = network->getShader( handle )->getName();

	// Replace original shader with the new.
	network->setShader( handle, std::move( newShader ) );

	// Iterating over a copy because we will modify the range during iteration.
	ShaderNetwork::ConnectionRange range = network->outputConnections( handle );
	vector<ShaderNetwork::Connection> outputConnections( range.begin(), range.end() );
	for( auto &c : outputConnections )
	{
		if( c.source.name != g_rParameter && c.source.name != g_gParameter && c.source.name != g_bParameter && c.source.name != g_aParameter )
		{
			network->removeConnection( c );
			c.source.name = remapOutputParameterName( c.source.name, shaderName );
			network->addConnection( c );
		}
	}
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// External API
//////////////////////////////////////////////////////////////////////////

std::vector<riley::ShadingNode> IECoreRenderMan::ShaderNetworkAlgo::convert( const IECoreScene::ShaderNetwork *network )
{
	vector<riley::ShadingNode> result;
	result.reserve( network->size() );

	HandleSet visited;
	convertShaderNetworkWalk( network->getOutput(), network, result, visited );

	return result;
}

namespace IECoreRenderMan::ShaderNetworkAlgo
{

// https://github.com/PixarAnimationStudios/OpenUSD/blob/dev/third_party/renderman-26/shaders/UsdPreviewSurfaceParameters.osl
// demonstrates how to convert parameters from USD to a `PxrSurface`.
void convertUSDShaders( ShaderNetwork *shaderNetwork )
{
	for( const auto &[handle, shader] : shaderNetwork->shaders() )
	{
		ShaderPtr newShader;
		if( shader->getName() == "UsdPreviewSurface" )
		{
			Color3f diffuseGain( 1.f );
			newShader = new Shader( "PxrSurface" );

			// Easy stuff with a one-to-one correspondence between `UsdPreviewSurface` and `PxrSurface`.

			transferUSDParameter( shaderNetwork, handle, shader.get(), g_diffuseColorParameter, newShader.get(), g_diffuseColorParameter, Color3f( 0.18f ) );
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_roughnessParameter, newShader.get(), g_specularRoughnessParameter, 0.5f );

			// Physical specular mode
			newShader->parameters()[g_specularFresnelModeParameter] = new IntData( 1 );

			// Emission. USDPreviewSurface only has `emissiveColor`, which we transfer to `glowColor`. But then
			// we need to turn on RenderMan's `glowGain` so that the `glowColor` is actually used.
			transferUSDParameter( shaderNetwork, handle, shader.get(), g_emissiveColorParameter, newShader.get(), g_glowColorParameter, Color3f( 0.f ) );
			const bool hasEmission =
				shaderNetwork->input( { handle, g_glowColorParameter } ) ||
				parameterValue( newShader.get(), g_glowColorParameter, Color3f( 0.f ) ) != Color3f( 0.f )
			;
			newShader->parameters()[g_glowGainParameter] = new FloatData( hasEmission ? 1.f : 0.f );

			// Parameters needed for specular and clearcoat
			const float ior = parameterValue( shader.get(), g_iorParameter, 1.5f );
			float fZero = ( ( 1.f - ior ) / ( 1.f + ior ) );
			fZero *= fZero;

			// Specular
			if( parameterValue<int>( shader.get(), g_useSpecularWorkflowParameter, 0 ) )
			{
				transferUSDParameter( shaderNetwork, handle, shader.get(), g_specularColorParameter, newShader.get(), g_specularFaceColorParameter, Color3f( 0.f ) );
				newShader->parameters()[g_specularEdgeColorParameter] = new Color3fData( Color3f( 1.f ) );
			}
			else
			{
				float metallic = std::clamp( parameterValue( shader.get(), g_metallicParameter, 0.f ), 0.f, 1.f );
				Color3f diffuseColor = parameterValue( shader.get(), g_diffuseColorParameter, Color3f( 0.18f ) );
				const Color3f spec = Color3f( 1.f ) + ( diffuseColor - Color3f( 1.f ) ) * metallic;
				const Color3f fZeroSpec = fZero * spec;

				newShader->parameters()[g_specularFaceColorParameter] = new Color3fData(
					fZeroSpec + ( spec - fZeroSpec ) * metallic
				);
				newShader->parameters()[g_specularEdgeColorParameter] = new Color3fData( spec );
				diffuseGain *= 1.f - metallic;
			}

			if( diffuseGain != Color3f( 1.f ) )
			{
				newShader->parameters()[g_diffuseGainParameter] = new Color3fData( diffuseGain );
			}

			// Ior is float in USD and `Color3f` in RenderMan
			newShader->parameters()[g_specularIorParameter] = new Color3fData( Color3f( ior ) );
			if( ShaderNetwork::Parameter input = shaderNetwork->input( { handle, g_iorParameter } ) )
			{
				shaderNetwork->addConnection( { input, { handle, g_specularIorParameter } } );
				shaderNetwork->removeConnection( { input, { handle, g_iorParameter } } );
			}

			// Clearcoat
			const float clearcoat = parameterValue( shader.get(), g_clearcoatParameter, 0.f );
			if( clearcoat > 0.f )
			{

			}
		}

		if( newShader )
		{
			replaceUSDShader( shaderNetwork, handle, std::move( newShader ) );
		}
	}
	IECoreScene::ShaderNetworkAlgo::removeUnusedShaders( shaderNetwork );
}

} // namespace IECoreRenderMan::ShaderNetworkAlgo
