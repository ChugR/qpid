/*
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
 */
package org.apache.qpid.amqp_1_0.jms.impl;

import org.apache.qpid.amqp_1_0.client.Sender;
import org.apache.qpid.amqp_1_0.jms.TemporaryTopic;

import javax.jms.JMSException;

public class TemporaryTopicImpl extends TopicImpl implements TemporaryTopic
{
    private Sender _sender;

    protected TemporaryTopicImpl(String address, Sender sender)
    {
        super(address);
        _sender = sender;
    }

    public void delete() throws JMSException
    {
        try
        {
            if(_sender != null)
            {
                _sender.close();
                _sender = null;
            }
        }
        catch (Sender.SenderClosingException e)
        {
            final JMSException jmsException = new JMSException(e.getMessage());
            jmsException.setLinkedException(e);
            throw jmsException;
        }
    }

}
