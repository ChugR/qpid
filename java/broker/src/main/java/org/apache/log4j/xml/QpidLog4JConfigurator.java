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
package org.apache.log4j.xml;

import org.apache.log4j.Level;
import org.apache.log4j.Logger;
import org.xml.sax.ErrorHandler;
import org.xml.sax.SAXException;
import org.xml.sax.SAXParseException;

import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import java.io.File;
import java.io.IOException;
import java.util.Map;
import java.util.concurrent.locks.ReentrantLock;

/**
 * Substitute for the Log4J XMLWatchdog (as used by DOMConfigurator.configureAndWatch)
 * 
 * Extends the default behaviour with a strict parser check on the XML file before allowing the reconfiguration to proceed,
 * ensuring that any parser error or warning prevents initiation of a configuration update by Log4J, which aborts mid-update
 * upon fatal errors from the parser and proceeds in the event of 'regular' parser errors and warnings, in all cases allowing
 * startup to proceed with whatever half-baked configuration then exists.
 */
public class QpidLog4JConfigurator
{
    //lock to protect access to the configuration file
    //shared with LoggingManagementMBean
    public static final ReentrantLock LOCK = new ReentrantLock();
    private static Logger _logger;
    private static DOMConfigurator domConfig = new DOMConfigurator();

    private QpidLog4JConfigurator()
    {
        //no instances
    }
    
    public static void configure(String filename) throws IOException, ParserConfigurationException, 
                                                         SAXException, IllegalLoggerLevelException
    {
        try
        {
            LOCK.lock();

            parseXMLConfigFile(filename);

            DOMConfigurator.configure(filename);
            
            if(_logger == null)
            {
                _logger = Logger.getLogger(QpidLog4JConfigurator.class);
            }
        }
        finally
        {
            LOCK.unlock();
        }
    }
    
    public static void configureAndWatch(String filename, long delay) throws IOException, ParserConfigurationException, 
                                                                             SAXException, IllegalLoggerLevelException
    {
        parseXMLConfigFile(filename);
        
        QpidLog4JXMLWatchdog watchdog = new QpidLog4JXMLWatchdog(filename);
        watchdog.setDelay(delay);
        watchdog.start();
    }
    
    private static void parseXMLConfigFile(String fileName) throws IOException, SAXException,
                                                                                        ParserConfigurationException
    {
        try
        {
            LOCK.lock();

            //check file was specified, exists, and is readable
            if(fileName == null)
            {
                throw new IOException("Provided log4j XML configuration filename was null");
            }

            File configFile = new File(fileName);

            if (!configFile.exists())
            {
                throw new IOException("The log4j XML configuration file does not exist: " + fileName);
            }
            else if (!configFile.canRead())
            {
                throw new IOException("The log4j XML configuration file is not readable: " + fileName);
            }

            //parse it
            DocumentBuilderFactory docFactory = DocumentBuilderFactory.newInstance();
            DocumentBuilder docBuilder;

            ErrorHandler errHandler = new QpidLog4JSaxErrorHandler();

            docFactory.setValidating(true);
            docBuilder = docFactory.newDocumentBuilder();
            docBuilder.setErrorHandler(errHandler);
            docBuilder.setEntityResolver(new Log4jEntityResolver());
            docBuilder.parse(fileName);
        }
        finally
        {
            LOCK.unlock();
        }
    }
    
    public static class QpidLog4JSaxErrorHandler implements ErrorHandler
    {
        public void error(SAXParseException e) throws SAXException
        {
            if(_logger != null)
            {
                _logger.warn(constructMessage("Error parsing XML file", e));
            }
            else
            {
                System.err.println(constructMessage("Error parsing XML file", e));
            }
        }

        public void fatalError(SAXParseException e) throws SAXException
        {
            throw new SAXException(constructMessage("Fatal error parsing XML file", e));
        }

        public void warning(SAXParseException e) throws SAXException
        {
            if(_logger != null)
            {
                _logger.warn(constructMessage("Warning parsing XML file", e));
            }
            else
            {
                System.err.println(constructMessage("Warning parsing XML file", e));
            }
        }

        private static String constructMessage(final String msg, final SAXParseException ex)
        {
            return msg + ": Line " + ex.getLineNumber()+" column " +ex.getColumnNumber() + ": " + ex.getMessage();
        }
    }
    
    private static class QpidLog4JXMLWatchdog extends XMLWatchdog
    {
        public QpidLog4JXMLWatchdog(String filename)
        {
            super(filename);
        }

        public void doOnChange()
        {
            try
            {
                LOCK.lock();
                
                try
                {
                    parseXMLConfigFile(filename);
                }
                catch (Exception e)
                {
                    //logger will be instantiated following first configuration success, which has been pre-validated
                    //and so the null check should never actually be required.
                    if(_logger != null)
                    {
                        _logger.warn("Parsing the log4j XML configuration file generated errors/warnings. " +
                                "The new configuration was not applied. Correct the issues to prompt " +
                                "another update attempt: " + e.getMessage());
                    }
                    return;
                }

                //everything checked was ok, let the normal update process proceed
                super.doOnChange();

                //a configuration has now been applied, enable logging for future attempts
                if(_logger == null)
                {
                    _logger = Logger.getLogger(QpidLog4JConfigurator.class);
                }

                _logger.info("Applied log4j configuration from: " + filename);
            }
            finally
            {
                LOCK.unlock();
            }

        }
    }

    private static void checkLevel(String loggerName, String levelString) throws IllegalLoggerLevelException
    {
        if("null".equalsIgnoreCase(levelString) || "inherited".equalsIgnoreCase(levelString))
        {
            //the string "null" signals to inherit from a parent logger 
            return;
        }
        
        Level level = Level.toLevel(levelString);

        //above Level.toLevel call returns a DEBUG Level if the request fails. Check the result.
        if (level.equals(Level.DEBUG) && !(levelString.equalsIgnoreCase("debug")))
        {
            //received DEBUG but we did not ask for it, the Level request failed.
            throw new IllegalLoggerLevelException("Level '" + levelString + "' specified for Logger '" + loggerName + "' is invalid");
        }
    }
    
    public static class IllegalLoggerLevelException extends Exception
    {
        private static final long serialVersionUID = 1L;

        public IllegalLoggerLevelException(String msg)
        {
            super(msg);
        }
    }
}

