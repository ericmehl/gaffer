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

#include "GafferSceneUI/SpotLightTool.h"

#include "GafferSceneUI/Private/ParameterInspector.h"
#include "GafferSceneUI/ContextAlgo.h"
#include "GafferSceneUI/SceneView.h"

#include "GafferUI/Handle.h"

#include "Gaffer/ScriptNode.h"
#include "Gaffer/UndoScope.h"

#include "boost/algorithm/string/predicate.hpp"
#include "boost/bind/bind.hpp"

#include "fmt/format.h"

using namespace boost::placeholders;
using namespace Imath;
using namespace IECoreScene;
using namespace Gaffer;
using namespace GafferUI;
using namespace GafferScene;
using namespace GafferSceneUI;

namespace
{

GraphComponent *sourceOrNull( const SpotLightTool::SelectionItem &item )
{
	return item.second ? ( item.second->editable() ? item.second->source() : nullptr ) : nullptr;
}

class AngleHandle : public Handle
{

	public :

		AngleHandle( const  std::string &name = "AngleGadget" ) : Handle( name )
		{

		}
		~AngleHandle() override
		{

		}

		float scaling( const DragDropEvent &event )
		{
			return abs( m_drag.updatedPosition( event ) / m_drag.startPosition() );
		}

	protected :

		void renderHandle( const Style *style, Style::State state ) const override
		{
			style->renderScaleHandle( Style::Axes::X, state );
		}

		void dragBegin( const DragDropEvent &event ) override
		{
			m_drag = LinearDrag( this, LineSegment3f( V3f( 0 ), V3f( 1, 0, 0 ) ), event );
		}

	private :

		LinearDrag m_drag;

};

}  // namespace

GAFFER_NODE_DEFINE_TYPE( SpotLightTool );

SpotLightTool::ToolDescription<SpotLightTool, SceneView> SpotLightTool::g_toolDescription;
size_t SpotLightTool::g_firstPlugIndex = 0;
SpotLightTool::SpotLightParameterMap SpotLightTool::g_spotLightParameterMap;

SpotLightTool::SpotLightTool( SceneView *view, const std::string &name ) :
	SelectionTool( view, name ),
	m_handle( new AngleHandle() ),
	m_handleDirty( true ),
	m_selectionDirty( true ),
	m_dragging( false ),
	m_mergeGroupId( 0 )
{
	view->viewportGadget()->addChild( m_handle );
	m_handle->setVisible( false );

	storeIndexOfNextChild( g_firstPlugIndex );

	addChild( new ScenePlug( "__scene", Plug::In ) );

	scenePlug()->setInput( view->inPlug<ScenePlug>() );

	plugDirtiedSignal().connect( boost::bind( &SpotLightTool::plugDirtied, this, ::_1 ) );
	view->plugDirtiedSignal().connect( boost::bind( &SpotLightTool::plugDirtied, this, ::_1 ) );

	connectToViewContext();
	view->contextChangedSignal().connect( boost::bind( &SpotLightTool::connectToViewContext, this ) );

	m_handle->dragBeginSignal().connectFront( boost::bind( &SpotLightTool::dragBegin, this ) );
	m_handle->dragMoveSignal().connect( boost::bind( &SpotLightTool::dragMove, this, ::_1, ::_2 ) );
	m_handle->dragEndSignal().connect( boost::bind( &SpotLightTool::dragEnd, this ) );
}

SpotLightTool::~SpotLightTool()
{

}

const SpotLightTool::Selection SpotLightTool::selection() const
{
	updateSelection();
	return m_selection;
}

bool SpotLightTool::selectionEditable() const
{
	Selection currentSelection = selection();

	if( currentSelection.empty() )
	{
		return false;
	}

	bool allEnabled = true;

	for( const auto &[path, inspection] : currentSelection )
	{
		if( inspection )
		{
			allEnabled &= Angle( inspection ).canApply();
		}
		else
		{
			return false;
		}
	}

	return allEnabled;
}

SpotLightTool::SelectionChangedSignal &SpotLightTool::selectionChangedSignal()
{
	return m_selectionChangedSignal;
}

Imath::M44f SpotLightTool::handleTransform()
{
	updateSelection();
	if( !selectionEditable() )
	{
		throw IECore::Exception( "Selection not editable" );
	}

	if( m_handleDirty )
	{
		updateHandle( 75 );
		m_handleDirty = false;
	}

	return m_handle->getTransform();
}

void SpotLightTool::setAngle( const float angle )
{

	Selection currentSelection = selection();

	for( const auto &[path, inspection] : currentSelection )
	{
		assert( inspection );

		// We don't use `Angle::apply()` here because that applies a multiplier
		// relative to the start angle. Instead we set it directly.
		auto floatPlug = runTimeCast<FloatPlug>( inspection->acquireEdit() );
		assert( floatPlug );
		floatPlug->setValue( angle );
	}
}

bool SpotLightTool::registerSpotLight(
	InternedString shaderAttribute,
	const std::string &shaderName,
	const std::string &coneAngleParameter
)
{
	auto result = g_spotLightParameterMap.insert(
		{
			spotLightParameterKey( shaderAttribute, shaderName ),
			ShaderNetwork::Parameter( "", coneAngleParameter )
		}
	);
	return result.second;
}

const std::string SpotLightTool::spotLightParameterKey( InternedString attribute, const std::string &shader )
{
	return attribute.string() + shader;
}

SpotLightTool::SpotLightParameterResult SpotLightTool::spotLightParameter(
	const ScenePlug *scene,
	const ScenePlug::ScenePath &path
)
{
	ConstCompoundObjectPtr attributes = scene->attributes( path );
	for( const auto &[attribute, value] : attributes->members() )
	{
		if( auto shader = runTimeCast<const ShaderNetwork>( value ) )
		{
			auto it = SpotLightTool::g_spotLightParameterMap.find(
				spotLightParameterKey( attribute, shader->outputShader()->getName() )
			);
			if( it != g_spotLightParameterMap.end() )
			{
				return SpotLightParameterInfo( attribute, (*it).second );
			}
		}
	}
	return {};
}

ScenePlug *SpotLightTool::scenePlug()
{
	return getChild<ScenePlug>( g_firstPlugIndex );
}

const ScenePlug *SpotLightTool::scenePlug() const
{
	return getChild<ScenePlug>( g_firstPlugIndex );
}

void SpotLightTool::connectToViewContext()
{
	m_contextChangedConnection = view()->getContext()->changedSignal().connect(
		boost::bind( &SpotLightTool::contextChanged, this, ::_2 )
	);
}

void SpotLightTool::contextChanged( const InternedString &name )
{
	if(
		ContextAlgo::affectsSelectedPaths( name ) ||
		ContextAlgo::affectsLastSelectedPath( name ) ||
		!boost::starts_with( name.string(), "ui:" )
	)
	{
		m_selectionDirty = true;
		selectionChangedSignal()( *this );
		m_handleDirty = true;
	}
}

bool SpotLightTool::affectsHandle( const Plug *input ) const
{
	return input == scenePlug()->transformPlug();
}

void SpotLightTool::updateHandle( float rasterScale )
{
	Context::Scope scopedContext( view()->getContext() );
	m_handle->setTransform( scenePlug()->fullTransform( selection().back().first ) );

	m_handle->setEnabled( selectionEditable() );
	m_handle->setRasterScale( rasterScale );
}

void SpotLightTool::plugDirtied( const Plug *plug )
{

	// Note : This method is called not only when plugs
	// belonging to the TransformTool are dirtied, but
	// _also_ when plugs belonging to the View are dirtied.

	if(
		plug == activePlug() ||
		plug == scenePlug()->childNamesPlug() ||
		plug == scenePlug()->transformPlug() ||
		( plug->ancestor<View>() && plug == view()->editScopePlug() )
	)
	{
		m_selectionDirty = true;
		if( !m_dragging )
		{
			selectionChangedSignal()( *this );
		}
		m_handleDirty = true;
	}

	if( plug == activePlug() )
	{
		if( activePlug()->getValue() )
		{
			m_preRenderConnection = view()->viewportGadget()->preRenderSignal().connect(
				boost::bind( &SpotLightTool::preRender, this )
			);
		}
		else
		{
			m_preRenderConnection.disconnect();
			m_handle->setVisible( false );
		}
	}
}

void SpotLightTool::preRender()
{
	if( !m_dragging )
	{
		updateSelection();
	}

	if( !selectionEditable() )
	{
		m_handle->setVisible( false );
		return;
	}

	m_handle->setVisible( true );

	if( m_handleDirty )
	{
		updateHandle( 75 );
		m_handleDirty = false;
	}
}

void SpotLightTool::updateSelection() const
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

	PathMatcher selectedPaths = ContextAlgo::getSelectedPaths( view()->getContext() );

	if( selectedPaths.isEmpty() )
	{
		return;
	}

	ScenePlug::ScenePath lastSelectedPath = ContextAlgo::getLastSelectedPath( view()->getContext() );
	assert( selectedPaths.match( lastSelectedPath ) & IECore::PathMatcher::ExactMatch );

	ScenePlug::PathScope scope( view()->getContext() );

	for( PathMatcher::Iterator it = selectedPaths.begin(), eIt = selectedPaths.end(); it != eIt; ++it )
	{
		SpotLightParameterResult result = spotLightParameter( scene, *it );
		if( result )
		{
			auto inspector = new GafferSceneUI::Private::ParameterInspector(
				const_cast<ScenePlug *>( scene ),
				const_cast<Plug *>( view()->editScopePlug() ),
				(*result).first,
				(*result).second
			);

			scope.setPath( &(*it) );
			m_selection.push_back( { *it, inspector->inspect() } );
		}
		else
		{
			m_selection.push_back( { *it, nullptr } );
		}
		// if( *it == lastSelectedPath )
		// {
		// 	/// \todo Is this really necessary? Shouldn't they already be the same?
		// 	lastSelectedPath = m_selection.back().first;
		// }
	}

	// Sort by `source()`, ensuring `lastInspection` comes first
	// in its group (so it survives deduplication).

	std::sort(
		m_selection.begin(),
		m_selection.end(),
		[&lastSelectedPath]( const SelectionItem &a, SelectionItem &b
		)
		{
			const auto ta = sourceOrNull( a );
			const auto tb = sourceOrNull( b );
			if( ta < tb )
			{
				return true;
			}
			else if( tb < ta )
			{
				return false;
			}
			return ( a.first != lastSelectedPath ) < ( b.first != lastSelectedPath );
		}
	);

	// Deduplicate by `source()`, being careful to avoid removing
	// items in EditScopes where the plug hasn't been created yet.
	auto last = std::unique(
		m_selection.begin(),
		m_selection.end(),
		[]( const SelectionItem &a, const SelectionItem &b )
		{
			const auto ta = sourceOrNull( a );
			const auto tb = sourceOrNull( b );
			return ta && tb && ta == tb;
		}
	);
	m_selection.erase( last, m_selection.end() );

	// Move `lastInspection` to the end
	auto lastInspectionIt = std::find_if(
		m_selection.begin(),
		m_selection.end(),
		[&lastSelectedPath]( const SelectionItem &x )
		{
			return x.first == lastSelectedPath;
		}
	);

	if( lastInspectionIt != m_selection.end() )
	{
		std::swap( m_selection.back(), *lastInspectionIt );
	}
	else
	{
		// We shouldn't get here, because ContextAlgo guarantees that lastSelectedPath is
		// contained in selectedPaths, and we've preserved lastSelectedPath through our
		// uniquefication process. But we could conceivably get here if an extension has
		// edited "ui:scene:selectedPaths" directly instead of using ContextAlgo,
		// in which case we emit a warning instead of crashing.
		IECore::msg( IECore::Msg::Warning, "SpotLightTool::updateSelection", "Last selected path not included in selection" );
	}
}

RunTimeTypedPtr SpotLightTool::dragBegin()
{
	m_drag.clear();

	Selection currentSelection = selection();

	for( const auto &[path, inspection] : currentSelection )
	{
		m_drag.push_back( Angle( inspection ) );
	}

	m_dragging = true;

	return nullptr;
}

bool SpotLightTool::dragMove( Gadget *gadget, const DragDropEvent &event )
{
	if( m_drag.empty() )
	{
		return true;
	}

	UndoScope undoScope(
		m_drag.front().inspection()->acquireEdit()->ancestor<ScriptNode>(),
		UndoScope::Enabled,
		undoMergeGroup()
	);

	float scaling = static_cast<AngleHandle *>( gadget )->scaling( event );
	for( auto &a : m_drag )
	{
		a.apply( scaling );
	}

	return true;
}

bool SpotLightTool::dragEnd()
{
	m_dragging = false;
	m_mergeGroupId++;
	selectionChangedSignal()( *this );

	return false;
}

std::string SpotLightTool::undoMergeGroup() const
{
	return fmt::format( "SpotLightTool{}{}", fmt::ptr( this ), m_mergeGroupId );
}

//////////////////////////////////////////////////////////////////////////
// SpotLightTool::Angle
//////////////////////////////////////////////////////////////////////////

SpotLightTool::Angle::Angle(GafferSceneUI::Private::Inspector::ResultPtr inspection)
	: m_inspection( inspection )
{

}

GafferSceneUI::Private::Inspector::ResultPtr SpotLightTool::Angle::inspection() const
{
	return m_inspection;
}

bool SpotLightTool::Angle::canApply()
{
	return m_inspection ? m_inspection->editable() : false;
}

void SpotLightTool::Angle::apply( const float scale )
{
	if( m_inspection )
	{
		if( auto floatPlug = runTimeCast<FloatPlug>( m_inspection->acquireEdit() ) )
		{
			if( !m_originalAngle )
			{
				// First call to `apply()`.
				m_originalAngle = floatPlug->getValue();
			}

			floatPlug->setValue( (*m_originalAngle) * scale );
		}
		else
		{
			msg( Msg::Warning, "SpotLightTool", "Cone angle parameters must be floats.");
		}
	}
}