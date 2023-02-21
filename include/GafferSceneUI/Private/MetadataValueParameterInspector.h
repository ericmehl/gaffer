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

#ifndef PRIVATE_METADATAVALUEPARAMETERINSPECTOR_H
#define PRIVATE_METADATAVALUEPARAMETERINSPECTOR_H

#include "GafferSceneUI/Export.h"

#include "GafferSceneUI/Private/ParameterInspector.h"

namespace GafferSceneUI
{

namespace Private
{

/// Inspects a parameter whose name is given by looking up the value of `metadataKey`.
/// The shader this parameter is inspected from is the first shader whose attribute
/// name matches `attributePattern`. Gaffer's standard pattern matching is used to
/// match attribute names to `attributePattern`.

class GAFFERSCENEUI_API MetadataValueParameterInspector : public ParameterInspector
{

	public :
		MetadataValueParameterInspector(
			const GafferScene::ScenePlugPtr &scene,
			const Gaffer::PlugPtr &editScope,
			const std::string &attributePattern,
			const IECore::InternedString &metadataKey
		);

		IE_CORE_DECLAREMEMBERPTR( MetadataValueParameterInspector );

	protected :

		IECore::InternedString attributeToQuery( const GafferScene::ScenePlug *scene ) const override;
		const IECoreScene::ShaderNetwork::Parameter parameterToQuery( const GafferScene::ScenePlug *scene ) const override;

	private :

		const std::string m_attributePattern;
		const IECore::InternedString m_metadataKey;

};

IE_CORE_DECLAREPTR( MetadataValueParameterInspector );

}  // namespace Private

}  // namespace GafferSceneUI

#endif // PRIVATE_METADATAVALUEPARAMETERINSPECTOR_H
