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

import unittest
import imath
import math

import IECore

import Gaffer
import GafferTest
import GafferUITest
import GafferScene
import GafferSceneUI
import GafferSceneTest

class SpotLightToolTest( GafferUITest.TestCase ) :

	def setUp( self ) :

		GafferUITest.TestCase.setUp( self )

		GafferSceneUI.SpotLightTool.registerSpotLight(
			IECore.InternedString( "light" ),
			"testLight",
			"coneAngle"
		)


	def testSelection( self ) :

		script = Gaffer.ScriptNode()

		script["light"] = GafferSceneTest.TestLight()

		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["light"]["out"] )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )

		tool = GafferSceneUI.SpotLightTool( view )
		tool["active"].setValue( True )

		self.assertEqual( len( tool.selection() ), 0 )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/group/light"] ) )
		self.assertEqual( len( tool.selection() ), 1 )
		paths = [ path for path, inspection in tool.selection() ]
		self.assertIn( "/group/light", paths )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/group" ] ) )
		self.assertEqual( len( tool.selection() ), 1 )
		paths = [ path for path, inspection in tool.selection() ]
		self.assertIn( "/group", paths )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/group", "/group/light" ] ) )
		self.assertEqual( len( tool.selection() ), 2 )
		paths = [ path for path, inspection in tool.selection() ]
		self.assertIn( "/group", paths )
		self.assertIn( "/group/light", paths )

	def testHandleTransform( self ) :

		script = Gaffer.ScriptNode()

		script["light"] = GafferSceneTest.TestLight()
		script["light"]["transform"]["rotate"]["y"].setValue( 90 )

		script["light2"] = GafferSceneTest.TestLight()
		script["light2"]["transform"]["translate"]["z"].setValue( 3 )

		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["light"]["out"] )
		script["group"]["in"][1].setInput( script["light2"]["out"] )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/group/light"] ) )

		tool = GafferSceneUI.SpotLightTool( view )
		tool["active"].setValue( True )

		self.assertTrue(
			tool.handleTransform().equalWithAbsError(
				imath.M44f().rotate( imath.V3f( 0, math.pi / 2, 0 ) ),
				.000001
			)
		)

		script["light"]["transform"]["translate"]["x"].setValue( 10 )

		self.assertTrue(
			tool.handleTransform().equalWithAbsError(
				imath.M44f().translate( imath.V3f( 10, 0, 0 ) ).rotate( imath.V3f( 0, math.pi / 2, 0 ) ),
				.000001
			)
		)

		script["group"]["transform"]["translate"]["y"].setValue( 10 )

		self.assertTrue(
			tool.handleTransform().equalWithAbsError(
				imath.M44f().translate( imath.V3f( 10, 10, 0 ) ).rotate( imath.V3f( 0, math.pi / 2, 0 ) ),
				.000001
			)
		)

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/group/light", "/group/light1" ] ) )
		GafferSceneUI.ContextAlgo.setLastSelectedPath( view.getContext(), "/group/light1" )

		self.assertTrue(
			tool.handleTransform().equalWithAbsError(
				imath.M44f().translate( imath.V3f( 0, 10, 3 ) ),
				.000001
			)
		)

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/group" ] ) )
		self.assertRaises( RuntimeError, tool.handleTransform )

	def testSelectionChangedSignal( self ) :

		script = Gaffer.ScriptNode()
		script["light"] = GafferSceneTest.TestLight()

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["light"]["out"] )

		tool = GafferSceneUI.SpotLightTool( view )
		tool["active"].setValue( True )

		cs = GafferTest.CapturingSlot( tool.selectionChangedSignal() )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/light" ]  ) )
		self.assertTrue( len( cs ) )
		self.assertEqual( cs[0][0], tool )

	def testSelectionEditable( self ) :

		script = Gaffer.ScriptNode()
		script["light"] = GafferSceneTest.TestLight()

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["light"]["out"] )

		tool = GafferSceneUI.SpotLightTool( view )
		tool["active"].setValue( True )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/light"] ) )

		self.assertTrue( tool.selectionEditable() )

	def testChangeAngle( self ) :

		script = Gaffer.ScriptNode()

		script["light"] = GafferSceneTest.TestLight()

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["light"]["out"] )

		tool = GafferSceneUI.SpotLightTool( view )
		tool["active"].setValue( True )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/light" ] ) )

		with Gaffer.UndoScope( script ) :
			tool.setAngle( 10 )

		self.assertEqual( script["light"]["parameters"]["coneAngle"].getValue(), 10 )

		with Gaffer.UndoScope( script ) :
			tool.setAngle( 20 )

		self.assertEqual( script["light"]["parameters"]["coneAngle"].getValue(), 20 )

		script.undo()
		self.assertEqual( script["light"]["parameters"]["coneAngle"].getValue(), 10 )

		script.undo()
		self.assertEqual( script["light"]["parameters"]["coneAngle"].getValue(), 0 )

	def testSelectionEditable( self ) :

		script = Gaffer.ScriptNode()

		script["light"] = GafferSceneTest.TestLight()

		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["light"]["out"] )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )

		tool = GafferSceneUI.SpotLightTool( view )
		tool["active"].setValue( True )

		self.assertFalse( tool.selectionEditable() )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/group/light"] ) )
		self.assertTrue( tool.selectionEditable() )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/group/light", "/group"] ) )
		self.assertFalse ( tool.selectionEditable() )


if __name__ == "__main__" :
	unittest.main()