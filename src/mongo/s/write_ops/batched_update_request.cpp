/**
 *    Copyright (C) 2013 10gen Inc.
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
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/s/write_ops/batched_update_request.h"

#include "mongo/db/catalog/document_validation.h"
#include "mongo/db/commands.h"
#include "mongo/db/field_parser.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

using std::unique_ptr;
using std::string;

using mongoutils::str::stream;

const std::string BatchedUpdateRequest::BATCHED_UPDATE_REQUEST = "update";
const BSONField<std::string> BatchedUpdateRequest::collName("update");
const BSONField<std::vector<BatchedUpdateDocument*>> BatchedUpdateRequest::updates("updates");
const BSONField<bool> BatchedUpdateRequest::ordered("ordered", true);

BatchedUpdateRequest::BatchedUpdateRequest() {
    clear();
}

BatchedUpdateRequest::~BatchedUpdateRequest() {
    unsetUpdates();
}

bool BatchedUpdateRequest::isValid(std::string* errMsg) const {
    std::string dummy;
    if (errMsg == NULL) {
        errMsg = &dummy;
    }

    // All the mandatory fields must be present.
    if (!_isNSSet) {
        *errMsg = stream() << "missing " << collName.name() << " field";
        return false;
    }

    if (!_isUpdatesSet) {
        *errMsg = stream() << "missing " << updates.name() << " field";
        return false;
    }

    return true;
}

BSONObj BatchedUpdateRequest::toBSON() const {
    BSONObjBuilder builder;

    if (_isNSSet)
        builder.append(collName(), _ns.coll());

    if (_isUpdatesSet) {
        BSONArrayBuilder updatesBuilder(builder.subarrayStart(updates()));
        for (std::vector<BatchedUpdateDocument*>::const_iterator it = _updates.begin();
             it != _updates.end();
             ++it) {
            BSONObj updateDocument = (*it)->toBSON();
            updatesBuilder.append(updateDocument);
        }
        updatesBuilder.done();
    }

    if (_isOrderedSet)
        builder.append(ordered(), _ordered);

    if (_shouldBypassValidation)
        builder.append(bypassDocumentValidationCommandOption(), true);

    return builder.obj();
}

void BatchedUpdateRequest::parseRequest(const OpMsgRequest& request) {
    clear();

    for (auto&& elem : request.body) {
        auto extractField = [&](const auto& fieldDesc, auto* valOut, auto* isSetOut) {
            std::string errMsg;
            FieldParser::FieldState fieldState =
                FieldParser::extract(elem, fieldDesc, valOut, &errMsg);
            if (fieldState == FieldParser::FIELD_INVALID) {
                uasserted(ErrorCodes::FailedToParse, errMsg);
            }
            *isSetOut = fieldState == FieldParser::FIELD_SET;
        };

        const StringData fieldName = elem.fieldNameStringData();
        if (fieldName == collName.name()) {
            std::string collNameTemp;
            extractField(collName, &collNameTemp, &_isNSSet);
            _ns = NamespaceString(request.getDatabase(), collNameTemp);
            uassert(ErrorCodes::InvalidNamespace,
                    str::stream() << "Invalid namespace: " << _ns.ns(),
                    _ns.isValid());
        } else if (fieldName == updates.name()) {
            extractField(updates, &_updates, &_isUpdatesSet);
        } else if (fieldName == ordered.name()) {
            extractField(ordered, &_ordered, &_isOrderedSet);
        } else if (fieldName == bypassDocumentValidationCommandOption()) {
            _shouldBypassValidation = elem.trueValue();
        } else if (!Command::isGenericArgument(fieldName)) {
            uasserted(ErrorCodes::FailedToParse,
                      str::stream() << "Unknown option to update command: " << fieldName);
        }
    }

    for (auto&& seq : request.sequences) {
        uassert(ErrorCodes::FailedToParse,
                str::stream() << "Unknown document sequence option to " << request.getCommandName()
                              << " command: "
                              << seq.name,
                seq.name == updates());
        uassert(ErrorCodes::FailedToParse,
                str::stream() << "Duplicate document sequence " << updates(),
                !_isUpdatesSet);
        _isUpdatesSet = true;

        for (auto&& obj : seq.objs) {
            _updates.push_back(new BatchedUpdateDocument());  // _updates takes ownership.
            std::string errMsg;
            uassert(ErrorCodes::FailedToParse,
                    errMsg,
                    _updates.back()->parseBSON(obj, &errMsg) && _updates.back()->isValid(&errMsg));
        }
    }
}

void BatchedUpdateRequest::clear() {
    _ns = NamespaceString();
    _isNSSet = false;

    unsetUpdates();

    _ordered = false;
    _isOrderedSet = false;

    _shouldBypassValidation = false;
}

std::string BatchedUpdateRequest::toString() const {
    return toBSON().toString();
}

void BatchedUpdateRequest::setNS(NamespaceString ns) {
    _ns = std::move(ns);
    _isNSSet = true;
}

const NamespaceString& BatchedUpdateRequest::getNS() const {
    dassert(_isNSSet);
    return _ns;
}

void BatchedUpdateRequest::addToUpdates(BatchedUpdateDocument* updates) {
    _updates.push_back(updates);
    _isUpdatesSet = true;
}

void BatchedUpdateRequest::unsetUpdates() {
    for (std::vector<BatchedUpdateDocument*>::iterator it = _updates.begin(); it != _updates.end();
         ++it) {
        delete *it;
    }
    _updates.clear();
    _isUpdatesSet = false;
}

size_t BatchedUpdateRequest::sizeUpdates() const {
    return _updates.size();
}

const std::vector<BatchedUpdateDocument*>& BatchedUpdateRequest::getUpdates() const {
    dassert(_isUpdatesSet);
    return _updates;
}

const BatchedUpdateDocument* BatchedUpdateRequest::getUpdatesAt(size_t pos) const {
    dassert(_isUpdatesSet);
    dassert(_updates.size() > pos);
    return _updates.at(pos);
}

void BatchedUpdateRequest::setOrdered(bool ordered) {
    _ordered = ordered;
    _isOrderedSet = true;
}

void BatchedUpdateRequest::unsetOrdered() {
    _isOrderedSet = false;
}

bool BatchedUpdateRequest::isOrderedSet() const {
    return _isOrderedSet;
}

bool BatchedUpdateRequest::getOrdered() const {
    if (_isOrderedSet) {
        return _ordered;
    } else {
        return ordered.getDefault();
    }
}
}  // namespace mongo
