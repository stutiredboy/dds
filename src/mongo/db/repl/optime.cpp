/*    Copyright 2009 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "mongo/db/repl/optime.h"

#include <iostream>
#include <ctime>

#include "mongo/bson/inline_decls.h"
#include "mongo/util/debug_util.h"
#include "mongo/util/log.h"

namespace mongo {

    NOINLINE_DECL OpTime OpTime::skewed() {
        bool toLog = false;
        ONCE toLog = true;
        RARELY toLog = true;
        last.i++;
        if ( last.i & 0x80000000 )
            toLog = true;
        if ( toLog ) {
            log() << "clock skew detected  prev: " << last.secs << " now: " << (unsigned) time(0) << endl;
        }
        if ( last.i & 0x80000000 ) {
            log() << "error large clock skew detected, shutting down" << endl;
            throw ClockSkewException();
        }
        return last;
    }

    /*static*/ OpTime OpTime::_now() {
        OpTime result;
        unsigned t = (unsigned) time(0);
        if ( last.secs == t ) {
            last.i++;
            result = last;
        }
        else if ( t < last.secs ) {
            result = skewed(); // separate function to keep out of the hot code path
        }
        else { 
            last = OpTime(t, 1);
            result = last;
        }
        notifier.notify_all();
        return last;
    }

    OpTime OpTime::now(const mongo::mutex::scoped_lock&) {
        return _now();
    }

    OpTime OpTime::getLast(const mongo::mutex::scoped_lock&) {
        return last;
    }

    void OpTime::waitForDifferent(unsigned millis){
        mutex::scoped_lock lk(m);
        while (*this == last) {
            if (!notifier.timed_wait(lk.boost(), boost::posix_time::milliseconds(millis)))
                return; // timed out
        }
    }

}
