##########################################################################
#
#  Copyright (c) 2015, Image Engine Design Inc. All rights reserved.
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

__import__( "Gaffer" )

from ._GafferDispatch import *
from .LocalDispatcher import LocalDispatcher
from .SystemCommand import SystemCommand
from .TaskContextProcessor import TaskContextProcessor
from .Wedge import Wedge
from .TaskContextVariables import TaskContextVariables
from .TaskSwitch import TaskSwitch
from .PythonCommand import PythonCommand


# Returns a list containting the arguments, including the Gaffer executable,
# to pass to a subprocess to run the given task batch.
def gafferCommandArguments( batch, ignoreScriptLoadErrors = False ) :

	batchContext = batch.context()
	if batchContext is None or batch.plug() is None :
		return []

	if (
		not hasattr( batch.plug().node(), "directCommandArguments" ) or
		len( ( args := batch.plug().node().directCommandArguments( batch.context() ) ) ) == 0
	) :
		frames = str( __import__( "IECore" ).frameListFromList( [ int( x ) for x in batch.frames() ] ) )

		args = [
			str( __import__( "Gaffer" ).executablePath() ), "execute",
			"-script", batch.context()["dispatcher:scriptFileName"],
			"-nodes", batch.plug().node().getName(),
			"-frames", frames,
		]

		if ignoreScriptLoadErrors :
			args.append( "-ignoreScriptLoadErrors" )

		scriptNode = batch.plug().ancestor( __import__( "Gaffer" ).ScriptNode )
		assert( scriptNode is not None )
		scriptContext = scriptNode.context()

		contextArgs = []
		for entry in [ k for k in batchContext.keys() if k != "frame" ] :
			if entry not in scriptContext.keys() or batchContext[entry] != scriptContext[entry] :
				contextArgs.extend( [ "-" + entry, __import__( "IECore" ).repr( batchContext[entry] ) ] )

		if contextArgs :
			args.extend( [ "-context" ] + contextArgs )

	return args

__import__( "IECore" ).loadConfig( "GAFFER_STARTUP_PATHS", subdirectory = "GafferDispatch" )
