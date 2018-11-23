##########################################################################
#
#  Copyright (c) 2022 Hypothetical Inc. All rights reserved.
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
import inspect
import unittest

import IECore

import Gaffer
import GafferTest

class FileSystemPathPlugTest( GafferTest.StringPlugTest ) :

	def inOutNode( self, name="FileSystemPathInOutNode", defaultValue="", substitutions = IECore.StringAlgo.Substitutions.AllSubstitutions ) :

		return GafferTest.FileSystemPathInOutNode( name = name, defaultValue = defaultValue, substitutions = substitutions )

	def testPathExpansion( self ) :

		n = self.inOutNode()

		# nothing should be expanded when we're in a non-computation context
		n["in"].setValue( "testy/Testy.##.exr" )
		self.assertEqual( n["in"].getValue(), os.path.join( "testy", "Testy.##.exr" ) )

		n["in"].setValue( "${a}/$b/${a:b}" )
		self.assertEqual( n["in"].getValue(), os.path.join( "${a}", "$b", "${a:b}" ) )

		# but expansions should happen magically when the compute()
		# calls getValue().
		context = Gaffer.Context()
		
		context["env:dir"] = "a/path"
		n["in"].setValue( "a/${env:dir}/b" )
		with context :
			self.assertEqual( n["out"].getValue(), os.path.join( "a", "a", "path", "b" ) )

		# once again, nothing should be expanded when we're in a
		# non-computation context
		n["in"].setValue( "testy/Testy.##.exr" )
		self.assertEqual( n["in"].getValue(), os.path.join( "testy", "Testy.##.exr" ) )

	def testTildeExpansion( self ) :

		n = self.inOutNode()

		n["in"].setValue( "~" )
		self.assertEqual( n["out"].getValue(), os.path.expanduser( "~" ) )

		n["in"].setValue( "~/something.tif" )
		self.assertEqual( n["out"].getValue(), os.path.join( os.path.expanduser( "~" ), "something.tif" ) )

		# ~ shouldn't be expanded unless it's at the front - it would
		# be meaningless in other cases.
		n["in"].setValue( "in ~1900" )
		self.assertEqual( n["out"].getValue(), "in ~1900" )

	def testSlashConversion( self ) :
		n = GafferTest.FileSystemPathInOutNode()

		n["in"].setValue( "C:/path/test.ext" )
		if os.name == "nt" :
			self.assertEqual( n["out"].getValue(), "C:\\path\\test.ext" )
		else :
			self.assertEqual( n["out"].getValue(), "C:/path/test.ext" )

		n2 = GafferTest.FileSystemPathInOutNode()

		n2["in"].setInput( n["out"] )
		if os.name == "nt" :
			self.assertEqual( n2["out"].getValue(), "C:\\path\\test.ext" )
		else :
			self.assertEqual( n2["out"].getValue(), "C:/path/test.ext" )

		n3 = GafferTest.FileSystemPathInOutNode()
		n3["in"].setValue( "relative/path/test.ext" )
		self.assertEqual( n3["out"].getValue(), os.path.join( "relative", "path", "test.ext" ) )

		n4 = GafferTest.FileSystemPathInOutNode()
		n4["in"].setValue( "C:\\backslash\\should\\work\\too.ext" )
		if os.name == "nt" :
			self.assertEqual( n4["out"].getValue(), "C:\\backslash\\should\\work\\too.ext" )
		else :
			self.assertEqual( n4["out"].getValue(), "C:/backslash/should/work/too.ext" )

	def testUNC( self ) :
		n = GafferTest.FileSystemPathInOutNode()

		n["in"].setValue( "/test.server/path/test.ext" )
		if os.name == "nt" :
			self.assertEqual( n["out"].getValue(), "\\\\test.server\\path\\test.ext" )
		else:
			self.assertEqual( n["out"].getValue(), "/test.server/path/test.ext" )
		
	def testStringPlugCompatibility( self ):
		n = GafferTest.FileSystemPathInOutNode()
		s = GafferTest.StringInOutNode()
		s["in"].setValue( "test/string.ext" )

		n["in"].setInput( s["out"] )

		self.assertEqual( n["out"].getValue(), os.path.join( "test", "string.ext" ) )


if __name__ == "__main__":
	unittest.main()
