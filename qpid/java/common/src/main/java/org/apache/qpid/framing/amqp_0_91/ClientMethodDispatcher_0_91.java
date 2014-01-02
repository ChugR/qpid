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

/*
 * This file is auto-generated by Qpid Gentools v.0.1 - do not modify.
 * Supported AMQP version:
  *   0-91
  */

package org.apache.qpid.framing.amqp_0_91;

import org.apache.qpid.AMQException;
import org.apache.qpid.framing.*;

public interface ClientMethodDispatcher_0_91 extends ClientMethodDispatcher
{

    public boolean dispatchBasicCancelOk(BasicCancelOkBody body, int channelId) throws AMQException;
    public boolean dispatchBasicConsumeOk(BasicConsumeOkBody body, int channelId) throws AMQException;
    public boolean dispatchBasicDeliver(BasicDeliverBody body, int channelId) throws AMQException;
    public boolean dispatchBasicGetEmpty(BasicGetEmptyBody body, int channelId) throws AMQException;
    public boolean dispatchBasicGetOk(BasicGetOkBody body, int channelId) throws AMQException;
    public boolean dispatchBasicQosOk(BasicQosOkBody body, int channelId) throws AMQException;
    public boolean dispatchBasicRecoverSyncOk(BasicRecoverSyncOkBody body, int channelId) throws AMQException;
    public boolean dispatchBasicReturn(BasicReturnBody body, int channelId) throws AMQException;
    public boolean dispatchChannelClose(ChannelCloseBody body, int channelId) throws AMQException;
    public boolean dispatchChannelCloseOk(ChannelCloseOkBody body, int channelId) throws AMQException;
    public boolean dispatchChannelFlow(ChannelFlowBody body, int channelId) throws AMQException;
    public boolean dispatchChannelFlowOk(ChannelFlowOkBody body, int channelId) throws AMQException;
    public boolean dispatchChannelOpenOk(ChannelOpenOkBody body, int channelId) throws AMQException;
    public boolean dispatchConnectionClose(ConnectionCloseBody body, int channelId) throws AMQException;
    public boolean dispatchConnectionCloseOk(ConnectionCloseOkBody body, int channelId) throws AMQException;
    public boolean dispatchConnectionOpenOk(ConnectionOpenOkBody body, int channelId) throws AMQException;
    public boolean dispatchConnectionSecure(ConnectionSecureBody body, int channelId) throws AMQException;
    public boolean dispatchConnectionStart(ConnectionStartBody body, int channelId) throws AMQException;
    public boolean dispatchConnectionTune(ConnectionTuneBody body, int channelId) throws AMQException;
    public boolean dispatchExchangeBoundOk(ExchangeBoundOkBody body, int channelId) throws AMQException;
    public boolean dispatchExchangeDeclareOk(ExchangeDeclareOkBody body, int channelId) throws AMQException;
    public boolean dispatchExchangeDeleteOk(ExchangeDeleteOkBody body, int channelId) throws AMQException;
    public boolean dispatchQueueBindOk(QueueBindOkBody body, int channelId) throws AMQException;
    public boolean dispatchQueueDeclareOk(QueueDeclareOkBody body, int channelId) throws AMQException;
    public boolean dispatchQueueDeleteOk(QueueDeleteOkBody body, int channelId) throws AMQException;
    public boolean dispatchQueuePurgeOk(QueuePurgeOkBody body, int channelId) throws AMQException;
    public boolean dispatchQueueUnbindOk(QueueUnbindOkBody body, int channelId) throws AMQException;
    public boolean dispatchTxCommitOk(TxCommitOkBody body, int channelId) throws AMQException;
    public boolean dispatchTxRollbackOk(TxRollbackOkBody body, int channelId) throws AMQException;
    public boolean dispatchTxSelectOk(TxSelectOkBody body, int channelId) throws AMQException;

}