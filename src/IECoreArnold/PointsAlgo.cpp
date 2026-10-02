//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2012-2016, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
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

#include "IECoreArnold/NodeAlgo.h"
#include "IECoreArnold/ShapeAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECore/MessageHandler.h"
#include "IECore/SimpleTypedData.h"

#include "fmt/format.h"

using namespace std;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreArnold;

namespace
{

const AtString g_gaussianArnoldString( "gaussian" );
const AtString g_gsOpacityArnoldString( "gs_opacity" );
const AtString g_gsRotationArnoldString( "gs_rotation" );
const AtString g_gsScaleArnoldString( "gs_scale" );
const AtString g_gsShArnoldString( "gs_sh" );
const AtString g_modeArnoldString( "mode" );
const AtString g_motionStartArnoldString( "motion_start" );
const AtString g_motionEndArnoldString( "motion_end" );
const AtString g_pointsArnoldString( "points" );
const AtString g_quadArnoldString( "quad" );
const AtString g_sphereArnoldString( "sphere" );

AtNode *convertStatic( const IECoreScene::PointsPrimitive *points, AtUniverse *universe, const std::string &nodeName, const AtNode *parentNode, const std::string &messageContext )
{

	AtNode *result = AiNode( universe, g_pointsArnoldString, AtString( nodeName.c_str() ), parentNode );

	// mode

	const StringData *t = points->variableData<StringData>( "type", PrimitiveVariable::Constant );
	if( t )
	{
		if( t->readable() == "particle" || t->readable()=="disk" )
		{
			// default type is disk - no need to do anything
		}
		else if( t->readable() == "sphere" )
		{
			AiNodeSetStr( result, g_modeArnoldString, g_sphereArnoldString );
		}
		else if( t->readable() == "patch" )
		{
			AiNodeSetStr( result, g_modeArnoldString, g_quadArnoldString );
		}
		else if( t->readable() == "gaussianSplat" )
		{
			AiNodeSetStr( result, g_modeArnoldString, g_gaussianArnoldString );
		}
		else
		{
			IECore::msg( IECore::Msg::Warning, messageContext, fmt::format( "Unknown type \"{}\" - reverting to disk mode.", t->readable() ) );
		}
	}

	// arbitrary user parameters

	const char *ignore[] = { "P", "width", "radius", "orientation", "sphericalHarmonicsCoefficient", nullptr };
	ShapeAlgo::convertPrimitiveVariables( points, result, ignore, messageContext );

	return result;

}

AtNode *convert( const IECoreScenePreview::Renderer::Samples<const IECoreScene::PointsPrimitive *> &samples, float motionStart, float motionEnd, AtUniverse *universe, const std::string &nodeName, const AtNode *parentNode, const std::string &messageContext )
{
	AtNode *result = convertStatic( samples.front(), universe, nodeName, parentNode, messageContext );

	const auto primitiveSamples = IECoreScenePreview::Renderer::staticSamplesCast<const Primitive *>( samples );
	if( !ShapeAlgo::convertP( primitiveSamples, result, g_pointsArnoldString, messageContext ) )
	{
		AiNodeDestroy( result );
		return nullptr;
	}

	ShapeAlgo::convertRadius( primitiveSamples, result, messageContext );

	AiNodeSetFlt( result, g_motionStartArnoldString, motionStart );
	AiNodeSetFlt( result, g_motionEndArnoldString, motionEnd );

	if( AiNodeGetStr( result, g_modeArnoldString ) == g_gaussianArnoldString )
	{
		const V3fVectorData *scaleData = samples.front()->variableData<V3fVectorData>( "scale", PrimitiveVariable::Interpolation::Vertex );
		/// \todo Warn if doesn't exist
		const std::vector<Imath::V3f> &scales = scaleData->readable();
		AiNodeSetArray( result, g_gsScaleArnoldString, AiArrayConvert( scales.size(), 1, AI_TYPE_VECTOR, scales.data() ) );

		const FloatVectorData *opacityData = samples.front()->variableData<FloatVectorData>( "opacity", PrimitiveVariable::Interpolation::Vertex );
		/// \todo Warn if doesn't exist
		const std::vector<float> &opacities = opacityData->readable();
		AiNodeSetArray( result, g_gsOpacityArnoldString, AiArrayConvert( opacities.size(), 1, AI_TYPE_FLOAT, opacities.data() ) );

		/// \todo Does this orientation satisfy the todo below about adding rotation?
		if( const QuatfVectorData *orientationData = samples.front()->variableData<QuatfVectorData>( "orientation", PrimitiveVariable::Vertex ) )
		{
			// Arnold wants the quaternions as groups of four floats with the imaginary part first.
			// Cortex represents them with the imaginary part second, so we need to flip them.
			const std::vector<Imath::Quatf> &orientation = orientationData->readable();
			AtArray *orientationArray = AiArrayAllocate( orientation.size() * 4, 1, AI_TYPE_FLOAT );
			float *shuffledOrientationArray = static_cast<float *>( AiArrayMap( orientationArray ) );
			for( size_t i = 0, eI = orientation.size(); i < eI; ++i )
			{
				shuffledOrientationArray[i * 4] = orientation[i].v.x;
				shuffledOrientationArray[i * 4 + 1] = orientation[i].v.y;
				shuffledOrientationArray[i * 4 + 2] = orientation[i].v.z;
				shuffledOrientationArray[i * 4 + 3] = orientation[i].r;
			}
			AiArrayUnmap( orientationArray );
			AiNodeSetArray( result, g_gsRotationArnoldString, orientationArray );
		}

		// Re-interleave the coefficients that Cortex split out into multiple primvars.

		const IntData *degreeData = samples.front()->variableData<IntData>( "sphericalHarmonicsDegree", PrimitiveVariable::Constant );

		const int degree = degreeData->readable();
		const int coefficientCount = ( degree + 1 ) * ( degree + 1 );

		std::vector<Imath::Color3f> allCoefficients( coefficientCount * samples.front()->getNumPoints() );

		for( size_t i = 0; i < coefficientCount; ++i )
		{
			const Color3fVectorData *coefficientData = samples.front()->variableData<Color3fVectorData>(
				std::string( "sphericalHarmonicsCoefficients[" ) + std::to_string( (int)i ) + "]",
				PrimitiveVariable::Vertex
			);

			if( !coefficientData )
			{
				throw IECore::Exception( fmt::format( "Could not find Gaussian Splat coeffient data set {}", i ) );
			}

			const std::vector<Imath::Color3f> &coefficients = coefficientData->readable();
			for( size_t j = 0, eJ = samples.front()->getNumPoints(); j < eJ; ++j )
			{
				allCoefficients[j * coefficientCount + i] = coefficients[j];
			}

		}

		// Normalize the first coefficient for each point, as done in arnold-usd.
		for( size_t i = 0, eI = samples.front()->getNumPoints(); i < eI; ++i )
		{
			allCoefficients[i * coefficientCount] = allCoefficients[i * coefficientCount] * 0.28209479177387814f + Imath::Color3f( 0.5f );
		}

		AiNodeSetArray( result, g_gsShArnoldString, AiArrayConvert( allCoefficients.size(), 1, AI_TYPE_RGB, allCoefficients.data() ) );
	}

	/// \todo Aspect, rotation

	return result;
}

NodeAlgo::ConverterDescription<PointsPrimitive> g_description( ::convert );

} // namespace
