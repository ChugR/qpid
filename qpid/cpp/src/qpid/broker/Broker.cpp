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

#include "config.h"
#include "Broker.h"
#include "DirectExchange.h"
#include "FanOutExchange.h"
#include "HeadersExchange.h"
#include "MessageStoreModule.h"
#include "NullMessageStore.h"
#include "RecoveryManagerImpl.h"
#include "TopicExchange.h"
#include "Link.h"
#include "qpid/management/PackageQpid.h"
#include "qpid/management/ManagementExchange.h"
#include "qpid/management/ArgsBrokerEcho.h"

#include "qpid/log/Statement.h"
#include "qpid/framing/AMQFrame.h"
#include "qpid/framing/ProtocolInitiation.h"
#include "qpid/sys/ProtocolFactory.h"
#include "qpid/sys/Poller.h"
#include "qpid/sys/Dispatcher.h"
#include "qpid/sys/Thread.h"
#include "qpid/sys/ConnectionInputHandler.h"
#include "qpid/sys/ConnectionInputHandlerFactory.h"
#include "qpid/sys/TimeoutHandler.h"
#include "qpid/sys/SystemInfo.h"
#include "qpid/Url.h"

#include <boost/bind.hpp>

#include <iostream>
#include <memory>

#if HAVE_SASL
#include <sasl/sasl.h>
static const bool AUTH_DEFAULT=true;
#else
static const bool AUTH_DEFAULT=false;
#endif

using qpid::sys::ProtocolFactory;
using qpid::sys::Poller;
using qpid::sys::Dispatcher;
using qpid::sys::Thread;
using qpid::framing::FrameHandler;
using qpid::framing::ChannelId;
using qpid::management::ManagementBroker;
using qpid::management::ManagementObject;
using qpid::management::Manageable;
using qpid::management::Args;
using qpid::management::ArgsBrokerEcho;

namespace qpid {
namespace broker {

Broker::Options::Options(const std::string& name) :
    qpid::Options(name),
    noDataDir(0),
    dataDir("/var/lib/qpidd"),
    port(DEFAULT_PORT),
    workerThreads(5),
    maxConnections(500),
    connectionBacklog(10),
    stagingThreshold(5000000),
    enableMgmt(1),
    mgmtPubInterval(10),
    auth(AUTH_DEFAULT),
    replayFlushLimit(64),
    replayHardLimit(0)
{
    int c = sys::SystemInfo::concurrency();
    workerThreads=c+1;
    addOptions()
        ("data-dir", optValue(dataDir,"DIR"), "Directory to contain persistent data generated by the broker")
        ("no-data-dir", optValue(noDataDir), "Don't use a data directory.  No persistent configuration will be loaded or stored")
        ("port,p", optValue(port,"PORT"), "Tells the broker to listen on PORT")
        ("worker-threads", optValue(workerThreads, "N"), "Sets the broker thread pool size")
        ("max-connections", optValue(maxConnections, "N"), "Sets the maximum allowed connections")
        ("connection-backlog", optValue(connectionBacklog, "N"), "Sets the connection backlog limit for the server socket")
        ("staging-threshold", optValue(stagingThreshold, "N"), "Stages messages over N bytes to disk")
        ("mgmt-enable,m", optValue(enableMgmt,"yes|no"), "Enable Management")
        ("mgmt-pub-interval", optValue(mgmtPubInterval, "SECONDS"), "Management Publish Interval")
        ("auth", optValue(auth, "yes|no"), "Enable authentication, if disabled all incoming connections will be trusted")
        ("replay-flush-limit", optValue(replayFlushLimit, "KB"), "Send flush request when the replay buffer reaches this limit. 0 means no limit.")
        ("replay-hard-limit", optValue(replayHardLimit, "KB"), "Kill a session if its replay buffer exceeds this limit. 0 means no limit.");
}

const std::string empty;
const std::string amq_direct("amq.direct");
const std::string amq_topic("amq.topic");
const std::string amq_fanout("amq.fanout");
const std::string amq_match("amq.match");
const std::string qpid_management("qpid.management");

Broker::Broker(const Broker::Options& conf) :
    poller(new Poller),
    config(conf),
    store(0),
    dataDir(conf.noDataDir ? std::string () : conf.dataDir),
    links(this),
    factory(*this),
    sessionManager(
        qpid::SessionState::Configuration(
            conf.replayFlushLimit*1024, // convert kb to bytes.
            conf.replayHardLimit*1024),
        *this)
{
    if(conf.enableMgmt){
        QPID_LOG(info, "Management enabled");
        ManagementBroker::enableManagement (dataDir.isEnabled () ? dataDir.getPath () : string (),
                                            conf.mgmtPubInterval, this);
        managementAgent = management::ManagementAgent::getAgent ();
        ((ManagementBroker*) managementAgent.get())->setInterval (conf.mgmtPubInterval);
        qpid::management::PackageQpid packageInitializer (managementAgent);

        System* system = new System (dataDir.isEnabled () ? dataDir.getPath () : string ());
        systemObject = System::shared_ptr (system);

        mgmtObject = management::Broker::shared_ptr (new management::Broker (this, system, conf.port));
        mgmtObject->set_workerThreads    (conf.workerThreads);
        mgmtObject->set_maxConns         (conf.maxConnections);
        mgmtObject->set_connBacklog      (conf.connectionBacklog);
        mgmtObject->set_stagingThreshold (conf.stagingThreshold);
        mgmtObject->set_mgmtPubInterval  (conf.mgmtPubInterval);
        mgmtObject->set_version          (PACKAGE_VERSION);
        mgmtObject->set_dataDirEnabled   (dataDir.isEnabled ());
        mgmtObject->set_dataDir          (dataDir.getPath ());
        
        managementAgent->addObject (mgmtObject, 2, 1);

        // Since there is currently no support for virtual hosts, a placeholder object
        // representing the implied single virtual host is added here to keep the
        // management schema correct.
        Vhost* vhost = new Vhost (this);
        vhostObject = Vhost::shared_ptr (vhost);

        queues.setParent    (vhost);
        exchanges.setParent (vhost);
        links.setParent     (vhost);
    }

    // Early-Initialize plugins
    const Plugin::Plugins& plugins=Plugin::getPlugins();
    for (Plugin::Plugins::const_iterator i = plugins.begin();
         i != plugins.end();
         i++)
        (*i)->earlyInitialize(*this);

    // If no plugin store module registered itself, set up the null store.
    if (store == 0)
        setStore (new NullMessageStore (false));

    queues.setStore     (store);
    dtxManager.setStore (store);
    links.setStore      (store);

    exchanges.declare(empty, DirectExchange::typeName); // Default exchange.
    
    if (store != 0) {
        RecoveryManagerImpl recoverer(queues, exchanges, links, dtxManager, 
                                      conf.stagingThreshold);
        store->recover(recoverer);
    }

    //ensure standard exchanges exist (done after recovery from store)
    declareStandardExchange(amq_direct, DirectExchange::typeName);
    declareStandardExchange(amq_topic, TopicExchange::typeName);
    declareStandardExchange(amq_fanout, FanOutExchange::typeName);
    declareStandardExchange(amq_match, HeadersExchange::typeName);

    if(conf.enableMgmt) {
        exchanges.declare(qpid_management, ManagementExchange::typeName);
        Exchange::shared_ptr mExchange = exchanges.get (qpid_management);
        Exchange::shared_ptr dExchange = exchanges.get (amq_direct);
        ((ManagementBroker*) managementAgent.get())->setExchange (mExchange, dExchange);
        dynamic_pointer_cast<ManagementExchange>(mExchange)->setManagmentAgent
            ((ManagementBroker*) managementAgent.get());
    }
    else
        QPID_LOG(info, "Management not enabled");

    /**
     * SASL setup, can fail and terminate startup
     */
    if (conf.auth) {
#if HAVE_SASL
        int code = sasl_server_init(NULL, BROKER_SASL_NAME);
        if (code != SASL_OK) {
                // TODO: Figure out who owns the char* returned by
                // sasl_errstring, though it probably does not matter much
            throw Exception(sasl_errstring(code, NULL, NULL));
        }
        QPID_LOG(info, "SASL enabled");
#else
        throw Exception("Requested authentication but SASL unavailable");
#endif
    }

    // Initialize plugins
    for (Plugin::Plugins::const_iterator i = plugins.begin();
         i != plugins.end();
         i++)
        (*i)->initialize(*this);
}

void Broker::declareStandardExchange(const std::string& name, const std::string& type)
{
    bool storeEnabled = store != NULL;
    std::pair<Exchange::shared_ptr, bool> status = exchanges.declare(name, type, storeEnabled);
    if (status.second && storeEnabled) {
        store->create(*status.first, framing::FieldTable ());
    }
}


shared_ptr<Broker> Broker::create(int16_t port) 
{
    Options config;
    config.port=port;
    return create(config);
}

shared_ptr<Broker> Broker::create(const Options& opts) 
{
    return shared_ptr<Broker>(new Broker(opts));
}

void Broker::setStore (MessageStore* _store)
{
    assert (store == 0 && _store != 0);
    if (store == 0 && _store != 0)
        store = new MessageStoreModule (_store);
}

void Broker::run() {
    accept();
	
    Dispatcher d(poller);
    int numIOThreads = config.workerThreads;
    std::vector<Thread> t(numIOThreads-1);

    // Run n-1 io threads
    for (int i=0; i<numIOThreads-1; ++i)
        t[i] = Thread(d);

    // Run final thread
    d.run();
	
    // Now wait for n-1 io threads to exit
    for (int i=0; i<numIOThreads-1; ++i) {
        t[i].join();
    }
}

void Broker::shutdown() {
    // NB: this function must be async-signal safe, it must not
    // call any function that is not async-signal safe.
    // Any unsafe shutdown actions should be done in the destructor.
    poller->shutdown();
}

Broker::~Broker() {
    shutdown();
    ManagementBroker::shutdown ();
    delete store;    
    if (config.auth) {
#if HAVE_SASL
        sasl_done();
#endif
    }
}

ManagementObject::shared_ptr Broker::GetManagementObject(void) const
{
    return dynamic_pointer_cast<ManagementObject> (mgmtObject);
}

Manageable* Broker::GetVhostObject(void) const
{
    return vhostObject.get();
}

Manageable::status_t Broker::ManagementMethod (uint32_t methodId,
                                               Args&    args)
{
    Manageable::status_t status = Manageable::STATUS_UNKNOWN_METHOD;

    QPID_LOG (debug, "Broker::ManagementMethod [id=" << methodId << "]");

    switch (methodId)
    {
    case management::Broker::METHOD_ECHO :
        status = Manageable::STATUS_OK;
        break;
      case management::Broker::METHOD_CONNECT : {
        management::ArgsBrokerConnect& hp=
            dynamic_cast<management::ArgsBrokerConnect&>(args);

        if (hp.i_useSsl)
            return Manageable::STATUS_FEATURE_NOT_IMPLEMENTED;

        std::pair<Link::shared_ptr, bool> response =
            links.declare (hp.i_host, hp.i_port, hp.i_useSsl, hp.i_durable,
                           hp.i_authMechanism, hp.i_username, hp.i_password);
        if (hp.i_durable && response.second)
            store->create(*response.first);

        status = Manageable::STATUS_OK;
        break;
      }
    case management::Broker::METHOD_JOINCLUSTER :
    case management::Broker::METHOD_LEAVECLUSTER :
        status = Manageable::STATUS_NOT_IMPLEMENTED;
        break;
    }

    return status;
}

boost::shared_ptr<ProtocolFactory> Broker::getProtocolFactory() const {
    assert(protocolFactories.size() > 0);
    return protocolFactories[0];
}

void Broker::registerProtocolFactory(ProtocolFactory::shared_ptr protocolFactory) {
    protocolFactories.push_back(protocolFactory);
}

// TODO: This can only work if there is only one protocolFactory
uint16_t Broker::getPort() const  {
    return getProtocolFactory()->getPort();
}

// TODO: This should iterate over all protocolFactories
void Broker::accept() {
    for (unsigned int i = 0; i < protocolFactories.size(); ++i)
        protocolFactories[i]->accept(poller, &factory);
}


// TODO: How to chose the protocolFactory to use for the connection
void Broker::connect(
    const std::string& host, uint16_t port, bool /*useSsl*/,
    sys::ConnectionCodec::Factory* f,
    sys::ProtocolAccess* access)
{
    getProtocolFactory()->connect(poller, host, port, f ? f : &factory, access);
}

void Broker::connect(
    const Url& url, sys::ConnectionCodec::Factory* f)
{
    url.throwIfEmpty();
    TcpAddress addr=boost::get<TcpAddress>(url[0]);
    connect(addr.host, addr.port, false, f, (sys::ProtocolAccess*) 0);
}

}} // namespace qpid::broker

