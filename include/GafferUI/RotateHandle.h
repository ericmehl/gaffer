//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2017, John Haddon. All rights reserved.
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

#ifndef GAFFERUI_ROTATEHANDLE_H
#define GAFFERUI_ROTATEHANDLE_H

#include "GafferUI/Handle.h"

IECORE_PUSH_DEFAULT_VISIBILITY
#include "OpenEXR/ImathEuler.h"
IECORE_POP_DEFAULT_VISIBILITY

namespace GafferUI
{

class GAFFERUI_API RotateHandle : public Handle
{

	public :

		RotateHandle( Style::Axes axes );
		~RotateHandle() override;

		IE_CORE_DECLARERUNTIMETYPEDEXTENSION( GafferUI::RotateHandle, RotateHandleTypeId, Handle );

		void setAxes( Style::Axes axes );
		Style::Axes getAxes() const;

		// Returns a vector where each component is 0 or 1,
		// indicating whether or not the handle will produce
		// rotation in that axis.
		Imath::V3i axisMask() const;

		// Measured in radians
		Imath::Eulerf rotation( const DragDropEvent &event ) const;

	protected :

		void renderHandle( const Style *style, Style::State state ) const override;
		void dragBegin( const DragDropEvent &event ) override;

	private :

		bool dragMove( const DragDropEvent &event );
		bool mouseMove( const ButtonEvent &event );
		Imath::V3f pointOnSphere( const IECore::LineSegment3f &line ) const;

		Style::Axes m_axes;
		// For X, Y and Z handles.
		PlanarDrag m_drag;
		float m_rotation;
		// For XYZ handle.
		Imath::M44f m_dragBeginWorldTransform;
		Imath::V3f m_dragBeginPointOnSphere;
		Imath::V3f m_highlightVector;

};

IE_CORE_DECLAREPTR( RotateHandle )

typedef Gaffer::FilteredChildIterator<Gaffer::TypePredicate<RotateHandle> > RotateHandleIterator;
typedef Gaffer::FilteredRecursiveChildIterator<Gaffer::TypePredicate<RotateHandle> > RecursiveRotateHandleIterator;

} // namespace GafferUI

#endif // GAFFERUI_ROTATEHANDLE_H
