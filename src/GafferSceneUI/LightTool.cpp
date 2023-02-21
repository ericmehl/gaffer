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

#include "GafferSceneUI/Private/MetadataValueParameterInspector.h"
#include "GafferSceneUI/Private/Inspector.h"
#include "GafferSceneUI/ContextAlgo.h"
#include "GafferSceneUI/SceneView.h"

#include "GafferScene/ScenePath.h"

#include "GafferUI/Handle.h"
#include "GafferUI/StandardStyle.h"

#include "Gaffer/Metadata.h"
#include "Gaffer/NameValuePlug.h"
#include "Gaffer/PathFilter.h"
#include "Gaffer/ScriptNode.h"
#include "Gaffer/TweakPlug.h"
#include "Gaffer/UndoScope.h"

#include "IECoreGL/CurvesPrimitive.h"
#include "IECoreGL/Group.h"
#include "IECoreGL/MeshPrimitive.h"
#include "IECoreGL/ShaderLoader.h"
#include "IECoreGL/ShaderStateComponent.h"
#include "IECoreGL/TextureLoader.h"
#include "IECoreGL/ToGLMeshConverter.h"

#include "IECoreScene/MeshPrimitive.h"

#include "boost/algorithm/string/predicate.hpp"
#include "boost/bind/bind.hpp"

#include "fmt/format.h"

using namespace boost::placeholders;
using namespace Imath;
using namespace IECoreScene;
using namespace IECoreGL;
using namespace Gaffer;
using namespace GafferUI;
using namespace GafferScene;
using namespace GafferSceneUI;
using namespace GafferSceneUI::Private;

namespace
{

// Color from `StandardLightVisualiser`
const Color3f g_lightToolHandleColor = Color3f( 1.0f, 0.835f, 0.07f );
const Color4f g_lightToolHandleColor4 = Color4f( g_lightToolHandleColor.x, g_lightToolHandleColor.y, g_lightToolHandleColor.z, 1.f );

// Multiplied by the highlight color for drawing a parameter's previous value
const float g_highlightMultiplier = 0.8f;

const InternedString g_lightFrustumScaleAttributeName( "gl:light:frustumScale" );

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
	return sourcePlug;
}

const char *translucentConstantFragSource()
{
	return
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

IECoreGL::MeshPrimitivePtr cube()
{
	static IECoreGL::MeshPrimitivePtr result;
	if( result )
	{
		return result;
	}

	IECoreScene::MeshPrimitivePtr mesh = IECoreScene::MeshPrimitive::createBox(
		Box3f( V3f( -1 ), V3f( 1 ) )
	);

	ToGLMeshConverterPtr converter = new ToGLMeshConverter( mesh );
	result = runTimeCast<IECoreGL::MeshPrimitive>( converter->convert() );

	return result;
}

IECoreScene::MeshPrimitivePtr solidAngle( float radius, float startFraction, float stopFraction, Color3f color )
{
	IntVectorDataPtr vertsPerPolyData = new IntVectorData;
	IntVectorDataPtr vertIdsData = new IntVectorData;
	V3fVectorDataPtr pData = new V3fVectorData;

	auto &vertsPerPoly = vertsPerPolyData->writable();
	auto &vertIds = vertIdsData->writable();
	auto &p = pData->writable();

	const int numCircleDivisions = 100;
	const int numSegments = std::max( 1, (int)ceil( abs( stopFraction - startFraction ) * numCircleDivisions ) );

	p.push_back( V3f( 0, 0, radius ) );

	for( int i = 0; i < numSegments + 1; ++i )
	{
		const float a = ( startFraction + ( stopFraction - startFraction ) * (float)i / (float)numSegments ) * 2.f * M_PI;
		p.push_back( V3f( -sin( a ), 0, -cos( a ) ) * radius + V3f( 0, 0, radius ) );
	}

	for( int i = 0; i < numSegments; ++i )
	{
		vertIds.push_back( i + 1 );
		vertIds.push_back( i + 2 );
		vertIds.push_back( 0 );
		vertsPerPoly.push_back( 3 );
	}

	IECoreScene::MeshPrimitivePtr solidArc = new IECoreScene::MeshPrimitive( vertsPerPolyData, vertIdsData, "linear", pData );
	solidArc->variables["N"] = IECoreScene::PrimitiveVariable( IECoreScene::PrimitiveVariable::Constant, new V3fData( V3f( 0, 1, 0 ) ) );
	solidArc->variables["Cs"] = IECoreScene::PrimitiveVariable( IECoreScene::PrimitiveVariable::Constant, new Color3fData( color ) );

	return solidArc;
}

class LightToolHandle : public Handle
{

	public :

		LightToolHandle(
			const std::string &attributePattern,
			const InternedString &metadataKey,
			const std::string &name
		) :
			Handle( name ),
			m_attributePattern( attributePattern ),
			m_metadataKey( metadataKey )
		{

		}
		~LightToolHandle()
		{

		}

		// Update `m_inspector` so its `dirtiedSignal()` triggers correctly. Derived classes
		// should call this parent method first, then implement custom logic.
		virtual void update( ScenePathPtr scenePath, const PlugPtr &editScope )
		{
			m_handleScenePath = scenePath;

			m_inspector = new MetadataValueParameterInspector(
				m_handleScenePath->getScene(),
				m_editScope,
				m_attributePattern,
				m_metadataKey
			);
		}

		MetadataValueParameterInspector *inspector() const
		{
			return m_inspector.get();
		}

		const std::string attributePattern() const
		{
			return m_attributePattern;
		}

		ScenePath *handleScenePath() const
		{
			return m_handleScenePath.get();
		}

		Plug *editScope() const
		{
			return m_editScope.get();
		}

		// Called at the beginning of a drag with the path to be inspected
		// set in the current context. Must be implemented by derived classes to collect
		// whatever data will be needed to implement the drag operation.
		virtual void addDragInspection() = 0;
		virtual bool handleDragMove( const GafferUI::DragDropEvent &event ) = 0;
		virtual bool handleDragEnd() = 0;

		// Must be implemented by derived classes to set the local transform of the handle
		// to the light.
		virtual void updateTransform() = 0;

	private :

		MetadataValueParameterInspectorPtr m_inspector;

		ScenePathPtr m_handleScenePath;

		const std::string m_attributePattern;
		const InternedString m_metadataKey;

		Gaffer::PlugPtr m_editScope;
};

class SpotLightHandle : public LightToolHandle
{

	public :

		enum class HandleType
		{
			Cone,
			Penumbra
		};

		SpotLightHandle(
			const std::string &attributePattern,
			const InternedString &metadataKey,
			HandleType handleType,
			const  std::string &name = "SpotLightHandle"
		) :
			LightToolHandle( attributePattern, metadataKey, name ),
			m_handleType( handleType ),
			m_frustumScale( 1.f ),
			m_lensRadius( 0 ),
			m_valueScale( 1.f )
		{

		}
		~SpotLightHandle() override
		{

		}

		void update( ScenePathPtr scenePath, const PlugPtr &editScope ) override
		{
			LightToolHandle::update( scenePath, editScope );

			m_otherInspector = new MetadataValueParameterInspector(
				handleScenePath()->getScene(),
				this->editScope(),
				attributePattern(),
				m_handleType == HandleType::Cone ? "penumbraAngleParameter" : "coneAngleParameter"
			);

			auto attributes = handleScenePath()->getScene()->attributes( handleScenePath()->names() );

			auto frustumScaleData = attributes->member<FloatData>( g_lightFrustumScaleAttributeName );
			m_frustumScale = frustumScaleData ? frustumScaleData->readable() : 1.f;

			/// \todo This is the same logic as `MetadataValueParameterInspector`, can it
			/// be removed when we transition to using UsdLux as our light representation?
			for( const auto &[attributeName, value] : attributes->members() )
			{
				if( StringAlgo::match( attributeName, attributePattern() ) && value->typeId() == (IECore::TypeId)ShaderNetworkTypeId )
				{
					const auto shader = attributes->member<ShaderNetwork>( attributeName )->outputShader();
					std::string shaderAttribute = shader->getType() + ":" + shader->getName();

					auto penumbraTypeData = Metadata::value<StringData>( shaderAttribute, "penumbraType" );
					m_penumbraType = penumbraTypeData ? std::optional<std::string>( penumbraTypeData->readable() ) : std::nullopt;

					m_lensRadius = 0;
					if( auto lensRadiusParameterName = Metadata::value<StringData>( shaderAttribute, "lensRadiusParameter" ) )
					{
						if( auto lensRadiusData = shader->parametersData()->member<FloatData>( lensRadiusParameterName->readable() ) )
						{
							m_lensRadius = lensRadiusData->readable();
						}
					}
				}
			}
		}

		void addDragInspection() override
		{
			auto coneAngleInspection = m_handleType == HandleType::Cone ? inspector()->inspect() : m_otherInspector->inspect();
			auto penumbraAngleInspection = m_handleType == HandleType::Cone ? m_otherInspector->inspect() : inspector()->inspect();

			ConstFloatDataPtr originalConeAngleData = runTimeCast<const IECore::FloatData>( coneAngleInspection->value() );
			assert( originalConeAngleData );  // Handle visibility and enabled state should ensure this is valid

			float originalPenumbraAngle = 0;
			if( penumbraAngleInspection )
			{
				ConstFloatDataPtr originalPenumbraAngleData = runTimeCast<const IECore::FloatData>( penumbraAngleInspection->value() );
				assert( originalPenumbraAngleData );

				originalPenumbraAngle = originalPenumbraAngleData->readable();
			}

			m_inspections.push_back(
				{
					coneAngleInspection,
					originalConeAngleData->readable(),
					penumbraAngleInspection,
					originalPenumbraAngle
				}
			);
		}

		bool handleDragMove( const GafferUI::DragDropEvent &event ) override
		{
			if( m_inspections.empty() )
			{
				return true;
			}

			// Similar to the `ScaleHandle`, approach zero scale asymptotically for better control
			/// \todo Is this really desireable? It prevents, for example, making the penumbra
			/// angle all the way to 0.
			float delta = (
				m_penumbraType == "inset" && m_handleType == HandleType::Penumbra
			) ? ( m_drag.startPosition() - m_drag.updatedPosition( event ) ) : ( m_drag.updatedPosition( event ) - m_drag.startPosition() );

			float newValueScale =  delta / rasterScaleFactor().x;
			if( newValueScale < 0 )
			{
				newValueScale = ( 1.f / ( 1.f - newValueScale ) ) - 1.f;
			}
			newValueScale += 1.f;

			// Check all the selected lights to see if they can take on the new value that would result
			// from `newValueScale`. If a light can't take on the new value, adjust `newValueScale` so
			// that it can. If we only conditionally set the value, the ability to actually hit the
			// min and max allowable values would depend on how fast the user drags, which is not intuitive.

			for( auto &[coneInspection, originalConeAngle, penumbraInspection, originalPenumbraAngle] : m_inspections )
			{
				if( m_handleType == HandleType::Cone )
				{
					const float newStartValue = originalConeAngle != 0 ? originalConeAngle : 1.f;
					const float newValue = newStartValue * newValueScale;

					if( penumbraInspection && m_penumbraType == "inset" && newValue * 0.5 - originalPenumbraAngle < 0 )
					{
						newValueScale = std::max( newValueScale, originalPenumbraAngle / ( 0.5f * newStartValue ) );
					}

					if( newValue > 180.f )
					{
						newValueScale = std::min( newValueScale, 180.f / newStartValue );
					}
				}
				else
				{
					const float newStartValue = originalPenumbraAngle != 0 ? originalPenumbraAngle : 1.f;
					const float newValue = newStartValue * newValueScale;

					if( m_penumbraType == "inset" && originalConeAngle * 0.5 - newValue < 0 )
					{
						newValueScale = std::min( newValueScale, 0.5f * originalConeAngle / newStartValue );
					}
					// if( m_penumbraType == "outset" && newValue * 2.f + originalConeAngle > 180.f )
					// {
					// 	canApply = false;
					// }
				}
			}

			m_valueScale = newValueScale;

			for( auto &[coneInspection, originalConeAngle, penumbraInspection, originalPenumbraAngle] : m_inspections )
			{
				if( m_handleType == HandleType::Cone )
				{
					float newValue = originalConeAngle != 0 ? originalConeAngle : 1.f;
					newValue *= newValueScale;

					auto plug = coneInspection->acquireEdit();
					auto floatPlug = runTimeCast<FloatPlug>( activeValuePlug( plug.get() ) );
					assert( floatPlug );

					floatPlug->setValue( newValue );
				}
				else
				{
					float newValue = originalPenumbraAngle != 0 ? originalPenumbraAngle : 1.f;
					newValue *= m_valueScale;

					auto plug = penumbraInspection->acquireEdit();
					auto floatPlug = runTimeCast<FloatPlug>( activeValuePlug( plug.get() ) );
					assert( floatPlug );

					floatPlug->setValue( newValue );
				}
			}

			return true;
		}

		bool handleDragEnd() override
		{
			m_inspections.clear();
			m_valueScale = 1.f;

			return false;
		}

		void updateTransform() override
		{
			ScenePlug::PathScope pathScope( handleScenePath()->getContext() );
			pathScope.setPath( &handleScenePath()->names() );

			auto inspection = inspector()->inspect();
			auto angleData = runTimeCast<const FloatData>( inspection->value() );
			assert( angleData );
			float angle = angleData->readable();

			if( m_penumbraType != "absolute" )
			{
				if( auto otherInspection = m_otherInspector->inspect() )
				{
					auto otherAngleData = runTimeCast<const FloatData>( otherInspection->value() );
					assert( otherAngleData );

					if( m_handleType == HandleType::Penumbra && ( !m_penumbraType || m_penumbraType == "inset" ) )
					{
						angle = otherAngleData->readable() - 2.f * angle;
					}
					else if( m_handleType == HandleType::Penumbra && m_penumbraType == "outset" )
					{
						angle = otherAngleData->readable() + 2.f * angle;
					}
				}
			}

			const float halfAngle = 0.5 * M_PI * angle / 180.f;

			// Multiply by 10 to match `StandardLightVisualiser::visualiser()`
			M44f transform = M44f().translate( V3f( 0, 0, -10.f * m_frustumScale ) ) *
				M44f().rotate( V3f( 0, -halfAngle, 0 ) ) *
				M44f().translate( V3f( m_lensRadius, 0, 0 ) )
			;

			if( m_handleType == HandleType::Penumbra )
			{
				transform *= M44f().rotate(  V3f( 0, 0, M_PI * 0.25f ) );
			}

			setTransform( transform );
		}

	protected :

		void renderHandle( const Style *style, Style::State state ) const override
		{
			State::bindBaseState();
			State *glState = const_cast<State *>( State::defaultState() );

			IECoreGL::GroupPtr group = new IECoreGL::Group;

			IECoreGL::GroupPtr cubeGroup = new IECoreGL::Group;
			cubeGroup->addChild( cube() );
			cubeGroup->setTransform( M44f().scale( V3f( 0.1f ) ) );

			group->addChild( cubeGroup );

			auto standardStyle = runTimeCast<const StandardStyle>( style );
			assert( standardStyle );
			Color3f highlightColor3 = standardStyle->getColor( StandardStyle::Color::HighlightColor );
			Color4f highlightColor4 = Color4f( highlightColor3.x, highlightColor3.y, highlightColor3.z, 1.f );

			group->getState()->add(
				new IECoreGL::Color(
					state == Style::State::HighlightedState ? highlightColor4 : g_lightToolHandleColor4
				)
			);
			group->getState()->add(
				new IECoreGL::ShaderStateComponent(
					ShaderLoader::defaultShaderLoader(),
					TextureLoader::defaultTextureLoader(),
					"",  // vertexSource
					"",  // geometrySource
					IECoreGL::Shader::constantFragmentSource(),
					new CompoundObject
				)
			);

			if( state == Style::State::HighlightedState )
			{
				ScenePlug::PathScope pathScope( handleScenePath()->getContext() );
				pathScope.setPath( &handleScenePath()->names() );

				if( auto inspection = inspector()->inspect() )
				{
					auto angleData = runTimeCast<const FloatData>( inspection->value() );
					assert( angleData );
					const float angle = angleData->readable();

					float currentFraction = 0;
					float previousFraction = 0;

					const float divisor = 
						m_handleType == HandleType::Cone ? 720.f :  // cone = 1/2 of 1 / 360
							m_penumbraType == "outset" ? 360.f :  // outset penumbra = 1 / 360
							m_penumbraType == "absolute" ? 720.f :   // absolute penumbra = 1/2 of  1 / 360
							-360.f  // inset penumbra = - 1 / 360, negative value accounts for inset flipping
					;
					currentFraction = angle / divisor;
					previousFraction = !m_inspections.empty() ? ( angle / m_valueScale ) / divisor : currentFraction;

					const float radius = 10.f * m_frustumScale / rasterScaleFactor().x;
					IECoreScene::MeshPrimitivePtr previousSolidAngle = nullptr;
					IECoreScene::MeshPrimitivePtr currentSolidAngle = nullptr;

					Color3f previousColor = highlightColor3 * g_highlightMultiplier;
					Color3f currentColor = highlightColor3;

					if(
						( m_handleType == HandleType::Cone && currentFraction > previousFraction ) ||
						( m_handleType == HandleType::Penumbra && m_penumbraType == "outset" && currentFraction > previousFraction ) ||
						( m_handleType == HandleType::Penumbra && m_penumbraType == "inset" && currentFraction < previousFraction )
					)
					{
						currentSolidAngle = solidAngle( radius, 0, currentFraction - previousFraction, currentColor );
						previousSolidAngle = solidAngle( radius, currentFraction - previousFraction, currentFraction, previousColor );
					}
					else if(
						( m_handleType == HandleType::Cone && currentFraction < previousFraction ) ||
						( m_handleType == HandleType::Penumbra && m_penumbraType == "outset" && currentFraction < previousFraction ) ||
						( m_handleType == HandleType::Penumbra && m_penumbraType == "inset" && currentFraction > previousFraction )
					)
					{
						currentSolidAngle = solidAngle( radius, 0, currentFraction, currentColor );
						previousSolidAngle = solidAngle( radius, 0, currentFraction - previousFraction, previousColor );
					}
					else
					{
						currentSolidAngle = solidAngle( radius, 0, currentFraction, previousColor );
					}

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

					if( currentSolidAngle )
					{
						ToGLMeshConverterPtr meshConverter = new ToGLMeshConverter( currentSolidAngle );
						solidAngleGroup->addChild( runTimeCast<IECoreGL::Renderable>( meshConverter->convert() ) );
					}
					if( previousSolidAngle )
					{
						ToGLMeshConverterPtr meshConverter = new ToGLMeshConverter( previousSolidAngle );
						solidAngleGroup->addChild( runTimeCast<IECoreGL::Renderable>( meshConverter->convert() ) );
					}

					group->addChild( solidAngleGroup );
				}
			}

			group->render( glState );
		}

	private :

		void dragBegin( const DragDropEvent &event ) override
		{
			m_drag = LinearDrag( this, LineSegment3f( V3f( 0 ), V3f( 1, 0, 0 ) ), event );
		}

		struct ConePenumbraInspection
		{
			const Inspector::ResultPtr coneInspection;
			const float originalConeAngle;
			const Inspector::ResultPtr penumbraInspection;
			const float originalPenumbraAngle;
		};

		std::vector<ConePenumbraInspection> m_inspections;

		LinearDrag m_drag;

		HandleType m_handleType;
		std::optional<std::string> m_penumbraType;

		MetadataValueParameterInspectorPtr m_otherInspector;

		float m_frustumScale;
		float m_lensRadius;
		float m_valueScale;
};

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

		void renderLayer( Layer layer, const Style *style, RenderReason reason ) const override
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

GAFFER_NODE_DEFINE_TYPE( LightTool );

LightTool::ToolDescription<LightTool, SceneView> LightTool::g_toolDescription;
size_t LightTool::g_firstPlugIndex = 0;

LightTool::LightTool( SceneView *view, const std::string &name ) :
	SelectionTool( view, name ),
	m_handles( new HandlesGadget() ),
	m_handleInspectionsDirty( true ),
	m_handleTransformsDirty( true ),
	m_selectionDirty( true ),
	m_priorityPathsDirty( true ),
	m_dragging( false ),
	m_mergeGroupId( 0 )
{
	view->viewportGadget()->addChild( m_handles );
	m_handles->setVisible( false );

	m_handles->addChild( new SpotLightHandle( "*:light", "coneAngleParameter", SpotLightHandle::HandleType::Cone ) );
	m_handles->addChild( new SpotLightHandle( "*:light", "penumbraAngleParameter", SpotLightHandle::HandleType::Penumbra ) );

	for( auto c : m_handles->children() )
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
}

LightTool::~LightTool()
{

}

const PathMatcher LightTool::selection() const
{
	updateSelection();
	return m_selection;
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
		m_selectionDirty = true;
		selectionChangedSignal()( *this );
		m_handleInspectionsDirty = true;
		m_handleTransformsDirty = true;
		m_priorityPathsDirty = true;
	}
}

void LightTool::updateHandleInspections()
{
	auto scene = scenePlug()->getInput<ScenePlug>();
	scene = scene ? scene->getInput<ScenePlug>() : scene;
	if( !scene )
	{
		return;
	}

	m_inspectorsDirtiedConnection.clear();

	auto selection = this->selection();
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

	ScenePlug::PathScope pathScope( view()->getContext() );

	for( auto &c : m_handles->children() )
	{
		auto handle = runTimeCast<LightToolHandle>( c );
		assert( handle );

		handle->update(
			new ScenePath( scene, view()->getContext(), lastSelectedPath ),
			view()->editScopePlug()
		);
		auto inspector = handle->inspector();

		/// \todo I think this is unneccessary because we are handling attribute and transform dirty signals?
		/// Or should this stay in and the attribute dirty handling goes away? That may be more targeted
		/// at the right attribute.
		m_inspectorsDirtiedConnection.push_back(
			inspector->dirtiedSignal().connect( boost::bind( &LightTool::dirtyHandleTransforms, this ) )
		);

		bool handleVisible = true;
		bool handleEnabled = true;

		for( PathMatcher::Iterator it = selection.begin(), eIt = selection.end(); it != eIt; ++it )
		{
			pathScope.setPath( &(*it) );

			auto inspection = inspector->inspect();

			handleVisible &= (bool)inspection;
			handleEnabled &= inspection ? inspection->editable() : false;
		}

		handle->setEnabled( handleEnabled );
		handle->setVisible( handleVisible );
	}
}

void LightTool::updateHandleTransforms( float rasterScale )
{
	auto scene = scenePlug()->getInput<ScenePlug>();
	scene = scene ? scene->getInput<ScenePlug>() : scene;
	if( !scene )
	{
		return;
	}

	ScenePlug::ScenePath lastSelectedPath = ContextAlgo::getLastSelectedPath( view()->getContext() );
	auto selection = this->selection();
	if( selection.isEmpty() )
	{
		return;
	}

	m_handles->setTransform( scene->fullTransform( lastSelectedPath ) );

	for( auto &c : m_handles->children() )
	{
		auto handle = runTimeCast<LightToolHandle>( c );
		assert( handle );

		handle->updateTransform();
		handle->setRasterScale( rasterScale );
	}
}

void LightTool::plugDirtied( const Plug *plug )
{

	// Note : This method is called not only when plugs
	// belonging to the TransformTool are dirtied, but
	// _also_ when plugs belonging to the View are dirtied.

	if(
		plug == activePlug() ||
		plug == scenePlug()->childNamesPlug() ||
		( plug->ancestor<View>() && plug == view()->editScopePlug() )
	)
	{
		m_selectionDirty = true;
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

	if(
		plug == scenePlug()->transformPlug()
	)
	{
		m_handleTransformsDirty = true;
	}
}

void LightTool::preRender()
{
	if( !m_dragging )
	{
		updateSelection();
		if( m_priorityPathsDirty )
		{
			m_priorityPathsDirty = false;
			SceneGadget *sceneGadget = static_cast<SceneGadget *>( view()->viewportGadget()->getPrimaryChild() );
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

	if( m_handleInspectionsDirty )
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
		updateHandleTransforms( 75 );
		m_handleTransformsDirty = false;
	}
}

void LightTool::updateSelection() const
{
	if( !m_selectionDirty )
	{
		return;
	}

	if( m_dragging )
	{
		// In theory, an expression or some such could change the effective
		// transform plug while we're dragging (for instance, by driving the
		// enabled status of a downstream transform using the translate value
		// we're editing). But we ignore that on the grounds that it's unlikely,
		// and also that it would be very confusing for the selection to be
		// changed mid-drag.
		return;
	}

	m_selection.clear();
	m_selectionDirty = false;

	if( !activePlug()->getValue() )
	{
		return;
	}

	// If there's no input scene, then there's no need to
	// do anything. Our `scenePlug()` receives its input
	// from the View's input, but that doesn't count.
	auto scene = scenePlug()->getInput<ScenePlug>();
	scene = scene ? scene->getInput<ScenePlug>() : scene;
	if( !scene )
	{
		return;
	}

	m_selection = ContextAlgo::getSelectedPaths( view()->getContext() );
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
	auto selection = this->selection();

	ScenePlug::PathScope pathScope( view()->getContext() );
	for( PathMatcher::Iterator it = selection.begin(), eIt = selection.end(); it != eIt; ++it )
	{
		pathScope.setPath( &(*it) );
		handle->addDragInspection();
	}

	return nullptr;
}

bool LightTool::dragMove( Gadget *gadget, const DragDropEvent &event )
{
	auto handle = runTimeCast<LightToolHandle>( gadget );

	UndoScope undoScope( view()->scriptNode(), UndoScope::Enabled, undoMergeGroup() );

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