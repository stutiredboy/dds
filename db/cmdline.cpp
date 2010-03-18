// cmdline.cpp

/**
*    Copyright (C) 2008 10gen Inc.
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

#include "cmdline.h"

namespace po = boost::program_options;

namespace mongo {
    CmdLine cmdLine;

    void CmdLine::addGlobalOptions( boost::program_options::options_description& general , 
                                    boost::program_options::options_description& hidden ){
        /* support for -vv -vvvv etc. */
        for (string s = "vv"; s.length() <= 10; s.append("v")) {
            hidden.add_options()(s.c_str(), "verbose");
        }
        
        general.add_options()
            ("help,h", "show this usage information")
            ("version", "show version information")
            ("config,f", po::value<string>(), "configuration file specifying additional options")
            ("verbose,v", "be more verbose (include multiple times for more verbosity e.g. -vvvvv)")
            ("quiet", "quieter output")
            ("port", po::value<int>(&cmdLine.port), "specify port number")
            
            ;
        
    }


    bool CmdLine::store( int argc , char ** argv , 
                         boost::program_options::options_description& options,
                         boost::program_options::positional_options_description& positional,
                         boost::program_options::variables_map &params ){
        
        
        /* don't allow guessing - creates ambiguities when some options are
         * prefixes of others. allow long disguises and don't allow guessing
         * to get away with our vvvvvvv trick. */
        int style = (((po::command_line_style::unix_style ^
                       po::command_line_style::allow_guessing) |
                      po::command_line_style::allow_long_disguise) ^
                     po::command_line_style::allow_sticky);

        
        try {
            po::store( po::command_line_parser(argc, argv)
                       .options( options )
                       .positional( positional )
                       .style( style )
                       .run(), 
                       params );

            if ( params.count("config") ){
                ifstream f( params["config"].as<string>().c_str() );
                if ( ! f.is_open() ){
                    cout << "ERROR: could not read from config file" << endl << endl;
                    cout << options << endl;
                    return false;
                }
                
                po::store( po::parse_config_file( f , options ) , params );
                f.close();
            }
            
            po::notify(params);
        } 
        catch (po::error &e) {
            cout << "ERROR: " << e.what() << endl << endl;
            cout << options << endl;
            return false;
        }

        if (params.count("verbose")) {
            logLevel = 1;
        }

        for (string s = "vv"; s.length() <= 10; s.append("v")) {
            if (params.count(s)) {
                logLevel = s.length();
            }
        }

        if (params.count("quiet")) {
            cmdLine.quiet = true;
        }

        return true;
    }
}
