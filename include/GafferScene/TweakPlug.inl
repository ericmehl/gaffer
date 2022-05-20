//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2019, Image Engine Design Inc. All rights reserved.
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

#ifndef GAFFERSCENE_TWEAKPLUG_INL
#define GAFFERSCENE_TWEAKPLUG_INL

#include "Gaffer/PlugAlgo.h"

#include "IECore/DataAlgo.h"
#include "IECore/TypeTraits.h"

namespace
{

const char *modeToString( GafferScene::TweakPlug::Mode mode )
{
	switch( mode )
	{
		case GafferScene::TweakPlug::Replace :
			return "Replace";
		case GafferScene::TweakPlug::Add :
			return "Add";
		case GafferScene::TweakPlug::Subtract :
			return "Subtract";
		case GafferScene::TweakPlug::Multiply :
			return "Multiply";
		case GafferScene::TweakPlug::Remove :
			return "Remove";
		default :
			return "Invalid";
	}
}

// TODO - if these make sense, I guess they should be pushed back to cortex

// IsColorTypedData
template< typename T > struct IsColorTypedData : boost::mpl::and_< IECore::TypeTraits::IsTypedData<T>, IECore::TypeTraits::IsColor< typename IECore::TypeTraits::ValueType<T>::type > > {};

// SupportsArithmeticData
template< typename T > struct SupportsArithData : boost::mpl::or_<  IECore::TypeTraits::IsNumericSimpleTypedData<T>, IECore::TypeTraits::IsVecTypedData<T>, IsColorTypedData<T>> {};


class NumericTweak
{
public:
	NumericTweak( IECore::Data *sourceData, GafferScene::TweakPlug::Mode mode, const std::string &tweakName )
		: m_sourceData( sourceData ), m_mode( mode ), m_tweakName( tweakName )
	{
	}

	template<typename T>
	void operator()( T * data, typename std::enable_if<SupportsArithData<T>::value>::type *enabler = nullptr ) const
	{
		T *sourceDataCast = IECore::runTimeCast<T>( m_sourceData );
		switch( m_mode )
		{
			case GafferScene::TweakPlug::Add :
				data->writable() += sourceDataCast->readable();
				break;
			case GafferScene::TweakPlug::Subtract :
				data->writable() -= sourceDataCast->readable();
				break;
			case GafferScene::TweakPlug::Multiply :
				data->writable() *= sourceDataCast->readable();
				break;
			case GafferScene::TweakPlug::Replace :
			case GafferScene::TweakPlug::Remove :
				// These cases are unused - we handle replace and remove mode outside of numericTweak.
				// But the compiler gets unhappy if we don't handle some cases
				break;
		}
	}

	void operator()( IECore::Data * data ) const
	{
		throw IECore::Exception( boost::str( boost::format( "Cannot apply tweak with mode %s to \"%s\" : Data type %s not supported." ) % modeToString( m_mode ) % m_tweakName % m_sourceData->typeName() ) );
	}

private:

	IECore::Data *m_sourceData;
	GafferScene::TweakPlug::Mode m_mode;
	const std::string &m_tweakName;
};

template<typename GetDataFunctor, typename SetDataFunctor>
bool applyTweakInternal(
	GafferScene::TweakPlug::Mode mode,
	const Gaffer::ValuePlug *valuePlug,
	const std::string &tweakName,
	const IECore::InternedString &parameterName,
	GetDataFunctor &&getDataFunctor,
	SetDataFunctor &&setDataFunctor,
	GafferScene::TweakPlug::MissingMode missingMode
)
{
	if( mode == GafferScene::TweakPlug::Remove )
	{
		return setDataFunctor( parameterName,  nullptr );
	}

	IECore::Data *parameterValue = getDataFunctor( parameterName );
	IECore::DataPtr newData = Gaffer::PlugAlgo::getValueAsData( valuePlug );
	if( !newData )
	{
		throw IECore::Exception(
			boost::str( boost::format( "Cannot apply tweak to \"%s\" : Value plug has unsupported type \"%s\"" ) % tweakName % valuePlug->typeName() )
		);
	}
	if( parameterValue && parameterValue->typeId() != newData->typeId() )
	{
		throw IECore::Exception(
			boost::str( boost::format( "Cannot apply tweak to \"%s\" : Value of type \"%s\" does not match parameter of type \"%s\"" ) % tweakName % parameterValue->typeName() % newData->typeName() )
		);
	}

	if( !parameterValue )
	{
		if( missingMode == GafferScene::TweakPlug::MissingMode::Ignore )
		{
			return false;
		}
		else if( !( mode == GafferScene::TweakPlug::Replace && missingMode == GafferScene::TweakPlug::MissingMode::IgnoreOrReplace) )
		{
			throw IECore::Exception( boost::str( boost::format( "Cannot apply tweak with mode %s to \"%s\" : This parameter does not exist" ) % modeToString( mode ) % tweakName ) );
		}
	}

	if( mode == GafferScene::TweakPlug::Replace )
	{
		setDataFunctor( parameterName, newData );
		return true;
	}

	NumericTweak t( newData.get(), mode, tweakName );
	dispatch( parameterValue, t );
	return true;
}

}  // namespace

namespace GafferScene
{

template<typename T>
T *TweakPlug::valuePlug()
{
	return IECore::runTimeCast<T>( valuePlugInternal() );
}

template<typename T>
const T *TweakPlug::valuePlug() const
{
	return IECore::runTimeCast<const T>( valuePlugInternal() );
}

template<class GetDataFunctor, class SetDataFunctor>
bool TweakPlug::applyTweak(
	GetDataFunctor &&getDataFunctor,
	SetDataFunctor &&setDataFunctor,
	MissingMode missingMode
) const
{
	if( !enabledPlug()->getValue() )
	{
		return false;
	}

	const std::string name = namePlug()->getValue();
	if( name.empty() )
	{
		return false;
	}

	const Mode mode = static_cast<Mode>( modePlug()->getValue() );
	return applyTweakInternal( mode, this->valuePlug(), name, name, getDataFunctor, setDataFunctor, missingMode );
}

} // namespace GafferScene

#endif // GAFFERSCENE_TWEAKPLUG_INL
