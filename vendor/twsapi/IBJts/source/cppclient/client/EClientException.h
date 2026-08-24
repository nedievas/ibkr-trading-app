/*
 * C++ TWS API Client
 *
 * Copyright (C) 2013-2026  Interactive Brokers LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#ifndef TWS_API_CLIENT_ECLIENTEXCEPTION_H
#define TWS_API_CLIENT_ECLIENTEXCEPTION_H

#include <iostream>
#include <exception>
#include "TwsSocketClientErrors.h"

struct EClientException : public std::exception {

private:
    CodeMsgPair m_error;
    std::string m_text;

public:
    CodeMsgPair error() const { return m_error; }
    std::string text() const  { return m_text; }

    EClientException(CodeMsgPair error, std::string text) : m_error(error), m_text(text) { }
};

#endif