##########################################################################
#
#  Copyright (c) 2026, Cinesite VFX Ltd. All rights reserved.
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

import os
import functools

import imath

import IECore

import Gaffer
import GafferUI

class BreadCrumbsWidget( GafferUI.Widget ) :

	def __init__( self, path, **kw ) :

		self.__row = GafferUI.ListContainer( GafferUI.ListContainer.Orientation.Horizontal, borderWidth = 1, spacing = 4 )

		GafferUI.Widget.__init__( self, self.__row, **kw )

		self.__row._qtWidget().setObjectName( "gafferBreadCrumbs" )

		with self.__row :
			self.__pathButtonContainer = GafferUI.ListContainer( GafferUI.ListContainer.Orientation.Horizontal, spacing = 4 )

			self.__textWidget = GafferUI.TextWidget( toolTip =
				"**Right-click** to navigate to children."
				"<br>**<kbd>Down</kbd>** for contents menu."
				"<br>**<kbd>Up</kbd>** to navigate to container."
				"<br>**<kbd>Tab</kbd>** to auto-complete."
				"<br>**<kbd>Home</kbd>** to return to root."
			)

			self.__textWidget.keyPressSignal().connect( Gaffer.WeakMethod( self.__textKeyPress ) )
			self.__textWidget.buttonPressSignal().connect( Gaffer.WeakMethod( self.__textButtonPress ) )
			self.__textChangedConnection = self.__textWidget.textChangedSignal().connect( Gaffer.WeakMethod( self.__textChanged ), scoped = True )

		self.__popupMenu = None

		self.__path = None
		self.setPath( path )

	def setPath( self, path ) :

		self.__path = path
		self.__path.pathChangedSignal().connect( Gaffer.WeakMethod( self.__pathChanged, fallbackResult = None ) )
		self.updateWidgets()

	def getPath( self ) :

		return self.__path

	def updateWidgets( self ) :

		# \todo Reduce, Reuse, Recycle instead of clearing and recreating all buttons
		while len( self.__pathButtonContainer ) > 0 :
			self.__pathButtonContainer.remove( self.__pathButtonContainer[0] )

		path = self.__path.copy()
		path.setFromString( path.root() )

		for w in self.__pathWidgets( path.copy() ) :
			self.__pathButtonContainer.append( w )

		for i in range( 0, len( self.__path ) ) :
			path.append( self.__path[i] )
			if path.isValid() :
				for w in self.__pathWidgets( path.copy() ) :
					self.__pathButtonContainer.append( w )
			else :
				break

		self.__textWidget.setText( path[-1] if ( len( path ) > 0 and not path.isValid() ) else "" )

	def __pathWidgets( self, path ) :

		pathButton = GafferUI.Button(
			path[-1] if len( path ) > 0 else "",
			image = "home.png" if len( path ) == 0 else None,
			hasFrame = False,
			highlightOnOver = False,
			toolTip = "**Click** to navigate to box." + ( "<br>**Right-click** to navigate to path sibling." if len( path ) > 0 else "" )
		)
		pathButton.buttonPressSignal().connect( functools.partial( Gaffer.WeakMethod( self.__pathButtonPress ), path ) )
		pathButton.enterSignal().connect( Gaffer.WeakMethod( self.__pathButtonEnter ) )
		pathButton.leaveSignal().connect( Gaffer.WeakMethod( self.__pathButtonLeave ) )

		return ( pathButton, GafferUI.Label( "/" ) )

	def __pathButtonPress( self, path, button, event ) :

		if event.buttons == GafferUI.ButtonEvent.Buttons.Right and len( path ) > 0 :
			parentPath = path.copy()
			del parentPath[-1]
			self.__popupListing( parentPath, button )
			return True
		elif event.button == event.Buttons.Left :
			self.__path[:] = path[:]
			return True

		return False

	def __pathButtonEnter( self, button ) :

		button.setHasFrame( True )

	def __pathButtonLeave( self, button ) :

		button.setHasFrame( False )

	def __textChanged( self, textWidget ) :

		text = textWidget.getText().replace( ".", "/" )

		if text == "" :
			return

		newPath = self.__path.copy()
		newPath.setFromString( str( self.__path ) + "/" + text )
		parentPath = newPath.copy()
		del parentPath[-1]

		passesFilter = True
		if self.__path.getFilter() is not None :
			passesFilter = self.__path.getFilter().filter( [newPath] ) == [newPath]

		if newPath.isValid() and passesFilter and  (
			( len( text ) > 0 and text[-1] == "/" ) or
			len( newPath ) == 0 or
			len( [ i for i in parentPath.children() if i[-1].startswith( newPath[-1] ) ] ) == 1
		) :
			self.__path[:] = newPath[:]

	def __setPathEntry( self, path ) :

		if path == self.__path :
			return

		newPath = self.__path.copy()
		newPath.setFromString( newPath.root() )
		pathLength = len( path )
		for i in range( 0, max( pathLength, len( self.__path ) ) ) :
			newPath.append( path[i] if i < pathLength else self.__path[i] )

		newPath.truncateUntilValid()
		self.__path[:] = newPath[:]

		self.__textWidget.grabFocus()

	def __textKeyPress( self, widget, event ) :

		if not self.__textWidget.getEditable() :
			return False

		if event.key == "Backspace" and self.__textWidget.getText() == "" and len( self.__path ) > 0 :
			t = self.__path[-1]
			del self.__path[-1]
			with Gaffer.Signals.BlockedConnection( self.__textChangedConnection ) :
				self.__textWidget.setText( t )
			return True

		elif event.key=="Tab" :
			self.__tabComplete()
			return True

		elif event.key == "Down" :
			self.__popupListing( self.__path, self.__textWidget )
			return True

		elif event.key == "Up" and len( self.__path ) > 0 :
			if self.__textWidget.getText() != "" :
				self.__textWidget.setText( "" )
			else :
				del self.__path[-1]
			return True

		elif event.key == "Home" :
			self.__path.setFromString( self.__path.root() )
			return True

		return False

	def __textButtonPress( self, widget, event ) :

		if event.buttons == GafferUI.ButtonEvent.Buttons.Right :
			self.__popupListing( self.__path, widget )
			return True

		return False

	def __tabComplete( self ) :

		position = self.__textWidget.getCursorPosition()
		text = self.__textWidget.getText()

		truncatedPath = self.__path.copy()

		if position > 0 :
			truncatedPath.append( text[:position] )

		if len( truncatedPath ) :
			matchStart = truncatedPath[-1]
			del truncatedPath[-1]
		else :
			matchStart = ""

		matches = [ x[-1] for x in truncatedPath.children() if x[-1].startswith( matchStart ) ]
		match = os.path.commonprefix( matches )

		if match :
			if len( matches ) == 1 :
				self.__path[:] = truncatedPath[:] + [ match ]
			else :
				self.__textWidget.setText( match )
				self.__popupListing( self.__path, self.__textWidget, match )

			self.__textWidget.setCursorPosition( len( self.__textWidget.getText() ) )

	def __popupListing( self, path, parentWidget, prefix = "" ) :

		menuDefinition = IECore.MenuDefinition()

		sortedChildren = sorted( path.children(), key = lambda v : v[-1] )

		for childPath in [ i for i in sortedChildren if i[-1].startswith( prefix ) ] :
			menuDefinition.append(
				"/" + childPath[-1],
				{
					"command" : functools.partial( Gaffer.WeakMethod( self.__setPathEntry ), childPath ),
				}
			)

		if menuDefinition.size() == 0 :
			menuDefinition.append(
				"/No viewable children",
				{
					"active" : False,
				}
			)

		bound = parentWidget.bound()
		xOffset = 0
		if isinstance( parentWidget, GafferUI.TextWidget ) :
			xOffset = parentWidget._qtWidget().cursorRect().left()

		self.__popupMenu = GafferUI.Menu( menuDefinition )
		self.__popupMenu.popup(
			parent = parentWidget,
			position = imath.V2i( bound.min().x + xOffset, bound.max().y ),
			forcePosition=True,
			grabFocus=True
		)

	def __pathChanged( self, path ) :

		self.updateWidgets()
