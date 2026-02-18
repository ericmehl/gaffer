//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2026, Cinesite VFX Ltd. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

// Reproduce Python's main executable found in `Programs/python.c` of CPython.
// Conceptually, Gaffer is just a Python process, and in an ideal world would
// run using a vanilla Python executable. But in practice we use our own
// derivative which provides the following benefits :
//   - A readily identifiable process name (gaffer rather than python).
//   - Control over what libraries the main executable links to, for example
//     libstdc++ and custom allocators.

#define PY_SSIZE_T_CLEAN
#include "Python.h"

#include "__gaffer.inl"

#ifdef MS_WINDOWS

// Replace the standard Windows allocators with TBB allocators
// which are much faster for heavily threaded applications.
// The header includes MSVC linker `pragma` preprocessor directives
// to link to the appropriate libraries.
#include "tbb/tbbmalloc_proxy.h"

#include <iostream>

int wmain( int argc, wchar_t **argv )
{
	// Verify that the TBB allocator has been registered.
	char **replacementLog;
	int replacementStatus = TBB_malloc_replacement_log( &replacementLog );

	if( replacementStatus != 0 )
	{
		std::cerr << "gaffer.exe : Failed to install TBB memory allocator. Performance may be degraded.\n";
		for( char **logEntry = replacementLog; *logEntry != 0; logEntry++ )
		{
			std::cerr << "gaffer.exe : " << *logEntry << "\n";
		}
	}
#else
int main( int argc, char **argv )
{
#endif

	PyStatus status;
	PyConfig config;
	PyConfig_InitPythonConfig( &config );

	try
	{

#ifdef MS_WINDOWS
		status = PyConfig_SetString( &config, &config.program_name, argv[0] );
#else
		status = PyConfig_SetBytesString( &config, &config.program_name, argv[0] );
#endif

		if( PyStatus_Exception( status ) )
		{
			throw std::runtime_error( "Error setting `config.program_name`" );
		}

		status = Py_InitializeFromConfig( &config );
		if( PyStatus_Exception( status ) )
		{
			throw std::runtime_error( "Error in `Py_InitializeFromConfig`" );
		}
		PyConfig_Clear( &config );

		PyRun_SimpleString( gafferScript );

		if( Py_FinalizeEx() < 0 )
		{
			exit( 120 );
		}
	}

	catch( const std::exception & )
	{
		/// \todo Do something with `e`?
		PyConfig_Clear( &config );
		Py_ExitStatusException( status );
	}

	return 0;
}