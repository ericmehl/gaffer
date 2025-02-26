##########################################################################
#
#  Copyright (c) 2025, Cinesite VFX Ltd. All rights reserved.
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
import IECoreScene
import IECoreRenderMan

class ShaderNetworkAlgoTest( unittest.TestCase ) :

	def testUSDPreviewSurface( self ) :

		parameters = {
			"diffuseColor" : IECore.Color3fData( imath.Color3f( 0.1, 0.2, 0.3 ) ),
		}

		network = IECoreScene.ShaderNetwork(
			shaders = {
				"previewSurface" : IECoreScene.Shader(
					"UsdPreviewSurface", "surface", parameters
				)
			},
			output = "previewSurface"
		)

		convertedNetwork = network.copy()
		IECoreRenderMan.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork )

		convertedShader = convertedNetwork.getShader( "previewSurface" )
		self.assertEqual( convertedShader.name, "PxrSurface" )

		self.assertEqual( convertedShader.parameters["diffuseColor"].value, imath.Color3f( 0.1, 0.2, 0.3 ) )

	def testConvertSimpleUSDUVTexture( self ) :

		for usdOutput, riOutput in [
			( "rgb", "resultRGB" ),
			( "r", "resultR" ),
			( "g", "resultG" ),
			( "b", "resultB" ),
			( "a", "resultA" ),
		] :

			with self.subTest( usdOutput = usdOutput, riOutput = riOutput ) :
				network = IECoreScene.ShaderNetwork(
					shaders = {
						"previewSurface" : IECoreScene.Shader( "UsdPreviewSurface" ),
						"texture" : IECoreScene.Shader(
							"UsdUVTexture", "shader",
							{
								"file" : "test.png",
								# \todo Can we support `wrapS` and `wrapT`, `sourceColorSpace`?
								# "wrapS" : "useMetadata",
								# "wrapT" : "repeat",
								"sourceColorSpace" : "raw",
								"fallback" : IECore.Color4fData( imath.Color4f( 0.1, 0.2, 0.3, 1.0 ) ),
								"scale" : IECore.Color4fData( imath.Color4f( 0.4, 0.5, 0.6, 1.0 ) ),
								"bias" : IECore.Color4fData( imath.Color4f( 0.7, 0.8, 0.9, 1.0 ) ),
							}
						),
					},
					connections = [
						( ( "texture", usdOutput ), ( "previewSurface", "diffuseColor" ) ),
					],
					output = "previewSurface",
				)

				IECoreRenderMan.ShaderNetworkAlgo.convertUSDShaders( network )

				self.assertEqual( network.input( ( "previewSurface", "diffuseColor" ) ), ( "texture", riOutput ) )

				texture = network.getShader( "texture" )
				self.assertEqual( texture.name, "PxrTexture" )
				self.assertEqual( texture.parameters["filename"].value, "test.png" )
				self.assertEqual( texture.parameters["missingColor"].value, imath.Color4f( 0.1, 0.2, 0.3, 1.0 ) )
				self.assertEqual( texture.parameters["colorScale"].value, imath.Color4f( 0.4, 0.5, 0.6, 1.0 ) )
				self.assertEqual( texture.parameters["colorOffset"].value, imath.Color4f( 0.7, 0.8, 0.9, 1.0 ) )


if __name__ == "__main__" :
	unittest.main()
