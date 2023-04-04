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
import GafferUI
import GafferTest
import GafferUITest
import GafferScene
import GafferSceneUI
import GafferSceneTest

class LightToolTest( GafferUITest.TestCase ) :

	def setUp( self ) :

		GafferUITest.TestCase.setUp( self )

		Gaffer.Metadata.registerValue( "light:testLight", "type", "spot" )
		Gaffer.Metadata.registerValue( "light:testLight", "coneAngleParameter", "coneAngle" )
		Gaffer.Metadata.registerValue( "light:testLight", "penumbraAngleParameter", "penumbraAngle" )

	def testSelection( self ) :

		script = Gaffer.ScriptNode()

		script["light1"] = GafferSceneTest.TestLight()
		script["light2"] = GafferSceneTest.TestLight()

		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["light1"]["out"] )
		script["group"]["in"][1].setInput( script["light2"]["out"] )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )

		tool = GafferSceneUI.LightTool( view )
		tool["active"].setValue( True )

		self.assertTrue( tool.selection().isEmpty() )

		for selection in [ [ "/group/light" ], ["/group/light", "/group/light1" ], [ "/group/light"] ] :
			with self.subTest( selection, selection = selection ) :
				GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( selection ) )
				s = tool.selection()
				self.assertEqual( len( s.paths() ), len( selection ) )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [] ) )
		s = tool.selection()
		self.assertTrue( s.isEmpty() )

	def testHandleVisibility( self ) :

		script = Gaffer.ScriptNode()

		script["spotLight1"] = GafferSceneTest.TestLight( type = GafferSceneTest.TestLight.LightType.Spot )
		script["spotLight2"] = GafferSceneTest.TestLight( type = GafferSceneTest.TestLight.LightType.Spot )
		script["light1"] = GafferSceneTest.TestLight()

		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["spotLight1"]["out"] )
		script["group"]["in"][1].setInput( script["spotLight2"]["out"] )
		script["group"]["in"][2].setInput( script["light1"]["out"] )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )

		tool = GafferSceneUI.LightTool( view )
		tool["active"].setValue( True )

		handles = { v.getName() : v for v in view.viewportGadget()["HandlesGadget"].children() }

		self.assertIn( "coneAngleParameter", handles )
		self.assertIn( "penumbraAngleParameter", handles )

		# \todo Why do the handles come to us as `GraphComponent` objects instead of `Handle`?

		self.assertIsInstance( handles["coneAngleParameter"], GafferUI.Handle )
		self.assertIsInstance( handles["penumbraAngleParameter"], GafferUI.Handle )
		self.assertFalse( handles["coneAngleParameter"].getVisible() )
		self.assertFalse( handles["penumbraAngleParameter"].getVisible() )

	def testSelectionChangedSignal( self ) :

		script = Gaffer.ScriptNode()
		script["light"] = GafferSceneTest.TestLight()

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["light"]["out"] )

		tool = GafferSceneUI.LightTool( view )
		tool["active"].setValue( True )

		cs = GafferTest.CapturingSlot( tool.selectionChangedSignal() )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), IECore.PathMatcher( [ "/light" ]  ) )
		self.assertTrue( len( cs ) )
		self.assertEqual( cs[0][0], tool )


if __name__ == "__main__" :
	unittest.main()