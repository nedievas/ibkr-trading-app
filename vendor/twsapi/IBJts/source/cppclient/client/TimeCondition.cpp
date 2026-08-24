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

#include "StdAfx.h"
#include "TimeCondition.h"

std::string TimeCondition::valueToString() const {
	return m_time;
}

void TimeCondition::valueFromString(const std::string & v) {
	m_time = v;
}

std::string TimeCondition::toString() {
	return "time" + OperatorCondition::toString();
}

std::string TimeCondition::time() {
	return m_time;
}

void TimeCondition::time(const std::string & time) {
	m_time = time;
}
