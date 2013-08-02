/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include "Event.h"
#include "IdSetter.h"
#include "QueueGuard.h"
#include "QueueReplicator.h"
#include "QueueSnapshots.h"
#include "ReplicatingSubscription.h"
#include "Primary.h"
#include "HaBroker.h"
#include "qpid/assert.h"
#include "qpid/broker/Queue.h"
#include "qpid/broker/QueueObserver.h"
#include "qpid/broker/SessionContext.h"
#include "qpid/broker/amqp_0_10/MessageTransfer.h"
#include "qpid/framing/AMQFrame.h"
#include "qpid/framing/MessageTransferBody.h"
#include "qpid/log/Statement.h"
#include "qpid/types/Uuid.h"
#include <boost/pointer_cast.hpp>
#include <sstream>


namespace qpid {
namespace ha {

using namespace framing;
using namespace broker;
using namespace std;
using namespace boost;
using sys::Mutex;
using broker::amqp_0_10::MessageTransfer;

const string ReplicatingSubscription::QPID_REPLICATING_SUBSCRIPTION("qpid.ha-replicating-subscription");
const string ReplicatingSubscription::QPID_BROKER_INFO("qpid.ha-broker-info");
const string ReplicatingSubscription::QPID_ID_SET("qpid.ha-info");

class ReplicatingSubscription::QueueObserver : public broker::QueueObserver {
  public:
    QueueObserver(ReplicatingSubscription& rs_) : rs(rs_) {}
    void enqueued(const broker::Message&) {}
    void dequeued(const broker::Message& m) { rs.dequeued(m.getReplicationId()); }
    void acquired(const broker::Message&) {}
    void requeued(const broker::Message&) {}
  private:
    ReplicatingSubscription& rs;
};


/* Called by SemanticState::consume to create a consumer */
boost::shared_ptr<broker::SemanticState::ConsumerImpl>
ReplicatingSubscription::Factory::create(
    SemanticState* parent,
    const string& name,
    Queue::shared_ptr queue,
    bool ack,
    bool acquire,
    bool exclusive,
    const string& tag,
    const string& resumeId,
    uint64_t resumeTtl,
    const framing::FieldTable& arguments
) {
    boost::shared_ptr<ReplicatingSubscription> rs;
    if (arguments.isSet(QPID_REPLICATING_SUBSCRIPTION)) {
        rs.reset(new ReplicatingSubscription(
                     haBroker,
                     parent, name, queue, ack, acquire, exclusive, tag,
                     resumeId, resumeTtl, arguments));
        rs->initialize();
    }
    return rs;
}

namespace {
void copyIf(boost::shared_ptr<MessageInterceptor> from, boost::shared_ptr<IdSetter>& to) {
    boost::shared_ptr<IdSetter> result = boost::dynamic_pointer_cast<IdSetter>(from);
    if (result) to = result;
}
} // namespace

ReplicatingSubscription::ReplicatingSubscription(
    HaBroker& hb,
    SemanticState* parent,
    const string& name,
    Queue::shared_ptr queue,
    bool ack,
    bool /*acquire*/,
    bool exclusive,
    const string& tag,
    const string& resumeId,
    uint64_t resumeTtl,
    const framing::FieldTable& arguments
) : ConsumerImpl(parent, name, queue, ack, REPLICATOR, exclusive, tag,
                 resumeId, resumeTtl, arguments),
    position(0), ready(false), cancelled(false),
    haBroker(hb),
    primary(boost::dynamic_pointer_cast<Primary>(haBroker.getRole()))
{
    try {
        FieldTable ft;
        if (!arguments.getTable(ReplicatingSubscription::QPID_BROKER_INFO, ft))
            throw Exception("Replicating subscription does not have broker info: " + tag);
        info.assign(ft);

        // Set a log prefix message that identifies the remote broker.
        ostringstream os;
        os << "Subscription to " << queue->getName() << " at ";
        info.printId(os) << ": ";
        logPrefix = os.str();

        // If this is a non-cluster standalone replication then we need to
        // set up an IdSetter if there is not already one.
        boost::shared_ptr<IdSetter> idSetter;
        queue->getMessageInterceptors().each(
            boost::bind(&copyIf, _1, boost::ref(idSetter)));
        if (!idSetter) {
            QPID_LOG(debug, logPrefix << "Standalone replication");
            queue->getMessageInterceptors().add(
                boost::shared_ptr<IdSetter>(new IdSetter(queue->getName(), 1)));
        }

        // If there's already a guard (we are in failover) use it, else create one.
        if (primary) guard = primary->getGuard(queue, info);
        if (!guard) guard.reset(new QueueGuard(*queue, info));

        // NOTE: Once the observer is attached we can have concurrent
        // calls to dequeued so we need to lock use of this->dequeues.
        //
        // However we must attach the observer _before_ we snapshot for
        // initial dequeues to be sure we don't miss any dequeues
        // between the snapshot and attaching the observer.
        observer.reset(new QueueObserver(*this));
        queue->addObserver(observer);
        ReplicationIdSet primaryIds = haBroker.getQueueSnapshots()->get(queue)->snapshot();
        std::string backupStr = arguments.getAsString(ReplicatingSubscription::QPID_ID_SET);
        ReplicationIdSet backupIds;
        if (!backupStr.empty()) backupIds = decodeStr<ReplicationIdSet>(backupStr);

        // Initial dequeues are messages on backup but not on primary.
        ReplicationIdSet initDequeues = backupIds - primaryIds;
        QueuePosition front,back;
        queue->getRange(front, back, broker::REPLICATOR); // Outside lock, getRange locks queue
        {
            sys::Mutex::ScopedLock l(lock); // Concurrent calls to dequeued()
            dequeues += initDequeues;       // Messages on backup that are not on primary.
            skip = backupIds - initDequeues; // Messages already on the backup.
            // Queue front is moving but we know this subscriptions will start at a
            // position >= front so if front is safe then position must be.
            position = front;

            QPID_LOG(debug, logPrefix << "Subscribed: front " << front
                     << ", back " << back
                     << ", guarded " << guard->getFirst()
                     << ", on backup " << skip);
            checkReady(l);
        }
    }
    catch (const std::exception& e) {
        QPID_LOG(error, logPrefix << "Creation error: " << e.what()
                 << ": arguments=" << getArguments());
        throw;
    }
}

ReplicatingSubscription::~ReplicatingSubscription() {}


// Called in subscription's connection thread when the subscription is created.
// Called separate from ctor because sending events requires
// shared_from_this
//
void ReplicatingSubscription::initialize() {
    try {
        if (primary) primary->addReplica(*this);
        Mutex::ScopedLock l(lock); // Note dequeued() can be called concurrently.
        // Send initial dequeues to the backup.
        // There must be a shared_ptr(this) when sending.
        sendDequeueEvent(l);
    }
    catch (const std::exception& e) {
        QPID_LOG(error, logPrefix << "Initialization error: " << e.what()
                 << ": arguments=" << getArguments());
        throw;
    }
}

// True if the next position for the ReplicatingSubscription is a guarded position.
bool ReplicatingSubscription::isGuarded(sys::Mutex::ScopedLock&) {
    return position+1 >= guard->getFirst();
}

// Message is delivered in the subscription's connection thread.
bool ReplicatingSubscription::deliver(
    const qpid::broker::QueueCursor& c, const qpid::broker::Message& m)
{
    Mutex::ScopedLock l(lock);
    ReplicationId id = m.getReplicationId();
    position = m.getSequence();
    try {
        bool result = false;
        if (skip.contains(id)) {
            QPID_LOG(trace, logPrefix << "Skip " << LogMessageId(*getQueue(), m));
            skip -= id;
            guard->complete(id); // This will never be acknowledged.
            notify();
            result = true;
        }
        else {
            QPID_LOG(trace, logPrefix << "Replicated " << LogMessageId(*getQueue(), m));
            if (!ready && !isGuarded(l)) unready += id;
            sendIdEvent(id, l);
            result = ConsumerImpl::deliver(c, m);
        }
        checkReady(l);
        return result;
    } catch (const std::exception& e) {
        QPID_LOG(critical, logPrefix << "Error replicating " << LogMessageId(*getQueue(), m)
                 << ": " << e.what());
        throw;
    }
}

void ReplicatingSubscription::checkReady(sys::Mutex::ScopedLock& l) {
    if (!ready && isGuarded(l) && unready.empty()) {
        ready = true;
        sys::Mutex::ScopedUnlock u(lock);
        // Notify Primary that a subscription is ready.
        QPID_LOG(debug, logPrefix << "Caught up");
        if (primary) primary->readyReplica(*this);
    }
}

// Called in the subscription's connection thread.
void ReplicatingSubscription::cancel()
{
    {
        Mutex::ScopedLock l(lock);
        if (cancelled) return;
        cancelled = true;
    }
    QPID_LOG(debug, logPrefix << "Cancelled");
    if (primary) primary->removeReplica(*this);
    getQueue()->removeObserver(observer);
    guard->cancel();
    ConsumerImpl::cancel();
}

// Consumer override, called on primary in the backup's IO thread.
void ReplicatingSubscription::acknowledged(const broker::DeliveryRecord& r) {
    // Finish completion of message, it has been acknowledged by the backup.
    ReplicationId id = r.getReplicationId();
    QPID_LOG(trace, logPrefix << "Acknowledged " <<
             LogMessageId(*getQueue(), r.getMessageId(), id));
    guard->complete(id);
    {
        Mutex::ScopedLock l(lock);
        unready -= id;
        checkReady(l);
    }
    ConsumerImpl::acknowledged(r);
}

// Called with lock held. Called in subscription's connection thread.
void ReplicatingSubscription::sendDequeueEvent(Mutex::ScopedLock& l)
{
    if (dequeues.empty()) return;
    QPID_LOG(trace, logPrefix << "Sending dequeues " << dequeues);
    sendEvent(DequeueEvent(dequeues), l);
}

// Called after the message has been removed
// from the deque and under the messageLock in the queue. Called in
// arbitrary connection threads.
void ReplicatingSubscription::dequeued(ReplicationId id)
{
    QPID_LOG(trace, logPrefix << "Dequeued ID " << id);
    {
        Mutex::ScopedLock l(lock);
        dequeues.add(id);
    }
    notify();                   // Ensure a call to doDispatch
}


// Called with lock held. Called in subscription's connection thread.
void ReplicatingSubscription::sendIdEvent(ReplicationId pos, Mutex::ScopedLock& l)
{
    sendEvent(IdEvent(pos), l);
}

void ReplicatingSubscription::sendEvent(const Event& event, Mutex::ScopedLock&)
{
    Mutex::ScopedUnlock u(lock);
    // Send the event directly to the base consumer implementation.  The dummy
    // consumer prevents acknowledgements being handled, which is what we want
    // for events
    ConsumerImpl::deliver(QueueCursor(), event.message(), boost::shared_ptr<Consumer>());
}

// Called in subscription's connection thread.
bool ReplicatingSubscription::doDispatch()
{
    {
        Mutex::ScopedLock l(lock);
        if (!dequeues.empty()) sendDequeueEvent(l);
    }
    try {
        return ConsumerImpl::doDispatch();
    }
    catch (const std::exception& e) {
        QPID_LOG(warning, logPrefix << " exception in dispatch: " << e.what());
        return false;
    }
}

void ReplicatingSubscription::addSkip(const ReplicationIdSet& ids) {
    Mutex::ScopedLock l(lock);
    skip += ids;
}

}} // namespace qpid::ha