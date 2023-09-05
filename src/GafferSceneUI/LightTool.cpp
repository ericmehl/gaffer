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

#include "GafferSceneUI/LightTool.h"

#include "GafferSceneUI/Private/ParameterInspector.h"
#include "GafferSceneUI/Private/Inspector.h"
#include "GafferSceneUI/ContextAlgo.h"
#include "GafferSceneUI/SceneView.h"

#include "GafferScene/ScenePath.h"

#include "GafferUI/Handle.h"
#include "GafferUI/ImageGadget.h"
#include "GafferUI/StandardStyle.h"

#include "Gaffer/Animation.h"
#include "Gaffer/Metadata.h"
#include "Gaffer/MetadataAlgo.h"
#include "Gaffer/NameValuePlug.h"
#include "Gaffer/OptionalValuePlug.h"
#include "Gaffer/PathFilter.h"
#include "Gaffer/ScriptNode.h"
#include "Gaffer/TweakPlug.h"
#include "Gaffer/UndoScope.h"

#include "IECoreGL/Camera.h"
#include "IECoreGL/CurvesPrimitive.h"
#include "IECoreGL/Group.h"
#include "IECoreGL/MeshPrimitive.h"
#include "IECoreGL/ShaderLoader.h"
#include "IECoreGL/ShaderStateComponent.h"
#include "IECoreGL/TextureLoader.h"
#include "IECoreGL/ToGLMeshConverter.h"

#include "IECoreScene/CurvesPrimitive.h"
#include "IECoreScene/MeshPrimitive.h"

#include "IECore/AngleConversion.h"

#include "boost/algorithm/string/predicate.hpp"
#include "boost/bind/bind.hpp"

#include "OpenEXR/OpenEXRConfig.h"
#if OPENEXR_VERSION_MAJOR < 3
#include "OpenEXR/ImathMatrixAlgo.h"
#include "OpenEXR/ImathSphere.h"
#else
#include "Imath/ImathMatrixAlgo.h"
#include "Imath/ImathSphere.h"
#endif

#include "fmt/format.h"

#include <algorithm>

using namespace boost::placeholders;
using namespace Imath;
using namespace IECoreScene;
using namespace IECoreGL;
using namespace Gaffer;
using namespace GafferUI;
using namespace GafferScene;
using namespace GafferSceneUI;
using namespace GafferSceneUI::Private;

// ============================================================================
// Utility Functions
// ============================================================================

namespace
{

const std::string g_lightAttributePattern = "*light";

const Color3f g_lightToolHandleColor = Color3f( 0.825, 0.720f, 0.230f );

// Color from `StandardLightVisualiser`
const Color3f g_lightToolHighlightColor = Color3f( 1.0f, 0.835f, 0.07f );
const Color4f g_lightToolHighlightColor4 = Color4f( g_lightToolHighlightColor.x, g_lightToolHighlightColor.y, g_lightToolHighlightColor.z, 1.f );

const Color4f g_lightToolDisabledColor4 = Color4f( 0.4f, 0.4f, 0.4f, 1.f );

// Multiplied by the highlight color for drawing a parameter's previous value
const float g_highlightMultiplier = 0.8f;

const InternedString g_lightVisualiserScaleAttributeName( "gl:visualiser:scale" );
const InternedString g_frustumScaleAttributeName( "gl:light:frustumScale" );
const InternedString g_insetPenumbraType( "inset" );
const InternedString g_outsetPenumbraType( "outset" );
const InternedString g_absolutePenumbraType( "absolute" );

const float g_circleHandleWidth = 2.5f;
const float g_circleHandleWidthLarge = 3.f;
const float g_circleHandleSelectionWidth = 5.f;

const float g_lineHandleWidth = 0.5f;
const float g_lineHandleWidthLarge = 1.f;
const float g_lineSelectionWidth = 3.f;

const float g_minorLineHandleWidth = 0.25f;
const float g_minorLineHandleWidthLarge = 0.5f;

const float g_dragArcWidth = 24.f;

const float g_arrowHandleSize = g_circleHandleWidth * 2.f;
const float g_arrowHandleSizeLarge = g_circleHandleWidthLarge * 2.f;
const float g_arrowHandleSelectionSize = g_circleHandleSelectionWidth * 2.f;

const float g_quadLightHandleSizeMultiplier = 1.75f;

const Color4f g_hoverTextColor( 1, 1, 1, 1 );

const int g_warningTipCount = 3;

const ModifiableEvent::Modifiers g_quadLightConstrainAspectRatioKey = ModifiableEvent::Modifiers::Control;

enum class Axis { X, Y, Z };

// Return the plug that holds the value we need to edit, and make sure it's enabled.

/// \todo This currently does nothing to enable a row if is disabled. Is that worth doing?

Plug *activeValuePlug( Plug *sourcePlug )
{
	if( auto nameValuePlug = runTimeCast<NameValuePlug>( sourcePlug ) )
	{
		nameValuePlug->enabledPlug()->setValue( true );
		return nameValuePlug->valuePlug();
	}
	if( auto tweakPlug = runTimeCast<TweakPlug>( sourcePlug ) )
	{
		tweakPlug->enabledPlug()->setValue( true );
		return tweakPlug->valuePlug();
	}
	if( auto optionalValuePlug = runTimeCast<OptionalValuePlug>( sourcePlug ) )
	{
		optionalValuePlug->enabledPlug()->setValue( true );
		return optionalValuePlug->valuePlug();
	}
	return sourcePlug;
}

void setValueOrAddKey( FloatPlug *plug, float time, float value )
{
	if( Animation::isAnimated( plug ) )
	{
		Animation::CurvePlug *curve = Animation::acquire( plug );
		curve->insertKey( time, value );
	}
	else
	{
		plug->setValue( value );
	}
}

const char *constantFragSource()
{
	return
		"#version 120\n"
		""
		"#if __VERSION__ <= 120\n"
		"#define in varying\n"
		"#endif\n"
		""
		"in vec3 fragmentCs;"
		""
		"void main()"
		"{"
			"gl_FragColor = vec4( fragmentCs, 1 );"
		"}"
	;
}

const char *translucentConstantFragSource()
{
	return
		"#version 120\n"
		""
		"#if __VERSION__ <= 120\n"
		"#define in varying\n"
		"#endif\n"
		""
		"in vec3 fragmentCs;"
		""
		"void main()"
		"{"
		"	gl_FragColor = vec4( fragmentCs, 0.375 );"
		"}"
	;
}

const char *faceCameraVertexSource()
{
	return

		"#version 120\n"
		""
		"#if __VERSION__ <= 120\n"
		"#define in attribute\n"
		"#define out varying\n"
		"#endif\n"
		""
		"uniform vec3 Cs = vec3( 1, 1, 1 );"
		"uniform bool vertexCsActive = false;"
		""
		"in vec3 vertexP;"
		"in vec3 vertexN;"
		"in vec2 vertexuv;"
		"in vec3 vertexCs;"
		""
		"out vec3 geometryI;"
		"out vec3 geometryP;"
		"out vec3 geometryN;"
		"out vec2 geometryuv;"
		"out vec3 geometryCs;"
		""
		"out vec3 fragmentI;"
		"out vec3 fragmentP;"
		"out vec3 fragmentN;"
		"out vec2 fragmentuv;"
		"out vec3 fragmentCs;"
		""
		"void main()"
		"{"
		""
		""
		"	vec3 aimedXAxis, aimedYAxis, aimedZAxis;"

		"	aimedXAxis = normalize( gl_ModelViewMatrixInverse * vec4( 0, 0, -1, 0 ) ).xyz;"
		"	aimedYAxis = normalize( gl_ModelViewMatrixInverse * vec4( 0, 1, 0, 0 ) ).xyz;"
		"	aimedZAxis = normalize( gl_ModelViewMatrixInverse * vec4( 1, 0, 0, 0 ) ).xyz;"
		""
		"	vec3 pAimed = vertexP.x * aimedXAxis + vertexP.y * aimedYAxis + vertexP.z * aimedZAxis;"
		""
		"	vec4 pCam = gl_ModelViewMatrix * vec4( pAimed, 1 );"
		"	gl_Position = gl_ProjectionMatrix * pCam;"
		"	geometryP = pCam.xyz;"
		"	geometryN = normalize( gl_NormalMatrix * vertexN );"
		"	if( gl_ProjectionMatrix[2][3] != 0.0 )"
		"	{"
		"		geometryI = normalize( -pCam.xyz );"
		"	}"
		"	else"
		"	{"
		"		geometryI = vec3( 0, 0, -1 );"
		"	}"
		""
		"	geometryuv = vertexuv;"
		"	geometryCs = mix( Cs, vertexCs, float( vertexCsActive ) );"
		""
		"	fragmentI = geometryI;"
		"	fragmentP = geometryP;"
		"	fragmentN = geometryN;"
		"	fragmentuv = geometryuv;"
		"	fragmentCs = geometryCs;"
		"}"

	;
}

// Adapted from `Handle::rasterScaleFactor()` to get the raster scale factor for
// an arbitrary point in gadget space.
float rasterScaleFactor( const Handle *handle, const V3f &p )
{
	auto viewport = handle->ancestor<ViewportGadget>();
	const M44f fullTransform = handle->fullTransform();

	const M44f cameraToGadget = viewport->getCameraTransform() * fullTransform.inverse();
	V3f cameraUpInGadgetSpace = V3f( 0, 1, 0 );
	cameraToGadget.multDirMatrix( cameraUpInGadgetSpace, cameraUpInGadgetSpace );

	const V2f p1 = viewport->gadgetToRasterSpace( p, handle );
	const V2f p2 = viewport->gadgetToRasterSpace( p + cameraUpInGadgetSpace, handle );

	return 1.f / ( p1 - p2 ).length();
}

IECoreScene::MeshPrimitivePtr solidArc( float minorRadius, float majorRadius, float startFraction, float stopFraction, const Color3f &color )
{
	IntVectorDataPtr vertsPerPolyData = new IntVectorData;
	IntVectorDataPtr vertIdsData = new IntVectorData;
	V3fVectorDataPtr pData = new V3fVectorData;

	std::vector<int> &vertsPerPoly = vertsPerPolyData->writable();
	std::vector<int> &vertIds = vertIdsData->writable();
	std::vector<V3f> &p = pData->writable();

	const int numCircleDivisions = 100;
	const int numSegments = std::max( 1, (int)ceil( abs( stopFraction - startFraction ) * numCircleDivisions ) );

	for( int i = 0; i < numSegments + 1; ++i )
	{
		const float a = ( startFraction + ( stopFraction - startFraction ) * (float)i / (float)numSegments ) * 2.f * M_PI;
		p.push_back( V3f( sin( a ), 0, cos( a ) ) * minorRadius );
		p.push_back( V3f( sin( a ), 0, cos( a ) ) * majorRadius );
	}

	for( int i = 0; i < numSegments; ++i )
	{

		vertIds.push_back( i * 2 );
		vertIds.push_back( i * 2 + 1 );
		vertIds.push_back( i * 2 + 3 );
		vertIds.push_back( i * 2 + 2 );
		vertsPerPoly.push_back( 4 );
	}

	IECoreScene::MeshPrimitivePtr solidAngle = new IECoreScene::MeshPrimitive( vertsPerPolyData, vertIdsData, "linear", pData );
	solidAngle->variables["N"] = IECoreScene::PrimitiveVariable( IECoreScene::PrimitiveVariable::Constant, new V3fData( V3f( 0, 1, 0 ) ) );
	solidAngle->variables["Cs"] = IECoreScene::PrimitiveVariable( IECoreScene::PrimitiveVariable::Constant, new Color3fData( color ) );

	return solidAngle;
}

IECoreGL::MeshPrimitivePtr circle()
{
	static IECoreGL::MeshPrimitivePtr result;
	if( result )
	{
		return result;
	}

	IntVectorDataPtr vertsPerPolyData = new IntVectorData;
	IntVectorDataPtr vertIdsData = new IntVectorData;
	V3fVectorDataPtr pData = new V3fVectorData;

	std::vector<int> &vertsPerPoly = vertsPerPolyData->writable();
	std::vector<int> &vertIds = vertIdsData->writable();
	std::vector<V3f> &p = pData->writable();

	p.push_back( V3f( 0 ) );

	const int numSegments = 20;
	for( int i = 0; i < numSegments + 1; ++i )
	{
		const float a = ( (float)i / (float)numSegments ) * 2.f * M_PI;
		p.push_back( V3f( 0, cos( a ), -sin( a ) ) );  // Face the X-axis
	}
	for( int i = 0; i < numSegments; ++i )
	{
		vertIds.push_back( i + 1 );
		vertIds.push_back( i + 2 );
		vertIds.push_back( 0 );
		vertsPerPoly.push_back( 3 );
	}

	IECoreScene::MeshPrimitivePtr circle = new IECoreScene::MeshPrimitive( vertsPerPolyData, vertIdsData, "linear", pData );
	ToGLMeshConverterPtr converter = new ToGLMeshConverter( circle );
	result = runTimeCast<IECoreGL::MeshPrimitive>( converter->convert() );

	return result;
}

IECoreGL::MeshPrimitivePtr ring()
{
	static IECoreGL::MeshPrimitivePtr result;
	if( result )
	{
		return result;
	}

	IntVectorDataPtr vertsPerPolyData = new IntVectorData;
	IntVectorDataPtr vertIdsData = new IntVectorData;
	V3fVectorDataPtr pData = new V3fVectorData;

	std::vector<int> &vertsPerPoly = vertsPerPolyData->writable();
	std::vector<int> &vertIds = vertIdsData->writable();
	std::vector<V3f> &p = pData->writable();

	const int numSegments = 20;
	for( int i = 0; i < numSegments + 1; ++i )
	{
		const float a = ( (float)i / (float)numSegments ) * 2.f * M_PI;
		const V3f dir( 0, cos( a ) , -sin( a ) );  // Face the X-axis

		p.push_back( dir * 1.f );
		p.push_back( dir * 0.5f );
	}
	for( int i = 0; i < numSegments; ++i )
	{
		vertIds.push_back( i * 2 );
		vertIds.push_back( i * 2 + 1 );
		vertIds.push_back( i * 2 + 3 );
		vertIds.push_back( i * 2 + 2 );
		vertsPerPoly.push_back( 4 );
	}

	IECoreScene::MeshPrimitivePtr ring = new IECoreScene::MeshPrimitive( vertsPerPolyData, vertIdsData, "linear", pData );
	ToGLMeshConverterPtr converter = new ToGLMeshConverter( ring );
	result = runTimeCast<IECoreGL::MeshPrimitive>( converter->convert() );

	return result;
}

IECoreGL::MeshPrimitivePtr cone( float height, float startRadius, float endRadius )
{
	IECoreGL::MeshPrimitivePtr result;

	IntVectorDataPtr vertsPerPolyData = new IntVectorData;
	IntVectorDataPtr vertIdsData = new IntVectorData;
	V3fVectorDataPtr pData = new V3fVectorData;

	std::vector<int> &vertsPerPoly = vertsPerPolyData->writable();
	std::vector<int> &vertIds = vertIdsData->writable();
	std::vector<V3f> &p = pData->writable();

	const int numSegments = 20;
	for( int i = 0; i < numSegments + 1; ++i )
	{
		const float a = ( (float)i / (float)numSegments ) * 2.f * M_PI;

		p.push_back( V3f( -sin( a ) * startRadius, cos( a ) * startRadius, 0 ) );
		p.push_back( V3f( -sin( a ) * endRadius, cos( a ) * endRadius, height ) );  // Face the -Z axis
	}
	for( int i = 0; i < numSegments; ++i )
	{
		vertIds.push_back( i * 2 );
		vertIds.push_back( i * 2 + 1 );
		vertIds.push_back( i * 2 + 3 );
		vertIds.push_back( i * 2 + 2 );
		vertsPerPoly.push_back( 4 );
	}

	IECoreScene::MeshPrimitivePtr mesh = new IECoreScene::MeshPrimitive( vertsPerPolyData, vertIdsData, "linear", pData );
	IECoreGL::ToGLMeshConverterPtr converter = new ToGLMeshConverter( mesh );
	result = runTimeCast<IECoreGL::MeshPrimitive>( converter->convert() );

	return result;
}

const float g_tipScale = 10.f;
const float g_tipIconSize = 1.25f;
const float g_tipIconOffset = -0.25f;
const float g_tipIndent = 1.75f;
const float g_tipLineSpacing = -1.375f;

IECoreGL::MeshPrimitivePtr unitCone()
{
	static IECoreGL::MeshPrimitivePtr result = cone( 1.5f, 0.5f, 0 );
	return result;
}

GraphComponent *commonAncestor( std::vector<GraphComponent *> &graphComponents )
{
	const size_t gcSize = graphComponents.size();
	if( gcSize == 0 )
	{
		return nullptr;
	}
	if( gcSize == 1 )
	{
		return graphComponents[0];
	}

	GraphComponent *commonAncestor = graphComponents[0]->commonAncestor( graphComponents[1] );

	for( size_t i = 2; i < gcSize; ++i )
	{
		if( commonAncestor->isAncestorOf( graphComponents[i] ) )
		{
			continue;
		}
		commonAncestor = graphComponents[i]->commonAncestor( commonAncestor );
	}

	return commonAncestor;
}

void drawSelectionTips(
	const V3f &gadgetSpacePosition,
	std::vector<const Inspector::Result *> inspections,
	const std::string multiPlugDescription,
	const std::string infoSuffix,
	const Handle *handle,
	const ViewportGadget *viewport,
	const GafferUI::Style *style
)
{
	std::vector<GraphComponent *> parameterSources;
	std::vector<std::string> warningTips;
	for( const auto &inspection : inspections )
	{
		if( auto source = inspection->source() )
		{
			EditScope *editScope = inspection->editScope();
			if( !editScope || ( editScope && editScope->isAncestorOf( source ) ) )
			{
				parameterSources.push_back( source );
			}
			else
			{
				parameterSources.push_back( editScope );
			}

			if( inspection->editable() && !inspection->editWarning().empty() )
			{
				warningTips.push_back( inspection->editWarning() );
			}
			else if( !inspection->editable() )
			{
				warningTips.push_back( inspection->nonEditableReason() );
			}
		}
	}

	std::string parameterInfo;
	if( parameterSources.size() == 1 )
	{
		parameterInfo = fmt::format(
			"Editing : {}",
			parameterSources.front()->relativeName( parameterSources.front()->ancestor<ScriptNode>() )
		);
	}
	else if( parameterSources.size() > 1 )
	{
		GraphComponent *commonAncestor = ::commonAncestor( parameterSources );

		parameterInfo = fmt::format( "Editing {} {}", parameterSources.size(), multiPlugDescription );

		if( commonAncestor && (Gaffer::TypeId)commonAncestor->typeId() != Gaffer::TypeId::ScriptNodeTypeId )
		{
			parameterInfo += fmt::format(
				" on {}",
				commonAncestor->relativeName( commonAncestor->ancestor<ScriptNode>() )
			);
		}
	}

	std::string warningInfo;
	int warningSize = (int)warningTips.size();
	int warningLines = 0;
	for( int i = 0, eI = std::min( warningSize, g_warningTipCount ); i < eI; ++i )
	{
		warningInfo += warningTips[i] + ( i < eI -1 ? "\n" : "" );
		warningLines++;
	}
	if( warningSize == g_warningTipCount + 1 )
	{
		// May as well print the real warning instead of a mysterious "and 1 more"
		warningInfo += "\n" + warningTips[warningSize - 1];
		warningLines++;
	}
	if( warningSize > g_warningTipCount + 1 )
	{
		warningInfo += fmt::format( "\nand {} more", warningSize - g_warningTipCount );
		warningLines++;
	}

	ViewportGadget::RasterScope rasterScope( viewport );

	glPushAttrib( GL_DEPTH_BUFFER_BIT );

	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );

	glPushMatrix();

	const V2f rasterPosition = viewport->gadgetToRasterSpace( gadgetSpacePosition, handle );
	const Box3f infoBound = style->textBound( Style::TextType::BodyText, parameterInfo );
	const Box3f warningBound = style->textBound( Style::TextType::BodyText, warningInfo );

	const float maxWidth = std::max( infoBound.max.x, warningBound.max.x );

	const V2i screenBound = viewport->getViewport();

	const float x =
		(rasterPosition.x + 15.f ) -
		std::max( ( rasterPosition.x + 15.f + maxWidth * g_tipScale ) - ( screenBound.x - 45.f ), 0.f )
	;
	float y = rasterPosition.y + g_tipLineSpacing * g_tipScale;
	if( !warningInfo.empty() )
	{
		y += g_tipLineSpacing * g_tipScale;
	}
	if( !infoSuffix.empty() )
	{
		y += g_tipLineSpacing * g_tipScale;
	}

	glTranslate( V2f( x, y ) );
	glScalef( g_tipScale, -g_tipScale, g_tipScale );

	IECoreGL::ConstTexturePtr infoTexture = ImageGadget::loadTexture( "infoSmall.png" );
	glPushMatrix();
	glTranslate( V2f( 0, g_tipIconOffset ) );
	style->renderImage( Box2f( V2f( 0 ), V2f( g_tipIconSize ) ), infoTexture.get() );
	glPopMatrix();

	glPushMatrix();
	glTranslate( V2f( g_tipIndent, 0 ) );
	style->renderText( Style::TextType::BodyText, parameterInfo, Style::NormalState, &g_hoverTextColor );
	glPopMatrix();

	if( !warningInfo.empty() )
	{
		IECoreGL::ConstTexturePtr warningTexture = ImageGadget::loadTexture( "warningSmall.png" );
		glPushMatrix();
		glTranslate( V2f( 0, g_tipIconOffset) );
		for( int i = 0; i < warningLines; ++i )
		{
			glTranslate( V2f( 0, g_tipLineSpacing ) );
			style->renderImage( Box2f( V2f( 0 ), V2f( g_tipIconSize ) ), warningTexture.get() );
		}
		glPopMatrix();

		glPushMatrix();
		glTranslate( V2f( g_tipIndent, g_tipLineSpacing ) );
		style->renderText( Style::TextType::BodyText, warningInfo, Style::NormalState, &g_hoverTextColor );
		glPopMatrix();
	}
	if( !infoSuffix.empty() )
	{
		glTranslate( V2f( g_tipIndent, g_tipLineSpacing * ( warningLines + 1 ) ) );
		style->renderText( Style::TextType::BodyText, infoSuffix, Style::NormalState, &g_hoverTextColor );
	}

	glPopMatrix();
	glPopAttrib();
}

float sphereSpokeClickAngle( const Line3f &eventLine, float radius, float spokeAngle, float &newAngle )
{
	const float B = 2.f * ( eventLine.dir ^ eventLine.pos );
	const float C = ( eventLine.pos ^ eventLine.pos ) - ( radius * radius );

	const float discriminant = B * B - 4.f * C;
	// If discriminant is negative, the click is outside the sphere.
	if( discriminant < 0 )
	{
		return false;
	}

	// t = ( -B +/- sqrt( B^2 - 4AC ) ) / 2A ( A = 1 )
	const float sqRoot = std::sqrt( discriminant );

	const V3f minusP = eventLine( ( -B - sqRoot ) * 0.5f );
	const V3f plusP = eventLine( ( -B + sqRoot ) * 0.5f );

	if( minusP.z > 0 && plusP.z > 0 )
	{
		newAngle = 180.f;
		return true;
	}
	else if( minusP.z >= 0 && plusP.z < 0 )
	{
		newAngle = radiansToDegrees( atan2( -plusP.x, -plusP.z ) );
		return true;
	}
	else if( minusP.z < 0 && plusP.z >= 0 )
	{
		newAngle = radiansToDegrees( atan2( -minusP.x, -minusP.z ) );
		return true;
	}

	const M44f r = M44f().rotate( V3f( 0, degreesToRadians( spokeAngle ), 0 ) );
	const Line3f handleLine( V3f( 0 ), V3f( 0, 0, -radius ) * r );

	const V3f p = handleLine.distanceTo( minusP ) < handleLine.distanceTo( plusP ) ? minusP : plusP;
	newAngle = radiansToDegrees( atan2( -p.x, -p.z ) );

	return true;
}

// ============================================================================
// LightToolHandle
// ============================================================================

class LightToolHandle : public Handle
{

	public :

		LightToolHandle(
			const std::string &lightTypePattern,
			const std::string &name
		) :
			Handle( name ),
			m_lightTypePattern( lightTypePattern ),
			m_lookThroughLight( false )
		{

		}
		~LightToolHandle() override
		{

		}

		// Update inspectors and data needed to display and interact with the tool. Called
		// in `preRender()` if the inspections are dirty.
		// Derived classes should call this parent method first, then implement custom logic.
		virtual void update( ScenePathPtr scenePath, const PlugPtr &editScope )
		{
			m_handleScenePath = scenePath;
			m_editScope = editScope;
		}

		bool isLightType( const std::string &shaderAttribute ) const
		{
			auto lightType = Metadata::value<StringData>( shaderAttribute, "type" );

			if( !lightType || !StringAlgo::matchMultiple( lightType->readable(), m_lightTypePattern ) )
			{
				return false;
			}

			return true;
		}

		ScenePath *handleScenePath() const
		{
			return m_handleScenePath.get();
		}

		Plug *editScope() const
		{
			return m_editScope.get();
		}

		void setLookThroughLight( bool lookThroughLight )
		{
			m_lookThroughLight = lookThroughLight;
		}

		bool getLookThroughLight() const
		{
			return m_lookThroughLight;
		}

		// Must be implemented by derived classes to create inspections needed
		// by the handle. Called during `preRender()` if the inspections are dirty.
		virtual void addDragInspection() = 0;

		virtual void clearDragInspections() = 0;

		virtual bool handleDragMove( const GafferUI::DragDropEvent &event ) = 0;
		virtual bool handleDragEnd() = 0;

		// Must be implemented by derived classes to set the local transform of the handle
		// relative to the light. The parent of the handle will have rotation and translation
		// set independently. `scale` and `shear` are passed here to allow the handle to decide
		// how to deal with those transforms.
		virtual void updateLocalTransform( const V3f &scale, const V3f &shear ) = 0;

		// Must be implemented by derived classes to return the visible and enabled state for
		// the `scenePath` in the current context.
		virtual bool visible() const = 0;
		virtual bool enabled() const = 0;

		// Must be implemented by derived classes to return all of the inspectors the handle uses.
		virtual std::vector<GafferSceneUI::Private::Inspector *> inspectors() const = 0;

	private :

		ScenePathPtr m_handleScenePath;

		const std::string m_lightTypePattern;

		Gaffer::PlugPtr m_editScope;

		bool m_lookThroughLight;
};

// ============================================================================
// SpotLightHandle
// ============================================================================

class SpotLightHandle : public LightToolHandle
{

	private :

		// A struct holding the angle inspections and the original angles during a drag.
		// Angles are in "handle-space" (generally 1/2 the full cone for the cone angle
		// and the full penumbra angle for penumbras. See `handleAngles` and `plugAngles`
		// for conversion details.)
		struct DragStartData
		{
			Inspector::ResultPtr coneInspection;
			float originalConeHandleAngle;
			Inspector::ResultPtr penumbraInspection;
			std::optional<float> originalPenumbraHandleAngle;
		};

	public :

		enum class HandleType
		{
			Cone,
			Penumbra
		};

		SpotLightHandle(
			const std::string &lightType,
			HandleType handleType,
			const SceneView *view,
			const float zRotation,
			const std::string &name = "SpotLightHandle"
		) :
			LightToolHandle( lightType, name ),
			m_view( view ),
			m_zRotation( zRotation ),
			m_handleType( handleType ),
			m_angleMultiplier( 1.f ),
			m_visualiserScale( 1.f ),
			m_frustumScale( 1.f ),
			m_lensRadius( 0 ),
			m_dragStartData {}
		{

			mouseMoveSignal().connect( boost::bind( &SpotLightHandle::mouseMove, this, ::_2 ) );

		}
		~SpotLightHandle() override
		{

		}

		void update( ScenePathPtr scenePath, const PlugPtr &editScope ) override
		{
			LightToolHandle::update( scenePath, editScope );

			if( !handleScenePath()->isValid() )
			{
				m_coneAngleInspector.reset();
				m_penumbraAngleInspector.reset();
				return;
			}

			ConstCompoundObjectPtr attributes = handleScenePath()->getScene()->fullAttributes( handleScenePath()->names() );

			float defaultVisualiserScale = 1.f;
			if( auto p = m_view->descendant<const FloatPlug>( "drawingMode.visualiser.scale" ) )
			{
				defaultVisualiserScale = p->getValue();
			}
			auto visualiserScaleData = attributes->member<FloatData>( g_lightVisualiserScaleAttributeName );
			m_visualiserScale = visualiserScaleData ? visualiserScaleData->readable() : defaultVisualiserScale;

			float defaultFrustumScale = 1.f;
			if( auto p = m_view->descendant<const FloatPlug>( "drawingMode.light.frustumScale" ) )
			{
				defaultFrustumScale = p->getValue();
			}
			auto frustumScaleData = attributes->member<FloatData>( g_frustumScaleAttributeName );
			m_frustumScale = frustumScaleData ? frustumScaleData->readable() : defaultFrustumScale;

			/// \todo This can be simplified and some of the logic, especially getting the inspectors, can
			/// be moved to the constructor when we standardize on a single USDLux light representation.

			for( const auto &[attributeName, value] : attributes->members() )
			{
				if(
					StringAlgo::match( attributeName, g_lightAttributePattern ) &&
					value->typeId() == (IECore::TypeId)ShaderNetworkTypeId
				)
				{
					const auto shader = attributes->member<ShaderNetwork>( attributeName )->outputShader();
					std::string shaderAttribute = shader->getType() + ":" + shader->getName();

					if( !isLightType( shaderAttribute ) )
					{
						continue;
					}

					auto coneParameterName = Metadata::value<StringData>( shaderAttribute, "coneAngleParameter" );
					if( !coneParameterName )
					{
						continue;
					}

					m_coneAngleInspector = new ParameterInspector(
						handleScenePath()->getScene(),
						this->editScope(),
						attributeName,
						ShaderNetwork::Parameter( "", coneParameterName->readable() )
					);

					auto penumbraTypeData = Metadata::value<StringData>( shaderAttribute, "penumbraType" );
					m_penumbraType = penumbraTypeData ? std::optional<InternedString>( InternedString( penumbraTypeData->readable() ) ) : std::nullopt;

					m_penumbraAngleInspector.reset();
					if( auto penumbraParameterName = Metadata::value<StringData>( shaderAttribute, "penumbraAngleParameter" ) )
					{
						m_penumbraAngleInspector = new ParameterInspector(
							handleScenePath()->getScene(),
							this->editScope(),
							attributeName,
							ShaderNetwork::Parameter( "", penumbraParameterName->readable() )
						);
					}

					m_lensRadius = 0;
					if( auto lensRadiusParameterName = Metadata::value<StringData>( shaderAttribute, "lensRadiusParameter" ) )
					{
						if( auto lensRadiusData = shader->parametersData()->member<FloatData>( lensRadiusParameterName->readable() ) )
						{
							m_lensRadius = lensRadiusData->readable();
						}
					}

					auto angleType = Metadata::value<StringData>( shaderAttribute, "coneAngleType" );
					if( angleType && angleType->readable() == "half" )
					{
						m_angleMultiplier = 2.f;
					}
					else
					{
						m_angleMultiplier = 1.f;
					}

					break;
				}
			}
		}

		void addDragInspection() override
		{
			Inspector::ResultPtr coneAngleInspection = m_coneAngleInspector->inspect();
			if( !coneAngleInspection )
			{
				return;
			}
			Inspector::ResultPtr penumbraAngleInspection = m_penumbraAngleInspector ? m_penumbraAngleInspector->inspect() : nullptr;

			ConstFloatDataPtr originalConeAngleData = runTimeCast<const IECore::FloatData>( coneAngleInspection->value() );
			if( !originalConeAngleData )
			{
				return;
			}

			ConstFloatDataPtr originalPenumbraAngleData;
			if( penumbraAngleInspection )
			{
				originalPenumbraAngleData = runTimeCast<const IECore::FloatData>( penumbraAngleInspection->value() );
				assert( originalPenumbraAngleData );
			}

			const auto &[coneHandleAngle, penumbraHandleAngle] = handleAngles(
				originalConeAngleData.get(),
				originalPenumbraAngleData ? originalPenumbraAngleData.get() : nullptr
			);

			m_inspections.push_back(
				{
					coneAngleInspection,
					coneHandleAngle,
					penumbraAngleInspection,
					penumbraHandleAngle
				}
			);
		}

		void clearDragInspections() override
		{
			m_inspections.clear();
		}

		bool handleDragMove( const GafferUI::DragDropEvent &event ) override
		{
			if( m_inspections.empty() || !allInspectionsEnabled() )
			{
				return true;
			}

			float newHandleAngle = 0;
			if( getLookThroughLight() )
			{
				// When looking through a light, the viewport field of view changes
				// with the cone angle. When dragging, taking just the `event` coordinates
				// causes a feedback loop where the `event` coordinates as a fraction of
				// the viewport cause the viewport to get smaller / larger, which causes the fraction
				// to get smaller / larger, quickly going to zero / 180.
				// We can avoid the feedback loop by using raster coordinates, which unproject
				// the local coordinates to a fixed frame of reference (the screen).
				const Line3f dragLine( event.line.p0, event.line.p1 );

				newHandleAngle = radiansToDegrees(
					atan2( rasterDragDistance( dragLine ) + m_rasterXOffset, m_rasterZPosition )
				);
			}
			else if( m_drag.value().isLinearDrag() )
			{
				// Intersect the gadget-local `event` line with the sphere centered at the gadget
				// origin with radius equal to the distance along the handle where the user clicked.
				// `Imath::Sphere3::intersect()` returns the closest (if any) intersection, but we
				// want the intersection closest to the handle line, so we do the calculation here.

				const Line3f eventLine( event.line.p0, event.line.p1 );

				const auto &[coneInspection, coneHandleAngle, penumbraInspection, penumbraHandleAngle] = spotLightHandleAngles();
				const float angle = m_handleType == HandleType::Cone ? coneHandleAngle : penumbraHandleAngle.value();

				if( !sphereSpokeClickAngle( eventLine, m_arcRadius, angle, newHandleAngle ) )
				{
					return true;
				}
			}
			else
			{
				// All other drags can use the `AngularDrag` directly.
				newHandleAngle = radiansToDegrees( m_drag.value().updatedRotation( event ) );
			}

			// Clamp the handle being dragged, then calculate the angle delta.

			const float clampedHandleAngle = clampHandleAngle(
				newHandleAngle,
				m_dragStartData.originalConeHandleAngle,
				m_dragStartData.originalPenumbraHandleAngle
			);
			const float angleDelta = clampedHandleAngle - (
				m_handleType == HandleType::Cone ? m_dragStartData.originalConeHandleAngle : m_dragStartData.originalPenumbraHandleAngle.value()
			);

			for( auto &[coneInspection, originalConeHandleAngle, penumbraInspection, originalPenumbraHandleAngle] : m_inspections )
			{
				if( m_handleType == HandleType::Cone )
				{
					ValuePlugPtr conePlug = coneInspection->acquireEdit();
					auto coneFloatPlug = runTimeCast<FloatPlug>( activeValuePlug( conePlug.get() ) );
					if( !coneFloatPlug )
					{
						throw Exception( "Invalid type for \"coneAngleParameter\"" );
					}

					// Clamp each individual cone angle as well
					setValueOrAddKey(
						coneFloatPlug,
						m_view->getContext()->getTime(),
						conePlugAngle(
							clampHandleAngle(
								originalConeHandleAngle + angleDelta,
								originalConeHandleAngle,
								originalPenumbraHandleAngle
							)
						)
					);
				}

				if( m_handleType == HandleType::Penumbra )
				{
					ValuePlugPtr penumbraPlug = penumbraInspection->acquireEdit();
					auto penumbraFloatPlug = runTimeCast<FloatPlug>( activeValuePlug( penumbraPlug.get() ) );
					if( !penumbraFloatPlug )
					{
						throw Exception( "Inavlid type for \"penumbraAngleParameter\"" );
					}

					// Clamp each individual cone angle as well
					setValueOrAddKey(
						penumbraFloatPlug,
						m_view->getContext()->getTime(),
						penumbraPlugAngle(
							clampHandleAngle(
								originalPenumbraHandleAngle.value() + angleDelta,
								originalConeHandleAngle,
								originalPenumbraHandleAngle
							)
						)
					);
				}
			}

			return true;
		}

		bool handleDragEnd() override
		{
			m_drag = std::nullopt;

			return false;
		}

		void updateLocalTransform( const V3f &, const V3f & ) override
		{
			M44f transform;
			if( m_handleType == HandleType::Penumbra && ( !m_penumbraType || m_penumbraType == g_insetPenumbraType ) )
			{
				// Rotate 180 on the Z-axis to make positive rotations inset
				transform *= M44f().rotate( V3f( 0, 0, M_PI ) );
			}

			if( m_handleType == HandleType::Penumbra )
			{
				// For inset and outset penumbras, transform the handle so the -Z axis
				// points along the cone line, making all angles relative to the cone angle.
				const auto &[coneInspection, coneHandleAngle, penumbraInspection, penumbraHandleAngle] = spotLightHandleAngles();
				if( !m_penumbraType || m_penumbraType == g_insetPenumbraType || m_penumbraType == g_outsetPenumbraType )
				{
					transform *= M44f().rotate( V3f( 0, degreesToRadians( coneHandleAngle ), 0 ) );
				}
			}

			transform *= M44f().translate( V3f( -m_lensRadius, 0, 0 ) );
			transform *= M44f().rotate( V3f( 0, 0, degreesToRadians( m_zRotation ) ) );

			setTransform( transform );
		}

		bool visible() const override
		{
			if( !m_coneAngleInspector || ( m_handleType == HandleType::Penumbra && !m_penumbraAngleInspector ) )
			{
				return false;
			}

			// We can be called to check visibility for any scene location set in the current context, spot light
			// or otherwise. If there isn't an inspection, this handle should be hidden (likely because the scene
			// location is not a spot light).

			Inspector::ResultPtr contextConeInspection = m_coneAngleInspector->inspect();
			Inspector::ResultPtr contextPenumbraInspection = m_penumbraAngleInspector ? m_penumbraAngleInspector->inspect() : nullptr;

			if( !contextConeInspection || ( m_handleType == HandleType::Penumbra && !contextPenumbraInspection ) )
			{
				return false;
			}

			// We are a spot light, but the penumbra will be hidden if it's too close to the cone angle, for
			// the location we're attaching the handles to.

			/// \todo This checks the penumbra / cone angles only for the last selected location, causing
			/// repeated checks of the same location when `visible()` is called in a loop over multiple scene
			/// locations. We rely on history caching to make this relatively fast, but ideally this could be
			/// tested only once per selection list.

			const auto &[coneInspection, coneAngle, penumbraInspection, penumbraAngle] = spotLightHandleAngles();
			if( m_handleType == HandleType::Penumbra && penumbraAngle )
			{
				const float radius = m_visualiserScale * m_frustumScale * -10.f;
				const V2f coneRaster = m_view->viewportGadget()->gadgetToRasterSpace(
					V3f( 0, 0, radius ),
					this
				);
				const M44f rot = M44f().rotate( V3f( 0, degreesToRadians( penumbraAngle.value() ), 0 ) );
				const V2f penumbraRaster = m_view->viewportGadget()->gadgetToRasterSpace(
					V3f( 0, 0, radius ) * rot,
					this
				);

				if( ( coneRaster - penumbraRaster ).length() < ( 2.f * g_circleHandleWidthLarge ) )
				{
					return false;
				}
			}

			return true;
		}

		bool enabled() const override
		{
			if( !m_coneAngleInspector )
			{
				return false;
			}

			// Return true without checking the `enabled()` state of our inspections.
			// This allows the tooltip-on-highlight behavior to show a tooltip explaining
			// why an edit is not possible. The alternative is to draw the tooltip for all
			// handles regardless of mouse position because a handle can only be in a disabled
			// or highlighted drawing state.
			// The drawing code takes care of graying out uneditable handles and the inspections
			// prevent the value from being changed.

			return true;
		}

		std::vector<GafferSceneUI::Private::Inspector *> inspectors() const override
		{
			if( m_handleType == HandleType::Cone )
			{
				return {m_coneAngleInspector.get()};
			}
			if(
				(
					!m_penumbraType ||
					m_penumbraType == g_insetPenumbraType ||
					m_penumbraType == g_outsetPenumbraType
				)
			)
			{
				return {m_coneAngleInspector.get(), m_penumbraAngleInspector.get()};
			}
			return {m_penumbraAngleInspector.get()};
		}

	protected :

		void renderHandle( const Style *style, Style::State state ) const override
		{
			State::bindBaseState();
			auto glState = const_cast<State *>( State::defaultState() );

			IECoreGL::GroupPtr group = new IECoreGL::Group;

			const bool highlighted = state == Style::State::HighlightedState;

			// Line along cone. Use a cylinder because GL_LINE with width > 1
			// are not reliably selected.

			GroupPtr spokeGroup = new Group;

			spokeGroup->getState()->add(
				new IECoreGL::ShaderStateComponent(
					ShaderLoader::defaultShaderLoader(),
					TextureLoader::defaultTextureLoader(),
					"",
					"",
					constantFragSource(),
					new CompoundObject
				)
			);

			float spokeRadius = 0;
			float handleRadius = 0;

			if( IECoreGL::Selector::currentSelector() )
			{
				spokeRadius = g_lineSelectionWidth;
				handleRadius = g_circleHandleSelectionWidth;
			}
			else
			{
				spokeRadius = m_handleType == HandleType::Cone ? (
					highlighted ? g_lineHandleWidthLarge : g_lineHandleWidth
				) : (
					highlighted ? g_minorLineHandleWidthLarge : g_minorLineHandleWidth
				);

				handleRadius = highlighted ? g_circleHandleWidthLarge : g_circleHandleWidth;
			}

			const V3f farP = V3f( 0, 0, m_frustumScale * m_visualiserScale * -10.f );
			const auto &[coneInspection, coneHandleAngle, penumbraInspection, penumbraHandleAngle] = spotLightHandleAngles();
			const float angle = m_handleType == HandleType::Cone ? coneHandleAngle : penumbraHandleAngle.value();

			const M44f handleTransform = M44f().rotate( V3f( 0, degreesToRadians( angle ), 0 ) );

			spokeGroup->addChild(
				cone(
					m_visualiserScale * m_frustumScale * -10.f,
					spokeRadius * ::rasterScaleFactor( this, V3f( 0 ) ),
					spokeRadius * ::rasterScaleFactor( this, farP * handleTransform )
				)
			);

			auto standardStyle = runTimeCast<const StandardStyle>( style );
			assert( standardStyle );
			const Color3f highlightColor3 = standardStyle->getColor( StandardStyle::Color::HighlightColor );
			const Color4f highlightColor4 = Color4f( highlightColor3.x, highlightColor3.y, highlightColor3.z, 1.f );

			const bool enabled = allInspectionsEnabled();

			spokeGroup->getState()->add(
				new IECoreGL::Color(
					enabled ? ( highlighted ? g_lightToolHighlightColor4 : highlightColor4 ) : g_lightToolDisabledColor4
				)
			);

			group->addChild( spokeGroup );

			// Circles at end of cone and frustum

			IECoreGL::GroupPtr iconGroup = new IECoreGL::Group;

			iconGroup->getState()->add(
				new IECoreGL::ShaderStateComponent(
					ShaderLoader::defaultShaderLoader(),
					TextureLoader::defaultTextureLoader(),
					faceCameraVertexSource(),
					"",
					constantFragSource(),
					new CompoundObject
				)
			);

			IECoreGL::MeshPrimitivePtr decoration;
			if(
				( m_handleType == HandleType::Cone && m_penumbraAngleInspector && ( !m_penumbraType || m_penumbraType == g_insetPenumbraType ) ) ||
				( m_handleType == HandleType::Penumbra && ( m_penumbraType == g_outsetPenumbraType || m_penumbraType == g_absolutePenumbraType ) )
			)
			{
				decoration = ring();
			}
			else
			{
				decoration = circle();
			}

			IECoreGL::GroupPtr nearIconGroup = new IECoreGL::Group;
			nearIconGroup->addChild( decoration );

			const V3f nearP = V3f( 0, 0, -m_visualiserScale );

			nearIconGroup->setTransform(
				M44f().scale( V3f( handleRadius ) * ::rasterScaleFactor( this, nearP * handleTransform ) ) *
				M44f().translate( nearP )
			);
			iconGroup->addChild( nearIconGroup );

			IECoreGL::GroupPtr farIconGroup = new IECoreGL::Group;
			const float farRasterScaleFactor = ::rasterScaleFactor( this, farP * handleTransform );
			farIconGroup->addChild( decoration );
			farIconGroup->setTransform(
				M44f().scale( V3f( handleRadius ) * farRasterScaleFactor ) *
				M44f().translate(  farP + V3f( 0, 0, -handleRadius * farRasterScaleFactor ) )
			);
			iconGroup->addChild( farIconGroup );

			iconGroup->getState()->add(
				new IECoreGL::Color(
					enabled ? ( highlighted ? g_lightToolHighlightColor4 : highlightColor4 ) : g_lightToolDisabledColor4
				)
			);

			group->addChild( iconGroup );

			// Drag arcs

			if( m_drag && !getLookThroughLight() )
			{
				const float currentFraction = angle / 360.f;
				const float previousFraction = !m_inspections.empty() ?
					( m_handleType == HandleType::Cone ?
						m_dragStartData.originalConeHandleAngle :
						m_dragStartData.originalPenumbraHandleAngle.value()
					) / 360.f : currentFraction;

				IECoreScene::MeshPrimitivePtr previousSolidArc = nullptr;
				IECoreScene::MeshPrimitivePtr currentSolidArc = nullptr;

				const Color3f previousColor = g_lightToolHandleColor * g_highlightMultiplier;
				const Color3f currentColor = g_lightToolHandleColor;

				const float arcWidth = g_dragArcWidth * ::rasterScaleFactor( this, V3f( 0, 0, -m_arcRadius ) );
				previousSolidArc = solidArc(
					std::min( -m_arcRadius + arcWidth * 1.5f, 0.f ),
					std::min( -m_arcRadius + arcWidth, 0.f ),
					previousFraction - currentFraction,
					-currentFraction,
					previousColor
				);
				currentSolidArc = solidArc(
					std::min( -m_arcRadius, 0.f ),
					std::min( -m_arcRadius + arcWidth, 0.f ),
					0,
					-currentFraction,
					currentColor
				);

				IECoreGL::GroupPtr solidAngleGroup = new IECoreGL::Group;
				solidAngleGroup->getState()->add(
					new IECoreGL::ShaderStateComponent(
						ShaderLoader::defaultShaderLoader(),
						TextureLoader::defaultTextureLoader(),
						"",  // vertexSource
						"",  // geometrySource
						translucentConstantFragSource(),
						new CompoundObject
					)
				);

				if( currentSolidArc )
				{
					ToGLMeshConverterPtr meshConverter = new ToGLMeshConverter( currentSolidArc );
					solidAngleGroup->addChild( runTimeCast<IECoreGL::Renderable>( meshConverter->convert() ) );
				}
				if( previousSolidArc )
				{
					ToGLMeshConverterPtr meshConverter = new ToGLMeshConverter( previousSolidArc );
					solidAngleGroup->addChild( runTimeCast<IECoreGL::Renderable>( meshConverter->convert() ) );
				}

				group->addChild( solidAngleGroup );
			}

			group->setTransform( handleTransform );

			group->render( glState );

			// Selection info

			if( highlighted )
			{
				std::vector<const Inspector::Result *> inspections;
				for( const auto &inspectionPair : m_inspections )
				{
					inspections.push_back(
						m_handleType == HandleType::Cone ? inspectionPair.coneInspection.get() :
						inspectionPair.penumbraInspection.get()
					);
				}

				drawSelectionTips(
					V3f( 0, 0, !getLookThroughLight() ? -m_arcRadius : 1.f ) * handleTransform,
					inspections,
					fmt::format( "{} angles", m_handleType == HandleType::Cone ? "cone" : "penumbra" ),
					"",  // infoSuffix
					this,
					m_view->viewportGadget(),
					style
				);
			}
		}

	private :

		bool mouseMove( const ButtonEvent &event )
		{
			if( m_drag || !m_coneAngleInspector || handleScenePath()->isEmpty() )
			{
				return false;
			}

			const auto &[coneInspection, coneHandleAngle, penumbraInspection, penumbraHandleAngle] = spotLightHandleAngles();

			const float angle = m_handleType == HandleType::Cone ? coneHandleAngle : penumbraHandleAngle.value();

			const M44f r = M44f().rotate( V3f( 0, degreesToRadians( angle ), 0 ) );
			const Line3f rayLine(
				V3f( 0 ),
				V3f( 0, 0, m_visualiserScale * m_frustumScale * -10.f ) * r
			);
			const V3f dragPoint = rayLine.closestPointTo( Line3f( event.line.p0, event.line.p1 ) );
			m_arcRadius = dragPoint.length();

			dirty( DirtyType::Render );

			return false;
		}

		void dragBegin( const DragDropEvent &event ) override
		{
			const auto &[ coneInspection, coneHandleAngle, penumbraInspection, penumbraHandleAngle] = spotLightHandleAngles();

			m_dragStartData.coneInspection = coneInspection;
			m_dragStartData.originalConeHandleAngle = coneHandleAngle;
			m_dragStartData.penumbraInspection = penumbraInspection;
			m_dragStartData.originalPenumbraHandleAngle = penumbraHandleAngle;

			m_drag = AngularDrag(
				this,
				V3f( 0, 0, 0 ),
				V3f( 0, 1.f, 0 ),
				V3f( 0, 0, -1.f ),
				event
			);

			if( getLookThroughLight() )
			{
				const float dragStartAngle = m_handleType == HandleType::Cone ? coneHandleAngle : penumbraHandleAngle.value();

				const Line3f clickLine( event.line.p0, event.line.p1 );
				const Line3f originLine( V3f( 0 ), V3f( 0, 0, -1.f ) );
				const Line3f handleLine(
					V3f( 0 ),
					V3f( 0, 0, -1.f ) * M44f().rotate( V3f( 0, degreesToRadians( dragStartAngle ), 0 ) )
				);

				const float clickRaster = rasterDragDistance( clickLine );
				const float originRaster = rasterDragDistance( originLine );
				const float handleRaster = rasterDragDistance( handleLine );

				const float delta = handleRaster - originRaster;

				m_rasterXOffset = delta - clickRaster;
				m_rasterZPosition = abs( delta ) / tan( degreesToRadians( dragStartAngle ) );
			}
		}

		DragStartData spotLightHandleAngles() const
		{
			ScenePlug::PathScope pathScope( handleScenePath()->getContext() );
			pathScope.setPath( &handleScenePath()->names() );

			Inspector::ResultPtr coneInspection = m_coneAngleInspector->inspect();
			if( !coneInspection )
			{
				return {nullptr, 0, nullptr, std::nullopt};
			}

			const FloatData *coneAngleData = runTimeCast<const FloatData>( coneInspection->value() );
			if( !coneAngleData )
			{
				return {nullptr, 0, nullptr, std::nullopt};
			}

			const FloatData *penumbraAngleData = nullptr;

			Inspector::ResultPtr penumbraInspection = m_penumbraAngleInspector ? m_penumbraAngleInspector->inspect() : nullptr;
			if( penumbraInspection )
			{
				penumbraAngleData = runTimeCast<const FloatData>( penumbraInspection->value() );
				assert( penumbraAngleData );
			}

			const auto &[coneAngle, penumbraAngle] = handleAngles( coneAngleData, penumbraAngleData );

			return {coneInspection, coneAngle, penumbraInspection, penumbraAngle};
		}

		// Convert from the angle representation used by plugs to that used by handles.
		std::pair<float, std::optional<float>> handleAngles( const FloatData *coneAngleData, const FloatData *penumbraAngleData ) const
		{
			std::optional<float> penumbraAngle = std::nullopt;
			if( penumbraAngleData )
			{
				if( m_penumbraType != g_absolutePenumbraType )
				{
					penumbraAngle = penumbraAngleData->readable();
				}
				else
				{
					penumbraAngle = penumbraAngleData->readable() * 0.5f;
				}
			}
			return {coneAngleData->readable() * 0.5f * m_angleMultiplier, penumbraAngle};
		}

		float conePlugAngle(const float a ) const
		{
			return a * 2.f / m_angleMultiplier;
		}

		float penumbraPlugAngle(const float a ) const
		{
			return ( m_penumbraType != g_absolutePenumbraType ? a : a * 2.f );
		}

		// Find the intersection of a line in gadget space with a unit sphere and
		// project that intersection to the handle's plane in raster space. Return
		// the projected point's distance from the raster center.
		float rasterDragDistance( const Line3f &ray )
		{
			V3f sphereIntersection;
			Sphere3f( V3f( 0 ), 1.f ).intersect( ray, sphereIntersection );

			const V2f gadgetRasterOrigin = m_view->viewportGadget()->gadgetToRasterSpace( V3f( 0, 0, -1.f ), this );
			const V2f rasterSphereIntersection = m_view->viewportGadget()->gadgetToRasterSpace( sphereIntersection, this );
			const V2f rasterNormal = ( m_view->viewportGadget()->gadgetToRasterSpace( V3f( 0, 1.f, -1.f ), this ) - gadgetRasterOrigin ).normalized();

			const V2f projectedPoint = rasterSphereIntersection - (rasterSphereIntersection - gadgetRasterOrigin).dot( rasterNormal ) * rasterNormal;

			const V2f rasterDistance = gadgetRasterOrigin - projectedPoint;

			// Flip the signs to account for the viewport origin, ensuring the returned value
			// is positive. We could take the absolute value, but that would cause drags to mirror
			// about the raster center. Instead we want to clamp at zero.
			if( abs( rasterDistance.x ) > abs( rasterDistance.y ) )
			{
				return rasterNormal.y < 0 ? rasterDistance.x : -rasterDistance.x;
			}
			return rasterNormal.x > 0 ? rasterDistance.y : -rasterDistance.y;
		}

		float clampHandleAngle(
			const float angle,
			const float originalConeAngle,
			const std::optional<float> originalPenumbraAngle
		)
		{
			float result = std::clamp( angle, 0.f, 90.f );
			if( m_handleType == HandleType::Cone )
			{
				if( originalPenumbraAngle && ( !m_penumbraType || m_penumbraType == g_insetPenumbraType ) )
				{
					result = std::max( result, originalPenumbraAngle.value() );
				}
				else if( m_penumbraType == g_outsetPenumbraType )
				{
					result = std::min( result, 90.f - originalPenumbraAngle.value() );
				}
			}

			else
			{
				if( !m_penumbraType || m_penumbraType == g_insetPenumbraType )
				{
					result = std::min( result, originalConeAngle );
				}
				else if( m_penumbraType == g_outsetPenumbraType )
				{
					result = std::min( result, 90.f - originalConeAngle );
				}
			}
			return result;
		}

		bool allInspectionsEnabled() const
		{
			bool enabled = true;
			for( auto &[coneInspection, originalConeAngle, penumbraInspection, originalPenumbraAngle] : m_inspections )
			{
				if( m_handleType == HandleType::Cone )
				{
					enabled &= coneInspection ? coneInspection->editable() : false;
				}
				else
				{
					enabled &= penumbraInspection ? penumbraInspection->editable() : false;
				}
			}

			return enabled;
		}

		ParameterInspectorPtr m_coneAngleInspector;
		ParameterInspectorPtr m_penumbraAngleInspector;

		const SceneView *m_view;

		const float m_zRotation;

		std::vector<DragStartData> m_inspections;

		std::optional<AngularDrag> m_drag;

		HandleType m_handleType;
		std::optional<InternedString> m_penumbraType;

		float m_angleMultiplier;

		float m_visualiserScale;
		float m_frustumScale;
		float m_lensRadius;

		DragStartData m_dragStartData;
		// The reference coordinates of the start of a drag
		// when looking through a light. `x` is the x distance, in raster
		// space, on the plane of the gadget. `y` is the depth, into the
		// screen, calculated as if it was in raster space.
		V2f m_lookThroughRasterReference;
		float m_rasterXOffset;
		float m_rasterZPosition;
		float m_arcRadius;
};

// ============================================================================
// WidthHeightHandle
// ============================================================================

class WidthHeightHandle : public LightToolHandle
{
	public :

		enum HandleType
		{
			Width = 1,
			Height = 2
		};

		WidthHeightHandle(
			const std::string &lightType,
			unsigned handleType,
			const SceneView *view,
			const InternedString widthParameter,
			const InternedString heightParameter,
			const float xSign,
			const float ySign,
			const std::string &name = "WidthHeightHandle"
		) :
			LightToolHandle( lightType, name ),
			m_view( view ),
			m_widthParameter( widthParameter ),
			m_heightParameter( heightParameter ),
			m_handleType( handleType ),
			m_dragStartInfo(),
			m_xSign( xSign ),
			m_ySign( ySign ),
			m_edgeCursorPoint( V3f( 0 ) ),
			m_scale( V2f( 1.f ) )
		{
			mouseMoveSignal().connect( boost::bind( &WidthHeightHandle::mouseMove, this, ::_2 ) );
		}

		~WidthHeightHandle() override
		{

		}

		void update( ScenePathPtr scenePath, const PlugPtr &editScope ) override
		{
			LightToolHandle::update( scenePath, editScope );

			m_widthInspector.reset();
			m_heightInspector.reset();

			if( !handleScenePath()->isValid() )
			{
				return;
			}

			/// \todo This can be simplified and some of the logic, especially getting the inspectors, can
			/// be moved to the constructor when we standardize on a single USDLux light representation.

			ConstCompoundObjectPtr attributes = handleScenePath()->getScene()->fullAttributes( handleScenePath()->names() );

			for( const auto &[attributeName, value ] : attributes->members() )
			{
				if(
					StringAlgo::match( attributeName, g_lightAttributePattern ) &&
					value->typeId() == (IECore::TypeId)ShaderNetworkTypeId
				)
				{
					const auto shader = attributes->member<ShaderNetwork>( attributeName )->outputShader();
					std::string shaderAttribute = shader->getType() + ":" + shader->getName();

					if( !isLightType( shaderAttribute ) )
					{
						continue;
					}

					auto widthParameterName = Metadata::value<StringData>( shaderAttribute, m_widthParameter );
					auto heightParameterName = Metadata::value<StringData>( shaderAttribute, m_heightParameter );
					if( !widthParameterName || !heightParameterName )
					{
						continue;
					}

					m_widthInspector = new ParameterInspector(
						handleScenePath()->getScene(),
						this->editScope(),
						attributeName,
						ShaderNetwork::Parameter( "", widthParameterName->readable() )
					);
					m_heightInspector = new ParameterInspector(
						handleScenePath()->getScene(),
						this->editScope(),
						attributeName,
						ShaderNetwork::Parameter( "", heightParameterName->readable() )
					);

					break;
				}
			}
		}

		void addDragInspection() override
		{
			InspectionInfo i = inspectionInfo();
			const auto &[widthInspection, originalWidth, heightInspection, originalHeight] = i;
			if( !widthInspection || !heightInspection )
			{
				return;
			}

			m_inspections.push_back( i );
		}

		void clearDragInspections() override
		{
			m_inspections.clear();
		}

		bool handleDragMove( const GafferUI::DragDropEvent &event ) override
		{
			if( m_inspections.empty() || !allInspectionsEnabled() )
			{
				return true;
			}

			float xMult = 1.f;
			float yMult = 1.f;

			float nonZeroWidth = m_dragStartInfo.originalWidth == 0 ? 1.f : m_dragStartInfo.originalWidth;
			float nonZeroHeight = m_dragStartInfo.originalHeight == 0 ? 1.f : m_dragStartInfo.originalHeight;

			if( m_handleType & HandleType::Width && m_handleType & HandleType::Height )
			{
				auto &drag = std::get<PlanarDrag>( m_drag );
				V2f newPosition = drag.updatedPosition( event ) - drag.startPosition();
				xMult = ( newPosition.x * 2.f ) / ( nonZeroWidth * m_scale.x ) + 1.f;
				yMult = ( newPosition.y * 2.f ) / ( nonZeroHeight * m_scale.y ) + 1.f;
			}
			else if( m_handleType & HandleType::Width )
			{
				auto &drag = std::get<LinearDrag>( m_drag );
				float newPosition = drag.updatedPosition( event ) - drag.startPosition();
				xMult = ( newPosition * 2.f ) / ( nonZeroWidth * m_scale.x ) + 1.f;
			}
			else if( m_handleType &HandleType::Height )
			{
				auto &drag = std::get<LinearDrag>( m_drag );
				float newPosition = drag.updatedPosition( event ) - drag.startPosition();
				yMult = ( newPosition * 2.f ) / ( nonZeroHeight * m_scale.y ) + 1.f;
			}

			if(
				event.modifiers == g_quadLightConstrainAspectRatioKey &&
				m_handleType & HandleType::Width &&
				m_handleType & HandleType::Height
			)
			{
				if( m_dragStartInfo.originalWidth > m_dragStartInfo.originalHeight )
				{
					yMult = xMult;
				}
				else
				{
					xMult = yMult;
				}
			}

			xMult = std::max( xMult, 0.f );
			yMult = std::max( yMult, 0.f );

			for( auto &[widthInspection, originalWidth, heightInspection, originalHeight] : m_inspections )
			{
				nonZeroWidth = originalWidth == 0 ? 1.f : originalWidth;
				nonZeroHeight = originalHeight == 0 ? 1.f : originalHeight;

				if( m_handleType & HandleType::Width && widthInspection && widthInspection->editable() )
				{
					ValuePlugPtr widthPlug = widthInspection->acquireEdit();
					auto widthFloatPlug = runTimeCast<FloatPlug>( activeValuePlug( widthPlug.get() ) );
					if( !widthFloatPlug )
					{
						throw Exception( "Invalid type of \"widthParameter\"" );
					}

					setValueOrAddKey(
						widthFloatPlug,
						m_view->getContext()->getTime(),
						nonZeroWidth * xMult
					);
				}

				if( m_handleType & HandleType::Height && heightInspection && heightInspection->editable() )
				{
					ValuePlugPtr heightPlug = heightInspection->acquireEdit();
					auto heightFloatPlug = runTimeCast<FloatPlug>( activeValuePlug( heightPlug.get() ) );
					if( !heightFloatPlug )
					{
						throw Exception( "Invalid type of \"heightParameter\"" );
					}

					setValueOrAddKey(
						heightFloatPlug,
						m_view->getContext()->getTime(),
						nonZeroHeight * yMult
					);
				}
			}

			return true;
		}

		bool handleDragEnd() override
		{
			m_drag = std::monostate{};
			return false;
		}

		void updateLocalTransform( const V3f &scale, const V3f & ) override
		{
			// Translate the handle to the center of the appropriate edge or corner.
			const auto &[widthInspection, originalWidth, heightInspection, originalHeight] = handleInspections();
			m_scale = V2f( scale.x, scale.y );

			M44f transform;
			if( m_handleType & HandleType::Width )
			{
				transform *= M44f().translate( V3f( originalWidth * 0.5f * m_xSign * m_scale.x, 0, 0 ) );
			}
			if( m_handleType & HandleType::Height )
			{
				transform *= M44f().translate( V3f( 0, originalHeight * 0.5f * m_ySign * m_scale.y, 0 ) );
			}

			setTransform( transform );
		}

		bool visible() const override
		{
			// We require both width and height to be present to be a valid quad light
			if( !m_widthInspector || !m_heightInspector )
			{
				return false;
			}

			Inspector::ResultPtr contextWidthInspection = m_widthInspector->inspect();
			Inspector::ResultPtr contextHeightInspection = m_heightInspector->inspect();

			if( !contextWidthInspection || !contextHeightInspection )
			{
				return false;
			}

			return true;
		}

		bool enabled() const override
		{
			if( !m_widthInspector || !m_heightInspector )
			{
				return false;
			}

			// Return true without checking the `enabled()` state of our inspections.
			// This allows the tooltip-on-highlight behavior to show a tooltip explaining
			// why an edit is not possible. The alternative is to draw the tooltip for all
			// handles regardless of mouse position because a handle can only be in a disabled
			// or highlighted drawing state.
			// The drawing code takes care of graying out uneditable handles and the inspections
			// prevent the value from being changed.
			return true;
		}

		std::vector<GafferSceneUI::Private::Inspector *> inspectors() const override
		{
			return {m_widthInspector.get(), m_heightInspector.get()};
		}

	protected :

		void renderHandle( const Style *style, Style::State state ) const override
		{
			if( getLookThroughLight() )
			{
				return;
			}

			State::bindBaseState();
			auto glState = const_cast<State *>( State::defaultState() );

			IECoreGL::GroupPtr group = new IECoreGL::Group;

			const bool highlighted = state == Style::State::HighlightedState;

			float spokeRadius = 0;
			float coneSize = 0;
			float cornerRadius = 0;

			if( IECoreGL::Selector::currentSelector() )
			{
				spokeRadius = g_lineSelectionWidth;
				coneSize = g_arrowHandleSelectionSize;
				cornerRadius = g_circleHandleSelectionWidth;
			}
			else
			{
				spokeRadius = highlighted ? g_lineHandleWidthLarge : g_lineHandleWidth;
				coneSize = highlighted ? g_arrowHandleSizeLarge : g_arrowHandleSize;
				cornerRadius = highlighted ? g_circleHandleWidthLarge : g_circleHandleWidth;
			}

			spokeRadius *= g_quadLightHandleSizeMultiplier;
			coneSize *= g_quadLightHandleSizeMultiplier;
			cornerRadius *= g_quadLightHandleSizeMultiplier;

			group->getState()->add(
				new IECoreGL::ShaderStateComponent(
					ShaderLoader::defaultShaderLoader(),
					TextureLoader::defaultTextureLoader(),
					"",
					"",
					constantFragSource(),
					new CompoundObject
				)
			);

			auto standardStyle = runTimeCast<const StandardStyle>( style );
			assert( standardStyle );
			const Color3f highlightColor3 = standardStyle->getColor( StandardStyle::Color::HighlightColor );
			const Color4f highlightColor4 = Color4f( highlightColor3.x, highlightColor3.y, highlightColor3.z, 1.f );

			const bool enabled = allInspectionsEnabled();

			group->getState()->add(
				new IECoreGL::Color(
					enabled ? ( highlighted ? g_lightToolHighlightColor4 : highlightColor4 ) : g_lightToolDisabledColor4
				)
			);

			if( ( m_handleType & HandleType::Width ) && ( m_handleType & HandleType::Height ) )
			{
				// Circles at corners for planar drag

				IECoreGL::GroupPtr iconGroup = new IECoreGL::Group;
				iconGroup->getState()->add(
					new IECoreGL::ShaderStateComponent(
						ShaderLoader::defaultShaderLoader(),
						TextureLoader::defaultTextureLoader(),
						faceCameraVertexSource(),
						"",
						constantFragSource(),
						new CompoundObject
					)
				);
				iconGroup->setTransform(
					M44f().scale( V3f( cornerRadius ) * ::rasterScaleFactor( this, V3f( 0 ) ) )
				);
				iconGroup->addChild( circle() );
				group->addChild( iconGroup );
			}
			else
			{
				// Lines and arrows on edges for linear drag

				LineSegment3f edgeSegment = this->edgeSegment( handleInspections() );

				M44f coneTransform;
				M44f edgeTransform;
				edgeTransforms( edgeSegment, coneTransform, edgeTransform );

				IECoreGL::GroupPtr coneGroup = new IECoreGL::Group;
				coneGroup->setTransform( coneTransform * M44f().scale( V3f( coneSize ) ) );
				coneGroup->addChild( unitCone() );
				group->addChild( coneGroup );

				IECoreGL::GroupPtr edgeGroup = new IECoreGL::Group;
				edgeGroup->addChild(
					cone(
						edgeSegment.length(),
						spokeRadius * ::rasterScaleFactor( this, edgeSegment.p0 ),
						spokeRadius * ::rasterScaleFactor( this, edgeSegment.p1 )
					)
				);
				edgeGroup->setTransform( edgeTransform );

				group->addChild( edgeGroup );
			}

			group->render( glState );

			if( highlighted )
			{
				std::vector<const Inspector::Result *> inspections;
				for( const auto &[widthInspection, originalWidth, heightInspection, originalHeight] : m_inspections )
				{
					if( m_handleType & HandleType::Width )
					{
						inspections.push_back( widthInspection.get() );
					}
					if( m_handleType & HandleType::Height )
					{
						inspections.push_back( heightInspection.get() );
					}
				}
				std::string tipSuffix = "";
				if( m_handleType & HandleType::Width )
				{
					tipSuffix = "widths";
				}
				if( m_handleType & HandleType::Height )
				{
					tipSuffix = m_handleType & HandleType::Width ? "plugs" : "heights";
				}

				drawSelectionTips(
					m_edgeCursorPoint,
					inspections,
					tipSuffix,
					( m_handleType & HandleType::Width && m_handleType &HandleType::Height ) ? "Hold Ctrl to maintain aspect ratio" : "",
					this,
					m_view->viewportGadget(),
					style
				);
			}
		}

	private :

		struct InspectionInfo
		{
			Inspector::ResultPtr widthInspection;
			float originalWidth;
			Inspector::ResultPtr heightInspection;
			float originalHeight;
		};

		bool mouseMove( const ButtonEvent &event )
		{
			if( !m_widthInspector || ! m_heightInspector )
			{
				return false;
			}

			if( m_handleType & HandleType::Width && m_handleType &HandleType::Height )
			{
				m_edgeCursorPoint = V3f( 0, 0, 0 );
				return false;
			}

			LineSegment3f edgeSegment = this->edgeSegment( handleInspections() );

			V3f eventClosest;
			m_edgeCursorPoint = edgeSegment.closestPoints( LineSegment3f( event.line.p0, event.line.p1 ), eventClosest );

			dirty( DirtyType::Render );

			return false;
		}

		void dragBegin( const DragDropEvent &event ) override
		{
			const auto &[widthInspection, originalWidth, heightInspection, originalHeight] = handleInspections();

			m_dragStartInfo.widthInspection = widthInspection;
			m_dragStartInfo.originalWidth = originalWidth;
			m_dragStartInfo.heightInspection = heightInspection;
			m_dragStartInfo.originalHeight = originalHeight;

			if( m_handleType & HandleType::Width && m_handleType & HandleType::Height )
			{
				m_drag = Handle::PlanarDrag( this, V3f( 0 ), V3f( m_xSign, 0, 0 ), V3f( 0, m_ySign, 0 ), event, true );
			}
			else if( m_handleType & HandleType::Width )
			{
				m_drag = Handle::LinearDrag( this, LineSegment3f( V3f( 0 ), V3f( m_xSign, 0, 0 ) ), event, true );
			}
			else if( m_handleType & HandleType::Height )
			{
				m_drag = Handle::LinearDrag( this, LineSegment3f( V3f( 0 ), V3f( 0, m_ySign, 0 ) ), event, true );
			}
		}

		InspectionInfo handleInspections() const
		{
			ScenePlug::PathScope pathScope( handleScenePath()->getContext() );
			pathScope.setPath( &handleScenePath()->names() );

			return inspectionInfo();
		}

		// Returns a `InspectionInfo` object for the current context.
		InspectionInfo inspectionInfo() const
		{
			Inspector::ResultPtr widthInspection = nullptr;
			float originalWidth = 0;

			// Get an inspection if possible regardless of the handle type because drawing
			// edge lines requires the opposite dimension's value.
			if( m_widthInspector )
			{
				widthInspection = m_widthInspector->inspect();
				if( widthInspection )
				{
					auto originalWidthData = runTimeCast<const IECore::FloatData>( widthInspection->value() );
					assert( originalWidthData );
					originalWidth = originalWidthData->readable();
				}
			}

			Inspector::ResultPtr heightInspection = nullptr;
			float originalHeight = 0;
			if( m_heightInspector )
			{
				heightInspection = m_heightInspector->inspect();
				if( heightInspection )
				{
					auto originalHeightData = runTimeCast<const IECore::FloatData>( heightInspection->value() );
					assert( originalHeightData );
					originalHeight = originalHeightData->readable();
				}
			}

			return { widthInspection, originalWidth, heightInspection, originalHeight };
		}

		bool allInspectionsEnabled() const
		{
			bool enabled = true;
			for( auto &[widthInspection, originalWidth, heightInspection, originalHeight] : m_inspections )
			{
				if( m_handleType & HandleType::Width )
				{
					enabled &= widthInspection ? widthInspection->editable() : false;
				}
				if( m_handleType & HandleType::Height )
				{
					enabled &= heightInspection ? heightInspection->editable() : false;
				}
			}

			return enabled;
		}

		LineSegment3f edgeSegment( const InspectionInfo &inspectionInfo ) const
		{
			const auto &[widthInspection, width, heightInspection, height] = inspectionInfo;

			float fullEdgeLength = 0;
			float fullEdgeLengthHalf = 0;
			float radius0 = 0;
			float radius1 = 0;
			if( m_handleType & HandleType::Width )
			{
				fullEdgeLength = height * m_scale.y;
				fullEdgeLengthHalf = fullEdgeLength * 0.5f;
				radius0 = g_circleHandleWidthLarge * ::rasterScaleFactor( this, V3f( 0, -fullEdgeLengthHalf, 0 ) ) * g_quadLightHandleSizeMultiplier;
				radius1 = g_circleHandleWidthLarge * ::rasterScaleFactor( this, V3f( 0, fullEdgeLengthHalf, 0 ) ) * g_quadLightHandleSizeMultiplier;
			}
			else
			{
				fullEdgeLength = width * m_scale.x;
				fullEdgeLengthHalf = fullEdgeLength * 0.5f;
				radius0 = g_circleHandleWidthLarge * ::rasterScaleFactor( this, V3f( -fullEdgeLengthHalf, 0, 0 ) ) * g_quadLightHandleSizeMultiplier;
				radius1 = g_circleHandleWidthLarge * ::rasterScaleFactor( this, V3f( fullEdgeLengthHalf, 0, 0 ) ) * g_quadLightHandleSizeMultiplier;
			}

			LineSegment3f result;

			if( m_handleType & HandleType::Width )
			{
				result.p0 = V3f( 0, std::min( 0.f, -fullEdgeLengthHalf + radius0 ), 0 );
				result.p1 = V3f( 0, std::max( 0.f, fullEdgeLengthHalf - radius1 ), 0 );
			}
			else
			{
				result.p0 = V3f( std::min( 0.f, -fullEdgeLengthHalf + radius0 ), 0, 0 );
				result.p1 = V3f( std::max( 0.f, fullEdgeLengthHalf - radius1 ), 0, 0 );
			}

			return result;
		}

		void edgeTransforms( const LineSegment3f &edgeSegment, M44f &coneTransform, M44f &edgeTransform ) const
		{
			if( m_handleType & HandleType::Width )
			{
				coneTransform = M44f().rotate( V3f( 0, M_PI * 0.5f * m_xSign, 0 ) );
				edgeTransform =
					M44f().rotate( V3f( -M_PI * 0.5f, 0, 0 ) ) *
					M44f().translate( V3f( 0, edgeSegment.p0.y, 0 ) )
				;
			}
			else
			{
				coneTransform = M44f().rotate( V3f( M_PI * 0.5f * -m_ySign, 0, 0 ) );
				edgeTransform =
					M44f().rotate( V3f( 0, M_PI * 0.5f, 0 ) ) *
					M44f().translate( V3f( edgeSegment.p0.x, 0, 0 ) )
				;
			}
			coneTransform *= M44f().scale( V3f( ::rasterScaleFactor( this, V3f( 0.0 ) ) ) );
		}

		ParameterInspectorPtr m_widthInspector;
		ParameterInspectorPtr m_heightInspector;

		const SceneView *m_view;

		const InternedString m_widthParameter;
		const InternedString m_heightParameter;

		std::vector<InspectionInfo> m_inspections;

		std::variant<std::monostate, Handle::LinearDrag, Handle::PlanarDrag> m_drag;

		const unsigned m_handleType;

		InspectionInfo m_dragStartInfo;

		// The sign for each axis of the handle
		const float m_xSign;
		const float m_ySign;

		V3f m_edgeCursorPoint;
		V2f m_scale;  // width and height scale of the light's transform
};

// ============================================================================
// HandlesGadget
// ============================================================================

class HandlesGadget : public Gadget
{

	public :

		HandlesGadget( const std::string &name="HandlesGadget" )
			:	Gadget( name )
		{
		}

	protected :

		Imath::Box3f renderBound() const override
		{
			// We need `renderLayer()` to be called any time it will
			// be called for one of our children. Our children claim
			// infinite bounds to account for their raster scale, so
			// we must too.
			Box3f b;
			b.makeInfinite();
			return b;
		}

		void renderLayer( Layer layer, const Style *, RenderReason ) const override
		{
			if( layer != Layer::MidFront )
			{
				return;
			}

			// Clear the depth buffer so that the handles render
			// over the top of the SceneGadget. Otherwise they are
			// unusable when the object is larger than the handles.
			/// \todo Can we really justify this approach? Does it
			/// play well with new Gadgets we'll add over time? If
			/// so, then we should probably move the depth clearing
			/// to `Gadget::render()`, in between each layer. If
			/// not we'll need to come up with something else, perhaps
			/// going back to punching a hole in the depth buffer using
			/// `glDepthFunc( GL_GREATER )`. Or maybe an option to
			/// render gadgets in an offscreen buffer before compositing
			/// them over the current framebuffer?
			glClearDepth( 1.0f );
			glClear( GL_DEPTH_BUFFER_BIT );
			glEnable( GL_DEPTH_TEST );

		}

		unsigned layerMask() const override
		{
			return (unsigned)Layer::MidFront;
		}

};

}  // namespace

// ============================================================================
// LightTool
// ============================================================================

GAFFER_NODE_DEFINE_TYPE( LightTool );

LightTool::ToolDescription<LightTool, SceneView> LightTool::g_toolDescription;
size_t LightTool::g_firstPlugIndex = 0;

LightTool::LightTool( SceneView *view, const std::string &name ) :
	SelectionTool( view, name ),
	m_handles( new HandlesGadget() ),
	m_handleInspectionsDirty( true ),
	m_handleTransformsDirty( true ),
	m_priorityPathsDirty( true ),
	m_dragging( false ),
	m_scriptNode( nullptr ),
	m_mergeGroupId( 0 )
{
	view->viewportGadget()->addChild( m_handles );
	m_handles->setVisible( false );

	// Spotlight handles

	m_handles->addChild( new SpotLightHandle( "spot", SpotLightHandle::HandleType::Penumbra, view, 0, "westConeAngleParameter" ) );
	m_handles->addChild( new SpotLightHandle( "spot", SpotLightHandle::HandleType::Cone, view, 0, "westPenumbraAngleParameter" ) );
	m_handles->addChild( new SpotLightHandle( "spot", SpotLightHandle::HandleType::Penumbra, view, 90, "southConeAngleParameter" ) );
	m_handles->addChild( new SpotLightHandle( "spot", SpotLightHandle::HandleType::Cone, view, 90, "southPenumbraAngleParameter" ) );
	m_handles->addChild( new SpotLightHandle( "spot", SpotLightHandle::HandleType::Penumbra, view, 180, "eastConeAngleParameter" ) );
	m_handles->addChild( new SpotLightHandle( "spot", SpotLightHandle::HandleType::Cone, view, 180, "eastPenumbraAngleParameter" ) );
	m_handles->addChild( new SpotLightHandle( "spot", SpotLightHandle::HandleType::Penumbra, view, 270, "northConeAngleParameter" ) );
	m_handles->addChild( new SpotLightHandle( "spot", SpotLightHandle::HandleType::Cone, view, 270, "northPenumbraAngleParameter" ) );

	// Quadlight handles

	m_handles->addChild( new WidthHeightHandle( "quad", WidthHeightHandle::HandleType::Width, view, "widthParameter", "heightParameter", -1.f, 0, "westParameter" ) );
	m_handles->addChild( new WidthHeightHandle( "quad", WidthHeightHandle::HandleType::Width | WidthHeightHandle::HandleType::Height, view, "widthParameter", "heightParameter", -1.f, -1.f, "southWestParameter" ) );
	m_handles->addChild( new WidthHeightHandle( "quad", WidthHeightHandle::HandleType::Height, view, "widthParameter", "heightParameter", 0, -1.f, "southParameter" ) );
	m_handles->addChild( new WidthHeightHandle( "quad", WidthHeightHandle::HandleType::Width | WidthHeightHandle::HandleType::Height, view, "widthParameter", "heightParameter", 1.f, -1.f, "soutEastParameter" ) );
	m_handles->addChild( new WidthHeightHandle( "quad", WidthHeightHandle::HandleType::Width, view, "widthParameter", "heightParameter", 1.f, 0.f, "eastParameter" ) );
	m_handles->addChild( new WidthHeightHandle( "quad", WidthHeightHandle::HandleType::Width | WidthHeightHandle::HandleType::Height, view, "widthParameter", "heightParameter", 1.f, 1.f, "northEastParameter" ) );
	m_handles->addChild( new WidthHeightHandle( "quad", WidthHeightHandle::HandleType::Height, view, "widthParameter", "heightParameter", 0, 1.f, "northParameter" ) );
	m_handles->addChild( new WidthHeightHandle( "quad", WidthHeightHandle::HandleType::Width | WidthHeightHandle::HandleType::Height, view, "widthParameter", "heightParameter", -1.f, 1.f, "northWestParameter" ) );

	// DiskLight handles
	m_handles->addChild( new WidthHeightHandle( "disk", WidthHeightHandle::HandleType::Width, view, "radiusParameter", "radiusParameter", 1.f, 0, "diskRadiusParameter" ) );

	// SphereLightHandles

	// CylinderLightHandles


	for( const auto &c : m_handles->children() )
	{
		auto handle = runTimeCast<Handle>( c );
		handle->setVisible( false );
		handle->dragBeginSignal().connectFront( boost::bind( &LightTool::dragBegin, this, ::_1 ) );
		handle->dragMoveSignal().connect( boost::bind( &LightTool::dragMove, this, ::_1, ::_2 ) );
		handle->dragEndSignal().connect( boost::bind( &LightTool::dragEnd, this, ::_1 ) );
	}

	storeIndexOfNextChild( g_firstPlugIndex );

	addChild( new ScenePlug( "__scene", Plug::In ) );

	scenePlug()->setInput( view->inPlug<ScenePlug>() );

	plugDirtiedSignal().connect( boost::bind( &LightTool::plugDirtied, this, ::_1 ) );
	view->plugDirtiedSignal().connect( boost::bind( &LightTool::plugDirtied, this, ::_1 ) );

	connectToViewContext();
	view->contextChangedSignal().connect( boost::bind( &LightTool::connectToViewContext, this ) );

	Metadata::plugValueChangedSignal().connect( boost::bind( &LightTool::metadataChanged, this, ::_3 ) );
	Metadata::nodeValueChangedSignal().connect( boost::bind( &LightTool::metadataChanged, this, ::_2 ) );
}

LightTool::~LightTool()
{

}

const PathMatcher LightTool::selection() const
{
	return ContextAlgo::getSelectedPaths( view()->getContext() );
}

LightTool::SelectionChangedSignal &LightTool::selectionChangedSignal()
{
	return m_selectionChangedSignal;
}

ScenePlug *LightTool::scenePlug()
{
	return getChild<ScenePlug>( g_firstPlugIndex );
}

const ScenePlug *LightTool::scenePlug() const
{
	return getChild<ScenePlug>( g_firstPlugIndex );
}

void LightTool::connectToViewContext()
{
	m_contextChangedConnection = view()->getContext()->changedSignal().connect(
		boost::bind( &LightTool::contextChanged, this, ::_2 )
	);
}

void LightTool::contextChanged( const InternedString &name )
{
	if(
		ContextAlgo::affectsSelectedPaths( name ) ||
		ContextAlgo::affectsLastSelectedPath( name ) ||
		!boost::starts_with( name.string(), "ui:" )
	)
	{
		m_handleInspectionsDirty = true;
		m_handleTransformsDirty = true;
		m_priorityPathsDirty = true;
		selectionChangedSignal()( *this );
	}
}

void LightTool::metadataChanged( InternedString key )
{
	if( !MetadataAlgo::readOnlyAffectedByChange( key ) )
	{
		return;
	}

	if( !m_handleInspectionsDirty )
	{
		m_handleInspectionsDirty = true;
		view()->viewportGadget()->renderRequestSignal()( view()->viewportGadget() );
	}
}

void LightTool::updateHandleInspections()
{
	if( m_dragging )
	{
		return;
	}

	auto scene = scenePlug()->getInput<ScenePlug>();
	scene = scene ? scene->getInput<ScenePlug>() : scene;
	if( !scene )
	{
		return;
	}

	m_inspectorsDirtiedConnection.clear();

	const PathMatcher selection = this->selection();
	if( selection.isEmpty() )
	{
		for( auto &c : m_handles->children() )
		{
			auto handle = runTimeCast<LightToolHandle>( c );
			handle->setVisible( false );
		}
		return;
	}

	ScenePlug::ScenePath lastSelectedPath = ContextAlgo::getLastSelectedPath( view()->getContext() );
	assert( selection.match( lastSelectedPath ) & PathMatcher::ExactMatch );

	bool lookThroughLight = false;

	if( auto lookThroughEnabledPlug = view()->descendant<BoolPlug>( "camera.lookThroughEnabled") )
	{
		if( lookThroughEnabledPlug->getValue() )
		{
			Path lookThroughCamera = Path( view()->descendant<StringPlug>( "camera.lookThroughCamera" )->getValue() );
			lookThroughLight = lookThroughCamera == Path( lastSelectedPath );
		}
	}

	ScenePlug::PathScope pathScope( view()->getContext() );

	for( auto &c : m_handles->children() )
	{
		auto handle = runTimeCast<LightToolHandle>( c );
		assert( handle );

		handle->update(
			new ScenePath( scene, view()->getContext(), lastSelectedPath ),
			view()->editScopePlug()
		);

		bool handleVisible = true;
		bool handleEnabled = true;

		for( PathMatcher::Iterator it = selection.begin(), eIt = selection.end(); it != eIt; ++it )
		{
			pathScope.setPath( &(*it) );

			handleVisible &= handle->visible();
			handleEnabled &= handle->enabled();
		}

		handle->setLookThroughLight( lookThroughLight );

		handle->setEnabled( handleEnabled );
		handle->setVisible( handleVisible );

		handle->clearDragInspections();

		if( handleVisible )
		{
			for( PathMatcher::Iterator it = selection.begin(), eIt = selection.end(); it != eIt; ++it )
			{
				pathScope.setPath( &(*it) );
				handle->addDragInspection();
			}
		}
	}
}

void LightTool::updateHandleTransforms( float rasterScale )
{
	Context::Scope scopedContext( view()->getContext() );

	auto scene = scenePlug()->getInput<ScenePlug>();
	scene = scene ? scene->getInput<ScenePlug>() : scene;
	if( !scene )
	{
		return;
	}

	const PathMatcher selection = this->selection();
	if( selection.isEmpty() )
	{
		return;
	}

	ScenePlug::ScenePath lastSelectedPath = ContextAlgo::getLastSelectedPath( view()->getContext() );
	assert( selection.match( lastSelectedPath ) & PathMatcher::Result::ExactMatch );
	if( !scene->exists( lastSelectedPath ) )
	{
		return;
	}

	const M44f fullTransform = scene->fullTransform( lastSelectedPath );
	m_handles->setTransform( sansScalingAndShear( fullTransform ) );

	V3f scale;
	V3f shear;
	extractScalingAndShear( fullTransform, scale, shear );

	for( auto &c : m_handles->children() )
	{
		auto handle = runTimeCast<LightToolHandle>( c );
		assert( handle );

		if( handle->getVisible() )
		{
			handle->updateLocalTransform( scale, shear );
			handle->setRasterScale( rasterScale );
		}

	}
}

void LightTool::plugDirtied( const Plug *plug )
{

	// Note : This method is called not only when plugs
	// belonging to the LightTool are dirtied, but
	// _also_ when plugs belonging to the View are dirtied.

	if(
		plug == activePlug() ||
		plug == scenePlug()->childNamesPlug() ||
		( plug->ancestor<View>() && plug == view()->editScopePlug() )
	)
	{
		if( !m_dragging )
		{
			selectionChangedSignal()( *this );
		}
		m_handleInspectionsDirty = true;
		m_priorityPathsDirty = true;
	}

	if( plug == activePlug() )
	{
		if( activePlug()->getValue() )
		{
			m_preRenderConnection = view()->viewportGadget()->preRenderSignal().connect(
				boost::bind( &LightTool::preRender, this )
			);
		}
		else
		{
			m_preRenderConnection.disconnect();
			m_handles->setVisible( false );
		}
	}

	if( plug == scenePlug()->transformPlug() )
	{
		m_handleTransformsDirty = true;
	}

	if(
		plug == view()->descendant<FloatPlug>( "drawingMode.visualiser.scale" ) ||
		plug == view()->descendant<FloatPlug>( "drawingMode.light.frustumScale" )
	)
	{
		m_handleInspectionsDirty = true;
	}

	/// \todo Checking for dirty attributes overlaps with the job of the inspector
	/// dirtied plug from `updateHandleInspections()`. Should we remove handling
	/// inspector dirtied signals? The `gl:visualiser:scale` attribute is used to
	/// place the handles, so we at least need to catch changes to that attribute.
	if( plug == scenePlug()->attributesPlug() )
	{
		m_handleInspectionsDirty = true;
		m_handleTransformsDirty = true;
	}
}

void LightTool::preRender()
{
	if( !m_dragging )
	{
		if( m_priorityPathsDirty )
		{
			m_priorityPathsDirty = false;
			auto sceneGadget = static_cast<SceneGadget *>( view()->viewportGadget()->getPrimaryChild() );
			if( !selection().isEmpty() )
			{
				sceneGadget->setPriorityPaths( ContextAlgo::getSelectedPaths( view()->getContext() ) );
			}
			else
			{
				sceneGadget->setPriorityPaths( IECore::PathMatcher() );
			}
		}
	}

	if( m_handleInspectionsDirty && !m_dragging )
	{
		updateHandleInspections();
		m_handleInspectionsDirty = false;

		for( auto &c : m_handles->children() )
		{
			auto handle = runTimeCast<LightToolHandle>( c );
			if( handle->getVisible() )
			{
				m_handles->setVisible( true );
				break;
			}
		}
	}

	if( m_handleTransformsDirty )
	{
		updateHandleTransforms( 0 );
		m_handleTransformsDirty = false;
	}
}

void LightTool::dirtyHandleTransforms()
{
	m_handleTransformsDirty = true;
}

RunTimeTypedPtr LightTool::dragBegin( Gadget *gadget )
{
	m_dragging = true;

	auto handle = runTimeCast<LightToolHandle>( gadget );
	assert( handle );
	const PathMatcher selection = this->selection();

	std::vector<Inspector *> inspectors = handle->inspectors();
	if( !inspectors.empty() )
	{
		ScenePlug::PathScope pathScope( view()->getContext() );
		PathMatcher::Iterator it = selection.begin();
		pathScope.setPath( &( *it ) );
		if( Inspector::ResultPtr inspection = inspectors[0]->inspect() )
		{
			if( ValuePlug *source = inspection->source() )
			{
				m_scriptNode = source->ancestor<ScriptNode>();
			}
		}
	}

	return nullptr;
}

bool LightTool::dragMove( Gadget *gadget, const DragDropEvent &event )
{
	auto handle = runTimeCast<LightToolHandle>( gadget );
	assert( handle );

	UndoScope undoScope( m_scriptNode.get(), UndoScope::Enabled, undoMergeGroup() );

	handle->handleDragMove( event );

	return true;
}

bool LightTool::dragEnd( Gadget *gadget )
{
	m_dragging = false;
	m_mergeGroupId++;
	selectionChangedSignal()( *this );

	auto handle = runTimeCast<LightToolHandle>( gadget );
	handle->handleDragEnd();

	return false;
}

std::string LightTool::undoMergeGroup() const
{
	return fmt::format( "LightTool{}{}", fmt::ptr( this ), m_mergeGroupId );
}
