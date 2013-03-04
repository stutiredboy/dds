// server_parameters.cpp

/**
*    Copyright (C) 2012 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mongo/pch.h"

#include "mongo/base/parse_number.h"
#include "mongo/db/server_parameters.h"

namespace mongo {

    namespace {
        ServerParameterSet* GLOBAL = NULL;
    }

    ServerParameter::ServerParameter( ServerParameterSet* sps, const std::string& name,
                                      bool allowedToChangeAtStartup, bool allowedToChangeAtRuntime )
        : _name( name ),
          _allowedToChangeAtStartup( allowedToChangeAtStartup ),
          _allowedToChangeAtRuntime( allowedToChangeAtRuntime ) {

        if ( sps ) {
            sps->add( this );
        }
    }

    ServerParameter::ServerParameter( ServerParameterSet* sps, const std::string& name )
        : _name( name ),
          _allowedToChangeAtStartup( true ),
          _allowedToChangeAtRuntime( true ) {

        if ( sps ) {
            sps->add( this );
        }
    }

    ServerParameter::~ServerParameter() {
    }

    ServerParameterSet* ServerParameterSet::getGlobal() {
        if ( !GLOBAL ) {
            GLOBAL = new ServerParameterSet();
        }
        return GLOBAL;
    }

    void ServerParameterSet::add( ServerParameter* sp ) {
        ServerParameter*& x = _map[sp->name()];
        if ( x ) abort();
        x = sp;
    }

    template <typename T>
    Status ExportedServerParameter<T>::setFromString( const string& str ) {
        T value;
        Status status = parseNumberFromString( str, &value );
        if ( !status.isOK() )
            return status;
        return set( value );
    }

    template Status ExportedServerParameter<int>::setFromString( const string& str );
    template Status ExportedServerParameter<long>::setFromString( const string& str );
    template Status ExportedServerParameter<long long>::setFromString( const string& str );
    template Status ExportedServerParameter<double>::setFromString( const string& str );

    template<>
    Status ExportedServerParameter<string>::setFromString( const string& str ) {
        return set( str );
    }

    template<>
    Status ExportedServerParameter<bool>::setFromString( const string& str ) {
        if ( str == "true" ||
             str == "1" )
            return set(true);
        if ( str == "false" ||
             str == "0" )
            return set(false);
        return Status( ErrorCodes::BadValue, "can't convert string to bool" );
    }

    template<>
    Status ExportedServerParameter< vector<string> >::setFromString( const string& str ) {
        vector<string> v;
        splitStringDelim( str, &v, ',' );
        return set( v );
    }

}  // namespace mongo
