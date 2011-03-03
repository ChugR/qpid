package org.apache.qpid.client;
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

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;

import javax.jms.ExceptionListener;
import javax.jms.JMSException;
import javax.jms.XASession;

import org.apache.qpid.AMQDisconnectedException;
import org.apache.qpid.AMQException;
import org.apache.qpid.client.failover.FailoverException;
import org.apache.qpid.client.failover.FailoverHandler;
import org.apache.qpid.client.failover.FailoverProtectedOperation;
import org.apache.qpid.client.failover.FailoverState;
import org.apache.qpid.configuration.ClientProperties;
import org.apache.qpid.framing.ProtocolVersion;
import org.apache.qpid.jms.BrokerDetails;
import org.apache.qpid.jms.ChannelLimitReachedException;
import org.apache.qpid.jms.Session;
import org.apache.qpid.protocol.AMQConstant;
import org.apache.qpid.thread.Threading;
import org.apache.qpid.transport.Connection;
import org.apache.qpid.transport.ConnectionClose;
import org.apache.qpid.transport.ConnectionException;
import org.apache.qpid.transport.ConnectionListener;
import org.apache.qpid.transport.ConnectionSettings;
import org.apache.qpid.transport.ProtocolVersionException;
import org.apache.qpid.transport.TransportException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class AMQConnectionDelegate_0_10 implements AMQConnectionDelegate, ConnectionListener
{
    /**
     * This class logger.
     */
    private static final Logger _logger = LoggerFactory.getLogger(AMQConnectionDelegate_0_10.class);

    /**
     * The AMQ Connection.
     */
    private AMQConnection _conn;

    /**
     * The QpidConeection instance that is mapped with thie JMS connection.
     */
    org.apache.qpid.transport.Connection _qpidConnection;
    private ConnectionException exception = null;

    //--- constructor
    public AMQConnectionDelegate_0_10(AMQConnection conn)
    {
        _conn = conn;
        _qpidConnection = new Connection();
        _qpidConnection.addConnectionListener(this);
    }

    /**
     * create a Session and start it if required.
     */
    public Session createSession(boolean transacted, int acknowledgeMode, int prefetchHigh, int prefetchLow)
            throws JMSException
    {
        _conn.checkNotClosed();

        if (_conn.channelLimitReached())
        {
            throw new ChannelLimitReachedException(_conn.getMaximumChannelCount());
        }

        int channelId = _conn.getNextChannelID();
        AMQSession session;
        try
        {
            session = new AMQSession_0_10(_qpidConnection, _conn, channelId, transacted, acknowledgeMode, prefetchHigh,
                                          prefetchLow);
            _conn.registerSession(channelId, session);
            if (_conn._started)
            {
                session.start();
            }
        }
        catch (Exception e)
        {
            _logger.error("exception creating session:", e);
            throw new JMSAMQException("cannot create session", e);
        }
        return session;
    }

    /**
     * Create an XASession with default prefetch values of:
     * High = MaxPrefetch
     * Low  = MaxPrefetch / 2
     * @return XASession
     * @throws JMSException
     */
    public XASession createXASession() throws JMSException
    {
        return createXASession((int) _conn.getMaxPrefetch(), (int) _conn.getMaxPrefetch() / 2);
    }

    /**
     * create an XA Session and start it if required.
     */
    public XASession createXASession(int prefetchHigh, int prefetchLow) throws JMSException
    {
        _conn.checkNotClosed();

        if (_conn.channelLimitReached())
        {
            throw new ChannelLimitReachedException(_conn.getMaximumChannelCount());
        }

        int channelId = _conn.getNextChannelID();
        XASessionImpl session;
        try
        {
            session = new XASessionImpl(_qpidConnection, _conn, channelId, prefetchHigh, prefetchLow);
            _conn.registerSession(channelId, session);
            if (_conn._started)
            {
                session.start();
            }
        }
        catch (Exception e)
        {
            throw new JMSAMQException("cannot create session", e);
        }
        return session;
    }


    /**
     * Make a connection with the broker
     *
     * @param brokerDetail The detail of the broker to connect to.
     * @throws IOException
     * @throws AMQException
     */
    public ProtocolVersion makeBrokerConnection(BrokerDetails brokerDetail) throws IOException, AMQException
    {
        try
        {
            if (_logger.isDebugEnabled())
            {
                _logger.debug("connecting to host: " + brokerDetail.getHost() +
                        " port: " + brokerDetail.getPort() +
                        " vhost: " + _conn.getVirtualHost() +
						" username: " + _conn.getUsername() +
						" password: " + _conn.getPassword() +
						" transport: " + brokerDetail.getTransport());
            }

            ConnectionSettings conSettings = retriveConnectionSettings(brokerDetail);
            _qpidConnection.connect(conSettings);

            _conn._connected = true;
            _conn.setUsername(_qpidConnection.getUserID());
            _conn.setMaximumChannelCount(_qpidConnection.getChannelMax());
            _conn._failoverPolicy.attainedConnection();
        }
        catch (ProtocolVersionException pe)
        {
            return new ProtocolVersion(pe.getMajor(), pe.getMinor());
        }
        catch (ConnectionException ce)
        {
            AMQConstant code = AMQConstant.REPLY_SUCCESS;
            if (ce.getClose() != null && ce.getClose().getReplyCode() != null)
            {
                code = AMQConstant.getConstant(ce.getClose().getReplyCode().getValue());
            }
            String msg = "Cannot connect to broker: " + ce.getMessage();
            throw new AMQException(code, msg, ce);
        }

        return null;
    }

    public void failoverPrep()
    {
        _logger.info("Preparing failover");
        List<AMQSession> sessions = new ArrayList<AMQSession>(_conn.getSessions().values());
        for (AMQSession s : sessions)
        {
            s.failoverPrep();
        }
    }

    public void resubscribeSessions() throws JMSException, AMQException, FailoverException
    {
        _logger.info("Resuming connection");
        getQpidConnection().resume();
        List<AMQSession> sessions = new ArrayList<AMQSession>(_conn.getSessions().values());
        _logger.info(String.format("Resubscribing sessions = %s sessions.size=%d", sessions, sessions.size()));
        for (AMQSession s : sessions)
        {
            s.resubscribe();
        }
    }

    public void closeConnection(long timeout) throws JMSException, AMQException
    {
        try
        {
            _qpidConnection.close();
        }
        catch (TransportException e)
        {
            throw new AMQException(e.getMessage(), e);
        }
    }

    public void opened(Connection conn) {}

    public void exception(Connection conn, ConnectionException exc)
    {
        if (exception != null)
        {
            _logger.error("previous exception", exception);
        }

        exception = exc;
    }

    public void closed(Connection conn)
    {
        ConnectionException exc = exception;
        exception = null;

        if (exc == null)
        {
            return;
        }

        ConnectionClose close = exc.getClose();
        if (close == null)
        {
            startFailoverThread();
        }
        else
        {
            _conn.exceptionReceived(exc);
        }
    }


    /** See {@link FailoverHandler} to see rationale for separate thread. */
    private void startFailoverThread()
    {
        final Thread failoverThread;
        try
        {
            failoverThread = Threading.getThreadFactory().createThread(new Runnable()
            {
                public void run()
                {
                    _conn.getProtocolHandler().setFailoverLatch(new CountDownLatch(1));

                    synchronized (_conn.getFailoverMutex())
                    {
                        try
                        {
                            if (!_conn.firePreFailover(false))
                            {
                                _logger.info("Failover process veto-ed by client");
                                _conn.exceptionReceived(new AMQDisconnectedException("Failover was vetoed by client", null));
                                return;
                            }

                            if (_conn.attemptReconnection())
                            {
                                _conn.failoverPrep();
                                if (_conn.firePreResubscribe())
                                {
                                    _logger.info("Resubscribing on new connection");
                                    _conn.resubscribeSessions();
                                }
                                else
                                {
                                    _logger.info("Client vetoed automatic resubscription");
                                }

                                _conn.fireFailoverComplete();
                                _logger.info("Connection failover completed successfully");
                            }
                            else
                            {
                                _conn.exceptionReceived(new AMQDisconnectedException("Server closed connection and no failover was successful", null));
                            }
                            return;
                        }
                        catch (Exception e)
                        {
                            _logger.info("Failover process failed - exception being propagated by protocol handler", e);
                            _conn.exceptionReceived(e);
                        }
                        finally
                        {
                            _conn.getProtocolHandler().getFailoverLatch().countDown();
                            _conn.getProtocolHandler().setFailoverLatch(null);
                        }
                    }
                }
            });
        }
        catch (Exception e)
        {
            throw new RuntimeException("Failed to create thread", e);
        }

        failoverThread.setName("Failover");
        // Do not inherit daemon-ness from current thread as this can be a daemon
        // thread such as a AnonymousIoService thread.
        failoverThread.setDaemon(false);
        failoverThread.start();
    }

    public <T, E extends Exception> T executeRetrySupport(FailoverProtectedOperation<T,E> operation) throws E
    {
        try
        {
            return operation.execute();
        }
        catch (FailoverException e)
        {
            throw new RuntimeException(e);
        }
    }

    public int getMaxChannelID()
    {
        //For a negotiated channelMax N, there are channels 0 to N-1 available.
        return _qpidConnection.getChannelMax() - 1;
    }

    public int getMinChannelID()
    {
        return Connection.MIN_USABLE_CHANNEL_NUM;
    }

    public ProtocolVersion getProtocolVersion()
    {
        return ProtocolVersion.v0_10;
    }
    
    private ConnectionSettings retriveConnectionSettings(BrokerDetails brokerDetail)
    {
        ConnectionSettings conSettings = new ConnectionSettings();
        conSettings.setHost(brokerDetail.getHost());
        conSettings.setPort(brokerDetail.getPort());
        conSettings.setVhost(_conn.getVirtualHost());
        conSettings.setUsername(_conn.getUsername());
        conSettings.setPassword(_conn.getPassword());
        conSettings.setProtocol(brokerDetail.getTransport());       
 
        // ------------ sasl options ---------------
        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_SASL_MECHS) != null)
        {
            conSettings.setSaslMechs(
                    brokerDetail.getProperty(BrokerDetails.OPTIONS_SASL_MECHS));
        }

        // Sun SASL Kerberos client uses the
        // protocol + servername as the service key.
        
        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_SASL_PROTOCOL_NAME) != null)
        {
            conSettings.setSaslProtocol(
                    brokerDetail.getProperty(BrokerDetails.OPTIONS_SASL_PROTOCOL_NAME));
        }
        
        
        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_SASL_SERVER_NAME) != null)
        {
            conSettings.setSaslServerName(
                    brokerDetail.getProperty(BrokerDetails.OPTIONS_SASL_SERVER_NAME));
        }
                        
        conSettings.setUseSASLEncryption(
                brokerDetail.getBooleanProperty(BrokerDetails.OPTIONS_SASL_ENCRYPTION));

        // ------------- ssl options ---------------------
        conSettings.setUseSSL(brokerDetail.getBooleanProperty(BrokerDetails.OPTIONS_SSL));
        
        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_TRUST_STORE) != null)
        {
            conSettings.setTrustStorePath(
                    brokerDetail.getProperty(BrokerDetails.OPTIONS_TRUST_STORE));
        }

        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_TRUST_STORE_PASSWORD) != null)
        {
            conSettings.setTrustStorePassword(
                    brokerDetail.getProperty(BrokerDetails.OPTIONS_TRUST_STORE_PASSWORD));
        }
        
        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_KEY_STORE) != null)
        {
            conSettings.setKeyStorePath(
                    brokerDetail.getProperty(BrokerDetails.OPTIONS_KEY_STORE));
        }
        
        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_KEY_STORE_PASSWORD) != null)
        {
            conSettings.setKeyStorePassword(
                    brokerDetail.getProperty(BrokerDetails.OPTIONS_KEY_STORE_PASSWORD));
        }

        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_SSL_CERT_ALIAS) != null)
        {
            conSettings.setCertAlias(
                    brokerDetail.getProperty(BrokerDetails.OPTIONS_SSL_CERT_ALIAS));
        }
        // ----------------------------
        
        conSettings.setVerifyHostname(brokerDetail.getBooleanProperty(BrokerDetails.OPTIONS_SSL_VERIFY_HOSTNAME));
        
        // Pass client name from connection URL
        Map<String, Object> clientProps = new HashMap<String, Object>();
        try
        {
            clientProps.put("clientName", _conn.getClientID());
	        conSettings.setClientProperties(clientProps);
        }
        catch (JMSException e)
        {
            // Ignore
        }
        
        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_TCP_NO_DELAY) != null)
        {
            conSettings.setTcpNodelay(
                    brokerDetail.getBooleanProperty(BrokerDetails.OPTIONS_TCP_NO_DELAY));
        }
        
        conSettings.setHeartbeatInterval(getHeartbeatInterval(brokerDetail));
        
        return conSettings;
    }
    
    // The idle_timeout prop is in milisecs while
    // the new heartbeat prop is in secs
    private int getHeartbeatInterval(BrokerDetails brokerDetail)
    {
        int heartbeat = 0;
        if (brokerDetail.getProperty(BrokerDetails.OPTIONS_IDLE_TIMEOUT) != null)
        {
            _logger.warn("Broker property idle_timeout=<mili_secs> is deprecated, please use heartbeat=<secs>");
            heartbeat = Integer.parseInt(brokerDetail.getProperty(BrokerDetails.OPTIONS_IDLE_TIMEOUT))/1000;
        }
        else if (brokerDetail.getProperty(BrokerDetails.OPTIONS_HEARTBEAT) != null)
        {
            heartbeat = Integer.parseInt(brokerDetail.getProperty(BrokerDetails.OPTIONS_HEARTBEAT));
        }
        else if (Integer.getInteger(ClientProperties.IDLE_TIMEOUT_PROP_NAME) != null) 
        {
            heartbeat = Integer.getInteger(ClientProperties.IDLE_TIMEOUT_PROP_NAME)/1000;
            _logger.warn("JVM arg -Didle_timeout=<mili_secs> is deprecated, please use -Dqpid.heartbeat=<secs>");
        }
        else
        {
            heartbeat = Integer.getInteger(ClientProperties.HEARTBEAT,ClientProperties.HEARTBEAT_DEFAULT);
        } 
        return heartbeat;
    }
    
    protected org.apache.qpid.transport.Connection getQpidConnection()
    {
        return _qpidConnection;
    }
}
