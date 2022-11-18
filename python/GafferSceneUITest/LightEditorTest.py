##########################################################################
#
#  Copyright (c) 2022, Cinesite VFX Ltd. All rights reserved.
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

import IECore

import Gaffer
import GafferUI
from GafferUI import _GafferUI
import GafferScene
import GafferSceneUI
import GafferSceneTest
import GafferUITest


class LightEditorTest( GafferUITest.TestCase ) :

	def testMuteToggle( self ) :

		# Scene Hierarchy
		#  /
		#  /groupMute
		#  /groupMute/Mute
		#  /groupMute/UnMute
		#  /groupMute/None
		#  /groupUnMute
		#  /groupUnMute/Mute
		#  /groupUnMute/UnMute
		#  /groupUnMute/None
		#  /Mute
		#  /UnMute
		#  /None

		script = Gaffer.ScriptNode()

		script["parent"] = GafferScene.Parent()
		script["parent"]["parent"].setValue( "/" )

		for parent in [ "groupMute", "groupUnMute", None ] :
			for child in [ "Mute", "UnMute", None ] :
				childNode = GafferSceneTest.TestLight()
				childNode["name"].setValue( child or "None" )
				script.addChild( childNode )

				if parent is not None :
					if parent not in script :
						script[parent] = GafferScene.Group()
						script[parent]["name"].setValue( parent )
						script["parent"]["children"][len( script["parent"]["children"].children() ) - 1 ].setInput( script[parent]["out"] )

					script[parent]["in"][len( script[parent]["in"].children() ) - 1].setInput( childNode["out"] )
				else :
					script["parent"]["children"][len( script["parent"]["children"].children() ) - 1].setInput( childNode["out"] )

		def resetEditScope() :
			script["editScope"] = Gaffer.EditScope()
			script["editScope"].setup( script["parent"]["out"] )
			script["editScope"]["in"].setInput( script["parent"]["out"] )

			for state in ["Mute", "UnMute"] :

				# group*
				edit = GafferScene.EditScopeAlgo.acquireAttributeEdit(
					script["editScope"],
					f"/group{state}",
					"light:mute",
					createIfNecessary = True
				)
				edit["mode"].setValue(Gaffer.TweakPlug.Mode.Create)
				edit["value"].setValue( state == "Mute" )
				edit["enabled"].setValue( True )

				# group*/Mute
				edit = GafferScene.EditScopeAlgo.acquireAttributeEdit(
					script["editScope"],
					f"/group{state}/Mute",
					"light:mute",
					createIfNecessary = True
				)
				edit["mode"].setValue(Gaffer.TweakPlug.Mode.Create)
				edit["value"].setValue( True )
				edit["enabled"].setValue( True )

				# group*/UnMute
				edit = GafferScene.EditScopeAlgo.acquireAttributeEdit(
					script["editScope"],
					f"/group{state}/UnMute",
					"light:mute",
					createIfNecessary = True
				)
				edit["mode"].setValue(Gaffer.TweakPlug.Mode.Create)
				edit["value"].setValue( False )
				edit["enabled"].setValue( True )

				# light
				edit = GafferScene.EditScopeAlgo.acquireAttributeEdit(
					script["editScope"],
					f"/{state}",
					"light:mute",
					createIfNecessary = True
				)
				edit["mode"].setValue(Gaffer.TweakPlug.Mode.Create)
				edit["value"].setValue( state == "Mute" )
				edit["enabled"].setValue( True )

			script.setFocus( script["editScope"] )

		# Tests against a given state, which is a dictionary of the form :
		# { "sceneLocation" : ( attributesMuteValue, fullAttributesMuteValue ), ... }
		def testLightMuteAttribute( clickCount, toggleLocation, newStates ) :

			for location in [
				"/groupMute",
				"/groupMute/Mute",
				"/groupMute/UnMute",
				"/groupMute/None",
				"/groupUnMute",
				"/groupUnMute/Mute",
				"/groupUnMute/UnMute",
				"/groupUnMute/None",
				"/Mute",
				"/UnMute",
				"/None",
			] :
				attributes = script["editScope"]["out"].attributes( location )
				fullAttributes = script["editScope"]["out"].fullAttributes( location )

				muteAttribute, fullMuteAttribute = newStates[location]

				with self.subTest( f"(attributes) Toggle Click {clickCount} = {toggleLocation}", location = location ) :
					if muteAttribute is not None :
						self.assertIn( "light:mute", attributes )
						self.assertEqual( attributes["light:mute"].value, muteAttribute )
					else :
						self.assertNotIn( "light:mute", attributes )
				with self.subTest( f"(fullAttributes) Toggle Click {clickCount} = {toggleLocation}", location = location ) :
					if fullMuteAttribute is not None :
						self.assertIn( "light:mute", fullAttributes )
						self.assertEqual( fullAttributes["light:mute"].value, fullMuteAttribute)
					else :
						self.assertNotIn( "light:mute", fullAttributes )

		initialState = {
			"/groupMute" : ( True, True ),
			"/groupMute/Mute" : ( True, True ),
			"/groupMute/UnMute" : ( False, False ),
			"/groupMute/None" : ( None, True ),
			"/groupUnMute" : ( False, False ),
			"/groupUnMute/Mute" : ( True, True ),
			"/groupUnMute/UnMute" : ( False, False ),
			"/groupUnMute/None" : ( None, False ),
			"/Mute" : ( True, True ),
			"/UnMute" : (False, False ),
			"/None" : (None, None ),
		}

		resetEditScope()
		testLightMuteAttribute( 0, "none", initialState )

		# dictionary of the form :
		# {
		#     "toggleLocation" : (
		#         clickPoint,
		#         firstClickMuteState,
		#         secondClickMuteState
		#     ),
		#     ...
		# }
		toggles = {
			"/groupMute" : (
				imath.V3f(105, 25, 1),
				{
					"/groupMute" : ( None, None ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, None ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : ( False, False ),
					"/None" : ( None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : ( False, False ),
					"/None" : ( None, None ),
				}
			),
			"/groupMute/Mute" : (
				imath.V3f(105, 45, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( False, False ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( None, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				}
			),
			"/groupMute/UnMute" : (
				imath.V3f(105, 65, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( None, True ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				}
			),
			"/groupMute/None" : (
				imath.V3f(105, 90, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( False, False ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				}
			),
			"/groupUnMute" : (
				imath.V3f(105, 110, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( True, True ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, True ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( None, None ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, None ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				}
			),
			"/groupUnMute/Mute" : (
				imath.V3f(105, 135, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( None, False ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				}
			),
			"/groupUnMute/UnMute" : (
				imath.V3f(105, 155, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( True, True ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( None, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				}
			),
			"/groupUnMute/None" : (
				imath.V3f(105, 175, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( True, True ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				}
			),
			"/Mute" : (
				imath.V3f(105, 200, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( None, None ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				}
			),
			"/UnMute" : (
				imath.V3f(105, 220, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (True, True ),
					"/None" : (None, None ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : ( None, None ),
					"/None" : (None, None ),
				}
			),
			"/None" : (
				imath.V3f(105, 250, 1),
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (True, True ),
				},
				{
					"/groupMute" : ( True, True ),
					"/groupMute/Mute" : ( True, True ),
					"/groupMute/UnMute" : ( False, False ),
					"/groupMute/None" : ( None, True ),
					"/groupUnMute" : ( False, False ),
					"/groupUnMute/Mute" : ( True, True ),
					"/groupUnMute/UnMute" : ( False, False ),
					"/groupUnMute/None" : ( None, False ),
					"/Mute" : ( True, True ),
					"/UnMute" : (False, False ),
					"/None" : (None, None ),
				}
			),
		}

		event = GafferUI.ButtonEvent(
			GafferUI.ButtonEvent.Buttons.Left,
			GafferUI.ButtonEvent.Buttons.Left
		)

		for togglePath, toggleData in toggles.items() :

			clickPoint, firstNewStates, secondNewStates = toggleData
			event.line.p0 = clickPoint

			with GafferUI.Window() as w :
				resetEditScope()
				editor = GafferSceneUI.LightEditor( script )

			w.setVisible( True )

			editor._LightEditor__settingsNode["editScope"].setInput( script["editScope"]["out"] )

			widget = editor._LightEditor__pathListing

			_GafferUI._pathModelWaitForPendingUpdates(
				GafferUI._qtAddress( widget._qtWidget().model() )
			)
			widget.setExpansion( IECore.PathMatcher( [ "/groupMute", "/groupUnMute" ] ) )
			self.waitForIdle( 10000 )

			self.assertEqual( str( widget.pathAt( event.line.p0 ) ), togglePath )

			widget.buttonPressSignal()( widget, event )

			widget.buttonDoubleClickSignal()( widget, event )
			testLightMuteAttribute( 1, togglePath, firstNewStates )

			widget.buttonDoubleClickSignal()( widget, event )
			testLightMuteAttribute( 2, togglePath, secondNewStates )

			del widget, editor


if __name__ == "__main__" :
	unittest.main()
