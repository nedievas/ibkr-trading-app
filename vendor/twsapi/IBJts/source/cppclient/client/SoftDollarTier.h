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
#ifndef TWS_API_CLIENT_SOFTDOLLARTIER_H
#define TWS_API_CLIENT_SOFTDOLLARTIER_H

#include "platformspecific.h"
#include <string>

class TWSAPIDLLEXP SoftDollarTier
{
	std::string m_name, m_val, m_displayName;

public:
	SoftDollarTier(const std::string& name = "", const std::string& val = "", const std::string& displayName = "");

	std::string name() const;
	std::string val() const;
	std::string displayName() const;
};

#endif