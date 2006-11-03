/*
 *
 * Copyright (c) 2006 The Apache Software Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include "qpid/framing/ProtocolVersion.h"
#include <sstream>

using namespace qpid::framing;

ProtocolVersion::ProtocolVersion() {}

ProtocolVersion::ProtocolVersion(u_int8_t _major, u_int8_t _minor) : 
    major_(_major),
    minor_(_minor)
{}

ProtocolVersion::ProtocolVersion(const ProtocolVersion::ProtocolVersion& p):
    major_(p.major_),
    minor_(p.minor_)
{}

ProtocolVersion::~ProtocolVersion()
{}
    
bool  ProtocolVersion::equals(u_int8_t _major, u_int8_t _minor) const
{
    return major_ == _major && minor_ == _minor;
}

bool ProtocolVersion::equals(const ProtocolVersion::ProtocolVersion& p) const
{
    return major_ == p.major_ && minor_ == p.minor_;
}

const std::string ProtocolVersion::toString() const
{
    std::stringstream ss;
    ss << major_ << "-" << minor_;
    return ss.str();
}

ProtocolVersion::ProtocolVersion ProtocolVersion::operator=(const ProtocolVersion& p)
{
    major_ = p.major_;
    minor_ = p.minor_;
    return *this;
}

