##########################################################################
#
#  Copyright (c) 2023, Cinesite VFX Ltd. All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are
#  met:
#
#      * Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      * Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials provided with
#        the distribution.
#
#      * Neither the name of John Haddon nor the names of
#        any other contributors to this software may be used to endorse or
#        promote products derived from this software without specific prior
#        written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
#  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
##########################################################################

import imath
import sys

import IECore

import Gaffer
import GafferUI
import GafferSceneUI

Gaffer.Metadata.registerNode(

	GafferSceneUI.LightTool,

	"description",
	"""
	Tool for editing spot lights.
	""",

	"viewer:shortCut", "A",
	"order", 6,
	"viewer:shouldAutoActivate", False,

	"nodeToolbar:bottom:type", "GafferUI.StandardNodeToolbar.bottom",

	# "toolbarLayout:customWidget:SelectionWidget:widgetType", "GafferSceneUI.LightToolUI._SelectionWidget",
	# "toolbarLayout:customWidget:SelectionWidget:section", "Bottom",

	# # So we don't obscure the corner gnomon
	# "toolbarLayout:customWidget:LeftSpacer:widgetType", "GafferSceneUI.TransformToolUI._LeftSpacer",
	# "toolbarLayout:customWidget:LeftSpacer:section", "Bottom",
	# "toolbarLayout:customWidget:LeftSpacer:index", 0,

	# # So our widget doesn't center, add a stretchy spacer to the right
	# "toolbarLayout:customWidget:RightSpacer:widgetType", "GafferSceneUI.CropWindowToolUI._RightSpacer",
	# "toolbarLayout:customWidget:RightSpacer:section", "Bottom",
	# "toolbarLayout:customWidget:RightSpacer:index", -1,

)

# class _LeftSpacer( GafferUI.Spacer ) :

# 	def __init__( self, imageView, **kw ) :

# 		GafferUI.Spacer.__init__( self, size = imath.V2i( 40, 1 ), maximumSize = imath.V2i( 40, 1 ) )

# class _RightSpacer( GafferUI.Spacer ) :

# 	def __init__( self, imageView, **kw ) :

# 		GafferUI.Spacer.__init__( self, size = imath.V2i( 0, 0 ) )

# def _boldFormatter( graphComponents ) :

# 	with IECore.IgnoredExceptions( ValueError ) :
# 		## \todo Should the NameLabel ignore ScriptNodes and their ancestors automatically?
# 		scriptNodeIndex = [ isinstance( g, Gaffer.ScriptNode ) for g in graphComponents ].index( True )
# 		graphComponents = graphComponents[scriptNodeIndex+1:]

# 	return "<b>" + ".".join( g.getName() for g in graphComponents ) + "</b>"

# class _SelectionWidget( GafferUI.Frame ) :

# 	def __init__( self, tool, **kw ) :

# 		GafferUI.Frame.__init__( self, borderWidth = 4, **kw )

# 		self.__tool = tool

# 		with self :
# 			with GafferUI.ListContainer( orientation = GafferUI.ListContainer.Orientation.Horizontal ) as self.__infoRow :

# 				GafferUI.Image( "infoSmall.png" )
# 				GafferUI.Spacer( size = imath.V2i( 4 ), maximumSize = imath.V2i( 4 ) )
# 				self.__infoLabel = GafferUI.Label( "" )
# 				self.__nameLabel = GafferUI.NameLabel( graphComponent = None, numComponents = sys.maxsize )
# 				self.__nameLabel.setFormatter( _boldFormatter )
# 				self.__nameLabel.buttonDoubleClickSignal().connect( Gaffer.WeakMethod( self.__buttonDoubleClick ), scoped = False )

# 				GafferUI.Spacer( size = imath.V2i( 8 ), maximumSize = imath.V2i( 8 ) )
# 				GafferUI.Divider( orientation = GafferUI.Divider.Orientation.Vertical )
# 				GafferUI.Spacer( size = imath.V2i( 8 ), maximumSize = imath.V2i( 8 ) )

# 		self.__tool.selectionChangedSignal().connect( Gaffer.WeakMethod( self.__update, fallbackResult = None ), scoped = False )

# 		self.__update()

# 	def context( self ) :

# 		return self.ancestor( GafferUI.NodeToolbar ).getContext()

# 	def getToolTip( self ) :

# 		toolTip = GafferUI.Frame.getToolTip( self )
# 		if toolTip :
# 			return toolTip

# 		toolSelection = self.__tool.selection()
# 		if not toolSelection or not self.__tool.selectionEditable() :
# 			return ""

# 		result = ""
# 		script = toolSelection[0].editTarget()

# 	@GafferUI.LazyMethod( deferUntilPlaybackStops = True )
# 	def __update( self, *unused ) :

# 		if not self.__tool["active"].getValue() :
# 			# We're about to be made invisible so all our update
# 			# would do is cause unnecessary flickering in Qt's
# 			# redraw.
# 			return

# 		toolSelection = self.__tool.selection()

# 		if len( toolSelection ) :

# 			warnings = { i.editWarning() for p, i in toolSelection if i is not None }
# 			if not warnings and not self.__tool.selectionEditable() :
# 				warnings = { "Selection not editable" }

# 			if not self.__tool.selectionEditable() :
# 				self.__infoRow.setVisible( False )
# 			elif len( toolSelection ) == 1 and toolSelection[0][1].source() is not None :
# 				self.__infoRow.setVisible( True )
# 				self.__infoLabel.setText( "Editing " )
# 				self.__nameLabel.setGraphComponent( toolSelection[0][1].source() )
# 			else :
# 				self.__infoRow.setVisible( True )
# 				self.__infoLabel.setText( "Editing {} spot lights".format( len( toolSelection ) ) )
# 				self.__nameLabel.setGraphComponent( None )

# 	def __buttonDoubleClick( self, widget, event ) :

# 		if widget.getGraphComponent() is None :
# 			return False

# 		GafferUI.NodeEditor.acquire( widget.getGraphComponent().node(), floating = True )
