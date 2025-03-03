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
			"emissiveColor" : IECore.Color3fData( imath.Color3f( 0.4, 0.5, 0.6 ) ),
			"useSpecularWorkflow" : 1,
			"specularColor" : IECore.Color3fData( imath.Color3f( 0.7, 0.8, 0.9 ) ),
			"metallic" : 0.5,
			"roughness" : 0.375,
			"clearcoat" : 0.25,
			"clearcoatRoughness" : 0.75,
			"opacity" : 0.625,
			"opacityThreshold" : 0.875,
			"ior" : 1.25,
			"normal" : IECore.V3fData( imath.V3f( 0.1, 0.2, 0.3 ) ),
			"occlusion" : 0.5625,
		}

		network = IECoreScene.ShaderNetwork(
			shaders = {
				"previewSurface" : IECoreScene.Shader(
					"UsdPreviewSurface", "surface", parameters
				)
			},
			output = "previewSurface"
		)

		IECoreRenderMan.ShaderNetworkAlgo.convertUSDShaders( network )

		self.assertEqual( len( network ), 2 )

		convertedShader = network.getShader( "previewSurface" )
		self.assertEqual( convertedShader.name, "__usd/UsdPreviewSurfaceParameters" )
		self.assertEqual( convertedShader.type, "osl:shader" )

		self.assertEqual( convertedShader.parameters["diffuseColor"].value, imath.Color3f( 0.1, 0.2, 0.3 ) )
		self.assertEqual( convertedShader.parameters["emissiveColor"].value, imath.Color3f( 0.4, 0.5, 0.6 ) )
		self.assertEqual( convertedShader.parameters["useSpecularWorkflow"].value, 1 )
		self.assertEqual( convertedShader.parameters["specularColor"].value, imath.Color3f( 0.7, 0.8, 0.9 ) )
		self.assertEqual( convertedShader.parameters["metallic"].value, 0.5 )
		self.assertEqual( convertedShader.parameters["roughness"].value, 0.375 )
		self.assertEqual( convertedShader.parameters["clearcoat"].value, 0.25 )
		self.assertEqual( convertedShader.parameters["clearcoatRoughness"].value, 0.75 )
		self.assertEqual( convertedShader.parameters["opacity"].value, 0.625 )
		self.assertEqual( convertedShader.parameters["opacityThreshold"].value, 0.875 )
		self.assertEqual( convertedShader.parameters["ior"].value, 1.25 )
		self.assertNotIn( "normal", convertedShader.parameters )
		self.assertEqual( convertedShader.parameters["normalIn"].value, imath.V3f( 0.1, 0.2, 0.3 ) )
		self.assertEqual( convertedShader.parameters["occlusion"].value, 0.5625 )

		convertedSurface = network.getShader( "previewSurfacePxrSurface" )
		self.assertEqual( convertedSurface.name, "PxrSurface" )

		for pxrSurfaceIn in [
			"diffuseGain",
			"diffuseColor",
			"specularFaceColor",
			"specularEdgeColor",
			"specularRoughness",
			"specularIor",
			"clearcoatFaceColor",
			"clearcoatEdgeColor",
			"clearcoatRoughness",
			"glowGain",
			"glowColor",
			"bumpNormal",
			"glassIor",
			"glassRoughness",
			"refractionGain",
			"presence",
		] :
			self.assertEqual( network.input( ( "previewSurfacePxrSurface", pxrSurfaceIn ) ), ( "previewSurface", pxrSurfaceIn + "Out" ) )

		self.assertEqual( network.getOutput(), ( "previewSurfacePxrSurface", "" ) )

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
				self.assertEqual( texture.parameters["missingColor"].value, imath.Color3f( 0.1, 0.2, 0.3 ) )
				self.assertEqual( texture.parameters["colorScale"].value, imath.Color3f( 0.4, 0.5, 0.6 ) )
				self.assertEqual( texture.parameters["colorOffset"].value, imath.Color3f( 0.7, 0.8, 0.9 ) )

	def testConvertUSDUVTextureColorSpace( self ) :

		for sourceColorSpace, linearizeValue, warningMessage in [
			( "raw", 0, None ),
			( "sRGB", 1, None ),
			( "auto", 0, "\"sourceColorSpace\" must be \"raw\" or \"sRGB\". Defaulting to \"raw\"." )
		] :
			with self.subTest( sourceColorSpace = sourceColorSpace, linearizeValue = linearizeValue, warningMessage = warningMessage ) :
				network = IECoreScene.ShaderNetwork(
					shaders = {
						"previewSurface" : IECoreScene.Shader( "UsdPreviewSurface" ),
						"texture" : IECoreScene.Shader(
							"UsdUVTexture", "shader",
							{
								"sourceColorSpace" : sourceColorSpace,
							}
						),
					},
					connections = [
						( ( "texture", "rgb" ), ( "previewSurface", "diffuseColor" ) ),
					],
					output = "previewSurface",
				)

				with IECore.CapturingMessageHandler() as mh :
					IECoreRenderMan.ShaderNetworkAlgo.convertUSDShaders( network )

				self.assertEqual( network.input( ( "previewSurface", "diffuseColor" ) ), ( "texture", "resultRGB" ) )

				texture = network.getShader( "texture" )

				self.assertEqual( texture.parameters["linearize"].value, linearizeValue )

				if warningMessage is not None :
					self.assertEqual( len( mh.messages ), 1 )
					self.assertEqual( mh.messages[0].level, IECore.MessageHandler.Level.Warning )
					self.assertEqual( mh.messages[0].context, "IECoreRenderMan::ShaderNetworkAlgo::convertUSDShaders" )
					self.assertEqual( mh.messages[0].message, warningMessage )

	def testConvertUSDPrimvarReader( self ) :

		for usdDataType, fallback, riType, riDefaultParameter, riDefault, readerOut, surfaceIn in [
			( "float", 2.0, "float", "defaultFloat", 2.0, "resultF", "metallic" ),
			# \todo `float2` doesn't have default in `PxrPrimvar`. Does it use `defaultFloat3`?
			( "float2", imath.V2f( 1, 2 ), "float2", "defaultFloat3", imath.Color3f( 1, 2, 0 ), "resultRGB", "diffuseColor" ),
			( "float3", imath.V3f( 1, 2, 3 ), "vector", "defaultFloat3", imath.Color3f( 1, 2, 3 ), "resultRGB", "diffuseColor" ),
			( "normal", imath.V3f( 1, 2, 3 ), "normal", "defaultFloat3", imath.Color3f( 1, 2, 3 ), "resultRGB", "diffuseColor" ),
			( "point", imath.V3f( 1, 2, 3 ), "point", "defaultFloat3", imath.Color3f( 1, 2, 3 ), "resultRGB", "diffuseColor" ),
			( "vector", imath.V3f( 1, 2, 3 ), "vector", "defaultFloat3", imath.Color3f( 1, 2, 3 ), "resultRGB", "diffuseColor" ),
			( "int", 10, "int", "defaultInt", 10, "resultF", "metallic" ),
		] :
			with self.subTest( usdDataType = usdDataType, fallback = fallback, riType = riType, riDefaultParameter = riDefaultParameter, riDefault = riDefault, readerOut = readerOut, surfaceIn = surfaceIn ) :
				network = IECoreScene.ShaderNetwork(
					shaders = {
						"previewSurface" : IECoreScene.Shader( "UsdPreviewSurface" ),
						"reader" : IECoreScene.Shader(
							"UsdPrimvarReader_{}".format( usdDataType ), "shader",
							{
								"varname" : "test",
								"fallback" : fallback,
							}
						),
					},
					connections = [
						( ( "reader", readerOut ), ( "previewSurface", surfaceIn ) ),
					],
					output = "previewSurface",
				)

				IECoreRenderMan.ShaderNetworkAlgo.convertUSDShaders( network )

				reader = network.getShader( "reader" )
				self.assertEqual( reader.name, "PxrPrimvar" )
				self.assertEqual( len( reader.parameters ), 3 )
				self.assertEqual( reader.parameters["varname"].value, "test" )
				self.assertEqual( reader.parameters["type"].value, riType )
				self.assertEqual( reader.parameters[riDefaultParameter].value, riDefault )

				self.assertEqual( network.input( ( "previewSurface", surfaceIn ) ), ( "reader", readerOut ) )

if __name__ == "__main__" :
	unittest.main()
