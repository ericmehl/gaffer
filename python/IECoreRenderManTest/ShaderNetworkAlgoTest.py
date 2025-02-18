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
			"roughness" : 0.75,
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
		self.assertEqual( convertedShader.parameters["specularRoughness"].value, 0.75 )

		# Use physical mode
		self.assertEqual( convertedShader.parameters["specularFresnelMode"].value, 1 )

		self.assertNotIn( "clearcoatFaceColor", convertedShader.parameters )
		self.assertNotIn( "clearcoatEdgeColor", convertedShader.parameters )
		self.assertNotIn( "clearcoatRoughness", convertedShader.parameters )
		self.assertNotIn( "clearcoatIor", convertedShader.parameters )
		self.assertNotIn( "clearcoatFresnelMode", convertedShader.parameters )

	def testConvertUSDPreviewSurfaceIor( self ) :

		parameters = {
			"ior" : 1.0,
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

		self.assertEqual( convertedShader.parameters["specularIor"].value, imath.Color3f( 1.0 ) )

	def testConvertUSDPreviewSurfaceEmission( self ) :

		for emissiveColor in ( imath.Color3f( 1 ), imath.Color3f( 0 ), None ) :

			with self.subTest( emissiveColor = emissiveColor ) :
				parameters = {}
				if emissiveColor is not None :
					parameters["emissiveColor"] = IECore.Color3fData( emissiveColor )

				network = IECoreScene.ShaderNetwork(
					shaders = {
						"previewSurface" : IECoreScene.Shader(
							"UsdPreviewSurface", "surface", parameters
						)
					},
					output = "previewSurface",
				)

				convertedNetwork = network.copy()
				IECoreRenderMan.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork )

				convertedShader = convertedNetwork.getShader( "previewSurface" )
				self.assertEqual(
					convertedShader.parameters["glowColor"].value,
					emissiveColor if emissiveColor is not None else imath.Color3f( 0 )
				)

				if emissiveColor is not None and emissiveColor != imath.Color3f( 0 ) :
					self.assertEqual( convertedShader.parameters["glowGain"], IECore.FloatData( 1 ) )
				else :
					self.assertEqual( convertedShader.parameters["glowGain"], IECore.FloatData( 0 ) )

	def testConvertUSDSpecular( self ) :

		for useSpecularWorkflow in ( True, False ) :
			for specularColor in ( imath.Color3f( 0, 0.25, 0.5 ), None ) :
				with self.subTest( useSpecularWorkflow = useSpecularWorkflow, specularColor = specularColor ) :

					parameters = {
						"metallic" : 0.2,
						"useSpecularWorkflow" : int( useSpecularWorkflow ),
					}
					if specularColor is not None :
						parameters["specularColor"] = specularColor
					if not useSpecularWorkflow :
						parameters["diffuseColor"] = imath.Color3f( 0.1, 0.2, 0.3 )
						parameters["ior"] = 1.2

					network = IECoreScene.ShaderNetwork(
						shaders = {
							"previewSurface" : IECoreScene.Shader(
								"UsdPreviewSurface", "surface", parameters
							)
						},
						output = "previewSurface",
					)

					convertedNetwork = network.copy()
					IECoreRenderMan.ShaderNetworkAlgo.convertUSDShaders( convertedNetwork )
					convertedShader = convertedNetwork.getShader( "previewSurface" )

					if useSpecularWorkflow :
						self.assertEqual(
							convertedShader.parameters["specularFaceColor"].value,
							specularColor if specularColor is not None else imath.Color3f( 0 )
						)
						self.assertEqual(
							convertedShader.parameters["specularEdgeColor"].value,
							imath.Color3f( 1.0 )
						)
						self.assertNotIn( "diffuseGain", convertedShader.parameters )
					else :
						spec = self.__mix( imath.Color3f( 1.0 ), parameters["diffuseColor"], 0.2 )
						fZero = ( 1.0 - parameters["ior"] ) / ( 1.0 + parameters["ior"] )
						fZero *= fZero
						specularColor = self.__mix( fZero * spec, spec, parameters["metallic"] )

						for i in range( 0, 3 ) :
							self.assertAlmostEqual(
								convertedShader.parameters["specularFaceColor"].value[i],
								specularColor[i] if specularColor is not None else 0.0
							)
							self.assertAlmostEqual(
								convertedShader.parameters["specularEdgeColor"].value[i],
								imath.Color3f( spec )[i]
							)
							self.assertEqual(
								convertedShader.parameters["diffuseGain"].value[i],
								imath.Color3f( 1.0 - parameters["metallic"] )[i]
							)
							self.assertEqual( convertedShader.parameters["specularIor"].value, imath.Color3f( 1.2 ) )

	def testConvertUSDClearcoat( self ) :

		parameters = {
			"clearcoat" : 0.75,
			"clearcoatRoughness" : 0.25,
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

		ior = 1.5
		r = ( 1.0 - ior ) / ( 1.0 + ior )
		self.assertEqual( convertedShader.parameters["clearcoatFaceColor"].value, imath.Color3f( r * r * parameters["clearcoat"] ) )
		self.assertEqual( convertedShader.parameters["clearcoatEdgeColor"].value, imath.Color3f( 1.0 ) )
		self.assertEqual( convertedShader.parameters["clearcoatRoughness"].value, 0.75 )
		self.assertEqual( convertedShader.parameters["clearcoatIor"].value, imath.Color3f( 1.5 ) )
		self.assertEqual( convertedShader.parameters["clearcoatFresnelMode"].value, 2 )

	def __mix( self, a, b, t ) :

		return a + ( b - a ) * t


if __name__ == "__main__" :
	unittest.main()
