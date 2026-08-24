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
#ifndef TWS_API_CLIENT_ORDERSTATUS_H
#define TWS_API_CLIENT_ORDERSTATUS_H

#include <string>
#include <algorithm>

enum class OrderStatus {
    ApiPending,
    ApiCancelled,
    PreSubmitted,
    PendingCancel,
    Cancelled,
    Submitted,
    Filled,
    Inactive,
    PendingSubmit,
    Unknown
};

struct OrderStatusUtil {

        static OrderStatus get(const std::string& apiString) {
            std::string lower = apiString;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

            if (lower == "apipending")    return OrderStatus::ApiPending;
            if (lower == "apicancelled")  return OrderStatus::ApiCancelled;
            if (lower == "presubmitted")  return OrderStatus::PreSubmitted;
            if (lower == "pendingcancel") return OrderStatus::PendingCancel;
            if (lower == "cancelled")     return OrderStatus::Cancelled;
            if (lower == "submitted")     return OrderStatus::Submitted;
            if (lower == "filled")        return OrderStatus::Filled;
            if (lower == "inactive")      return OrderStatus::Inactive;
            if (lower == "pendingsubmit") return OrderStatus::PendingSubmit;
            return OrderStatus::Unknown;
        }

    static const char* toString(OrderStatus status) {
        switch (status) {
            case OrderStatus::ApiPending:    return "ApiPending";
            case OrderStatus::ApiCancelled:  return "ApiCancelled";
            case OrderStatus::PreSubmitted:  return "PreSubmitted";
            case OrderStatus::PendingCancel: return "PendingCancel";
            case OrderStatus::Cancelled:     return "Cancelled";
            case OrderStatus::Submitted:     return "Submitted";
            case OrderStatus::Filled:        return "Filled";
            case OrderStatus::Inactive:      return "Inactive";
            case OrderStatus::PendingSubmit: return "PendingSubmit";
            case OrderStatus::Unknown:       return "Unknown";
            default:                         return "Unknown";
        }
    }

    static bool isActive(OrderStatus status) {
        return status == OrderStatus::PreSubmitted
            || status == OrderStatus::PendingCancel
            || status == OrderStatus::Submitted
            || status == OrderStatus::PendingSubmit;
    }

    static bool isTerminal(OrderStatus status) {
        return status == OrderStatus::Filled
            || status == OrderStatus::Cancelled
            || status == OrderStatus::Inactive
            || status == OrderStatus::ApiCancelled;
    }
};

#endif
