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


/**
 *  topic_publisher.cpp:
 *
 *  This program is one of three programs designed to be used
 *  together. These programs use the topic exchange.
 *  
 *    topic_config_queues.cpp:
 *
 *      Creates a queue on a broker, binding a routing key to route
 *      messages to that queue.
 *
 *    topic_publisher.cpp (this program):
 *
 *      Publishes to a broker, specifying a routing key.
 *
 *    topic_listener.cpp
 *
 *      Reads from a queue on the broker using a message listener.
 *
 */


#include <qpid/client/Connection.h>
#include <qpid/client/Session.h>
#include <qpid/client/AsyncSession.h>
#include <qpid/client/Message.h>


#include <unistd.h>
#include <cstdlib>
#include <iostream>

#include <sstream>

using namespace qpid::client;
using namespace qpid::framing;

using std::stringstream;
using std::string;

void publish_messages(Session& session, string exchange, string routing_key)
{
  Message message;

  // Set the routing key once, we'll use the same routing key for all
  // messages.

  message.getDeliveryProperties().setRoutingKey(routing_key); 
  for (int i=0; i<5; i++) {
    stringstream message_data;
    message_data << "Message " << i;

    message.setData(message_data.str());
    // Asynchronous transfer sends messages as quickly as
    // possible without waiting for confirmation.
    async(session).messageTransfer(arg::content=message, arg::destination=exchange);
  }

}

/*
 * no_more_messages()
 * 
 * Send a message to indicate that no more messages are coming.
 * Use the 'control' routing key (see comments in topic_config_queues.cpp).
 *
 */

void no_more_messages(Session& session, string exchange)
{
  Message message;

  message.getDeliveryProperties().setRoutingKey("control"); 
  message.setData("That's all, folks!");
  session.messageTransfer(arg::content=message, arg::destination=exchange); 
}

int main(int argc, char** argv) {
    const char* host = argc>1 ? argv[1] : "127.0.0.1";
    int port = argc>2 ? atoi(argv[2]) : 5672;
    std::string exchange = argc>3 ? argv[3] : "amq.topic";
    Connection connection;
    Message message;
    try {
        connection.open(host, port);
        Session session =  connection.newSession();

  //--------- Main body of program --------------------------------------------

        publish_messages(session, exchange, "usa.news");
        publish_messages(session, exchange, "usa.weather");
        publish_messages(session, exchange, "europe.news");
        publish_messages(session, exchange, "europe.weather");

        no_more_messages(session, exchange);

  //-----------------------------------------------------------------------------

        connection.close();
        return 0;
    } catch(const std::exception& error) {
        std::cout << error.what() << std::endl;
    }
    return 1;
}


