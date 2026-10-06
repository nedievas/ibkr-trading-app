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

#ifndef TWS_API_CLIENT_INELIGIBILITY_REASON_H
#define TWS_API_CLIENT_INELIGIBILITY_REASON_H

#include <string>

struct IneligibilityReason
{
    IneligibilityReason() {}
    IneligibilityReason(const std::string& p_id, const std::string& p_description)
        : id(p_id), description(p_description)
    {}

    std::string id;
    std::string description;

};

typedef std::shared_ptr<IneligibilityReason> IneligibilityReasonSPtr;
typedef std::vector<IneligibilityReasonSPtr> IneligibilityReasonList;
typedef std::shared_ptr<IneligibilityReasonList> IneligibilityReasonListSPtr;

#endif