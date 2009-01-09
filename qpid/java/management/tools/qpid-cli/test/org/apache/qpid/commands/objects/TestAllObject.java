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
package org.apache.qpid.commands.objects;

import javax.management.MBeanServerConnection;

import org.apache.qpid.ConnectionConstants;
import org.apache.qpid.Connector;
import org.apache.qpid.ConnectorFactory;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;

public class TestAllObject {
    Connector conn;
    MBeanServerConnection mbsc;
    AllObjects test;
    String test1,test2;

    @Before
    public void startup() throws Exception
    {
        conn  = ConnectorFactory.getConnector(ConnectionConstants.BROKER_HOSTNAME,ConnectionConstants.BROKER_PORT, ConnectionConstants.USERNAME, ConnectionConstants.PASSWORD);
        mbsc = conn.getMBeanServerConnection();
        test = new AllObjects(mbsc);
        test1 = "empty input1";
        test2 = "empty input2";


    }
    @Test
    public void TestSetQueryString()
    {
        test.setQueryString(test1,test2);
        Assert.assertEquals(test.querystring,"org.apache.qpid:*");

    }

    @After
    public void cleanup()
    {
        try{
            conn.getConnector().close();
        }catch(Exception ex)
        {
            ex.printStackTrace();
        }

    }
}
