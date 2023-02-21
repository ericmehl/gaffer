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

import IECore

import Gaffer
import GafferScene
import GafferUITest
import GafferSceneUI
import GafferSceneTest

class MetadataValueParameterInspectorTest( GafferUITest.TestCase ) :

	def setUp( self ) :

		GafferUITest.TestCase.setUp( self )

		Gaffer.Metadata.registerValue( "light:testLight", "coneAngleParameter", "coneAngle" )

	@staticmethod
	def __inspect( scene, path, metadataParameter, editScope=None, attributePattern = "light" ) :

		editScopePlug = Gaffer.Plug()
		editScopePlug.setInput( editScope["enabled"] if editScope is not None else None )
		inspector = GafferSceneUI.Private.MetadataValueParameterInspector(
			scene, editScopePlug, attributePattern, metadataParameter
		)
		with Gaffer.Context() as context :
			context["scene:path"] = IECore.InternedStringVectorData( path.split( "/" )[1:] )
			return inspector.inspect()

	def __assertExpectedResult( self, result, source, sourceType, editable, nonEditableReason = "", edit = None, editWarning = "" ) :

		self.assertEqual( result.source(), source )
		self.assertEqual( result.sourceType(), sourceType )
		self.assertEqual( result.editable(), editable )

		if editable :

			self.assertEqual( nonEditableReason, "" )
			self.assertEqual( result.nonEditableReason(), "" )

			acquiredEdit = result.acquireEdit()
			self.assertIsNotNone( acquiredEdit )
			if result.editScope() :
				self.assertTrue( result.editScope().isAncestorOf( acquiredEdit ) )

			if edit is not None :
				self.assertEqual(
					acquiredEdit.fullName() if acquiredEdit is not None else "",
					edit.fullName() if edit is not None else ""
				)

			self.assertEqual( result.editWarning(), editWarning )

		else :

			self.assertIsNone( edit )
			self.assertEqual( editWarning, "" )
			self.assertEqual( result.editWarning(), "" )
			self.assertNotEqual( nonEditableReason, "" )
			self.assertEqual( result.nonEditableReason(), nonEditableReason )
			self.assertRaises( RuntimeError, result.acquireEdit )

	def test( self ) :

		s = Gaffer.ScriptNode()

		s["light"] = GafferSceneTest.TestLight( type = GafferSceneTest.TestLight.LightType.Spot )

		s["attributes"] = GafferScene.CustomAttributes()
		s["attributes"]["in"].setInput( s["light"]["out"] )
		s["attributes"]["attributes"].addChild( Gaffer.NameValuePlug( "badAttribute", IECore.FloatData( 1.0 ), flags = Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic ) )

		s["group"] = GafferScene.Group()
		s["group"]["in"][0].setInput( s["attributes"]["out"] )

		s["editScope"] = Gaffer.EditScope()
		s["editScope"].setup( s["group"]["out"] )
		s["editScope"]["in"].setInput( s["group"]["out"] )

		SourceType = GafferSceneUI.Private.Inspector.Result.SourceType

		inspection = self.__inspect( s["editScope"]["out"], "/group/light", "coneAngleParameter" )
		self.__assertExpectedResult(
			inspection,
			source = s["light"]["parameters"]["coneAngle"],
			sourceType = SourceType.Other,
			editable = True,
			edit = s["light"]["parameters"]["coneAngle"]
		)

		inspection = self.__inspect( s["editScope"]["out"], "/group/light", "badParameter" )
		self.assertIsNone( inspection )

		inspection = self.__inspect( s["editScope"]["out"], "/group/light", "coneAngleParameter", attributePattern = "notAMatch" )
		self.assertIsNone( inspection )

		inspection = self.__inspect( s["editScope"]["out"], "/group/light", "badParameter", attributePattern = "notAMatch" )
		self.assertIsNone( inspection )

		inspection = self.__inspect( s["editScope"]["out"], "/group/light", "nonexistentParameter", attributePattern = "badAttribute" )
		self.assertIsNone( inspection )

		inspection = self.__inspect( s["editScope"]["out"], "/group/light", "coneAngleParameter", s["editScope"] )

		self.assertIsNone(
			GafferScene.EditScopeAlgo.acquireParameterEdit(
				s["editScope"], "/group/light", "light", ( "", "coneAngle" ), createIfNecessary = False
			)
		)
		self.__assertExpectedResult(
			inspection,
			source = s["light"]["parameters"]["coneAngle"],
			sourceType = SourceType.Upstream,
			editable = True
		)

		edit = inspection.acquireEdit()
		self.assertIsNotNone( edit )
		self.assertEqual(
			edit,
			GafferScene.EditScopeAlgo.acquireParameterEdit(
				s["editScope"], "/group/light", "light", ( "", "coneAngle" ), createIfNecessary = False
			)
		)


if __name__ == "__main__" :
	unittest.main()