/**
 *    Copyright (C) 2018 MongoDB Inc.
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

#include "mongo/db/s/active_move_primaries_registry.h"
#include "mongo/bson/bsonmisc.h"
#include "mongo/db/client.h"
#include "mongo/db/service_context_noop.h"
#include "mongo/s/request_types/move_primary_gen.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

using unittest::assertGet;

class MovePrimaryRegistration : public unittest::Test {
protected:
    void setUp() override {
        _client = _serviceContext.makeClient("MovePrimaryRegistrationTest");
        _opCtx = _serviceContext.makeOperationContext(_client.get());
    }

    void tearDown() override {
        _opCtx.reset();
        _client.reset();
    }

    OperationContext* getTxn() const {
        return _opCtx.get();
    }

    ServiceContextNoop _serviceContext;
    ServiceContext::UniqueClient _client;
    ServiceContext::UniqueOperationContext _opCtx;

    ActiveMovePrimariesRegistry _registry;
};

ShardMovePrimary createMovePrimaryRequest(const NamespaceString& nss) {
    ShardMovePrimary request;
    request.set_movePrimary(std::move(nss));
    request.setTo("shard0001");

    return request;
}

TEST_F(MovePrimaryRegistration, ScopedPrimaryMoveConstructorAndAssignment) {
    auto originalScopedMovePrimary = assertGet(
        _registry.registerMovePrimary(createMovePrimaryRequest(NamespaceString("TestDB"))));
    ASSERT(originalScopedMovePrimary.mustExecute());

    ScopedMovePrimary movedScopedMovePrimary(std::move(originalScopedMovePrimary));
    ASSERT(movedScopedMovePrimary.mustExecute());

    originalScopedMovePrimary = std::move(movedScopedMovePrimary);
    ASSERT(originalScopedMovePrimary.mustExecute());

    // Need to signal the registered migration so the destructor doesn't invariant
    originalScopedMovePrimary.signalComplete(Status::OK());
}

TEST_F(MovePrimaryRegistration, GetActiveMovePrimaryNamespace) {
    ASSERT(!_registry.getActiveMovePrimaryNss());

    const NamespaceString nss("TestDB");

    auto originalScopedMovePrimary =
        assertGet(_registry.registerMovePrimary(createMovePrimaryRequest(nss)));

    ASSERT_EQ(nss.ns(), _registry.getActiveMovePrimaryNss()->ns());

    // Need to signal the registered migration so the destructor doesn't invariant
    originalScopedMovePrimary.signalComplete(Status::OK());
}

TEST_F(MovePrimaryRegistration, SecondMovePrimaryReturnsConflictingOperationInProgress) {
    auto originalScopedMovePrimary = assertGet(
        _registry.registerMovePrimary(createMovePrimaryRequest(NamespaceString("TestDB"))));

    auto secondScopedMovePrimaryStatus =
        _registry.registerMovePrimary(createMovePrimaryRequest(NamespaceString("TestDB2")));
    ASSERT_EQ(ErrorCodes::ConflictingOperationInProgress,
              secondScopedMovePrimaryStatus.getStatus());

    originalScopedMovePrimary.signalComplete(Status::OK());
}

TEST_F(MovePrimaryRegistration, SecondMovePrimaryWithSameArgumentsJoinsFirst) {
    auto originalScopedMovePrimary = assertGet(
        _registry.registerMovePrimary(createMovePrimaryRequest(NamespaceString("TestDB"))));
    ASSERT(originalScopedMovePrimary.mustExecute());

    auto secondScopedMovePrimary = assertGet(
        _registry.registerMovePrimary(createMovePrimaryRequest(NamespaceString("TestDB"))));
    ASSERT(!secondScopedMovePrimary.mustExecute());

    originalScopedMovePrimary.signalComplete({ErrorCodes::InternalError, "Test error"});
    ASSERT_EQ(Status(ErrorCodes::InternalError, "Test error"),
              secondScopedMovePrimary.waitForCompletion(getTxn()));
}

}  // namespace
}  // namespace mongo
