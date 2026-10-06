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
#ifndef TWS_API_CLIENT_WSHEVENTDATA_H
#define TWS_API_CLIENT_WSHEVENTDATA_H

#include <string>

struct WshEventData
{
    int conId;
    std::string filter;
    bool fillWatchlist;
    bool fillPortfolio;
    bool fillCompetitors;
    std::string startDate;
    std::string endDate;
    int totalLimit;

	WshEventData(int conId_, bool fillWatchlist_, bool fillPortfolio_, bool fillCompetitors_, std::string startDate_, std::string endDate_, int totalLimit_)
	{
        this->conId = conId_;
        this->filter = "";
        this->fillWatchlist = fillWatchlist_;
        this->fillPortfolio = fillPortfolio_;
        this->fillCompetitors = fillCompetitors_;
        this->startDate = startDate_;
        this->endDate = endDate_;
        this->totalLimit = totalLimit_;
    }

    WshEventData(std::string filter_, bool fillWatchlist_, bool fillPortfolio_, bool fillCompetitors_, std::string startDate_, std::string endDate_, int totalLimit_)
    {
        this->conId = INT_MAX;
        this->filter = filter_;
        this->fillWatchlist = fillWatchlist_;
        this->fillPortfolio = fillPortfolio_;
        this->fillCompetitors = fillCompetitors_;
        this->startDate = startDate_;
        this->endDate = endDate_;
        this->totalLimit = totalLimit_;
    }
};

#endif // wsheventdata_def
