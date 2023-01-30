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

#ifndef GAFFERSCENEUI_SPOTLIGHTTOOL_H
#define GAFFERSCENEUI_SPOTLIGHTTOOL_H

#include "GafferSceneUI/Private/ParameterInspector.h"

#include "GafferSceneUI/Export.h"
#include "GafferSceneUI/SelectionTool.h"
#include "GafferSceneUI/TypeIds.h"

#include "GafferScene/ScenePlug.h"

#include "GafferUI/Handle.h"

#include "Gaffer/EditScope.h"

#include "IECoreScene/ShaderNetwork.h"

#include <utility>
#include <unordered_map>

namespace GafferSceneUI
{

IE_CORE_FORWARDDECLARE( SceneView )

class GAFFERSCENEUI_API SpotLightTool : public GafferSceneUI::SelectionTool
{

	public :

		SpotLightTool( SceneView *view, const std::string &name = defaultName<SpotLightTool>() );
		~SpotLightTool() override;

		GAFFER_NODE_DECLARE_TYPE( GafferSceneUI::SpotLightTool, SpotLightToolTypeId, SelectionTool );

		using SelectionItem = std::pair<GafferScene::ScenePlug::ScenePath, GafferSceneUI::Private::Inspector::ResultPtr>;
		using Selection = std::vector<SelectionItem>;
		const Selection selection() const;

		/// Returns true only if the selection is non-empty
		/// and every item is editable.
		bool selectionEditable() const;

		using SelectionChangedSignal = Gaffer::Signals::Signal<void (SpotLightTool &)>;
		SelectionChangedSignal &selectionChangedSignal();

		/// Returns the transform of the handle. Throws
		/// if the selection is invalid because then the
		/// transform would be meaningless. This is
		/// exposed primarily for the unit tests.
		Imath::M44f handleTransform();

		/// Changes the spot light cone angle as if the handles
		/// had been dragged interactively. Exists mainly for use
		/// in the unit tests.
		void setAngle( const float angle );

		static bool registerSpotLight(
			IECore::InternedString shaderAttribute,
			const std::string &shaderName,
			const std::string &coneAngleParameter
		);

	private :

		using SpotLightParameterMap = std::unordered_map<std::string, IECoreScene::ShaderNetwork::Parameter>;
		using SpotLightParameterInfo = std::pair<IECore::InternedString, IECoreScene::ShaderNetwork::Parameter>;
		using SpotLightParameterResult = std::optional<SpotLightParameterInfo>;
		static SpotLightParameterMap g_spotLightParameterMap;

		static const std::string spotLightParameterKey( IECore::InternedString attribute, const std::string &shaderName );
		static SpotLightParameterResult spotLightParameter( const GafferScene::ScenePlug *scene, const GafferScene::ScenePlug::ScenePath &path );

		// The guts of the cone angle transform logic. This is factored out
		// of the drag handling so it can be called from `setAngle()`.
		struct Angle
		{

			Angle( GafferSceneUI::Private::Inspector::ResultPtr inspection );

			GafferSceneUI::Private::Inspector::ResultPtr inspection() const;

			bool canApply();
			void apply( const float scale );

			private :

				GafferSceneUI::Private::Inspector::ResultPtr m_inspection;

				std::optional<float> m_originalAngle;
		};

		GafferScene::ScenePlug *scenePlug();
		const GafferScene::ScenePlug *scenePlug() const;

		void connectToViewContext();
		void contextChanged( const IECore::InternedString &name );
		bool affectsHandle( const Gaffer::Plug *input ) const;
		void updateHandle( float rasterScale );
		void plugDirtied( const Gaffer::Plug *plug );
		void preRender();
		void updateSelection() const;

		IECore::RunTimeTypedPtr dragBegin();
		bool dragMove( GafferUI::Gadget *gadget, const GafferUI::DragDropEvent &event );
		bool dragEnd();

		std::string undoMergeGroup() const;

		GafferUI::HandlePtr m_handle;
		bool m_handleDirty;
		mutable Selection m_selection;
		mutable bool m_selectionDirty;

		SelectionChangedSignal m_selectionChangedSignal;

		bool m_dragging;

		Gaffer::Signals::ScopedConnection m_contextChangedConnection;
		Gaffer::Signals::ScopedConnection m_preRenderConnection;

		std::vector<Angle> m_drag;
		int m_mergeGroupId;

		static ToolDescription<SpotLightTool, SceneView> g_toolDescription;
		static size_t g_firstPlugIndex;
};

IE_CORE_DECLAREPTR( SpotLightTool )

}  // namespace GafferSceneUI

#endif // GAFFERSCENEUI_SPOTLIGHTTOOL_H
