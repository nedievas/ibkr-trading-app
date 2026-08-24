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
#ifndef TWS_API_CLIENT_ORDERSTATE_H
#define TWS_API_CLIENT_ORDERSTATE_H

#include "Order.h"

struct OrderAllocation
{
	OrderAllocation()
	{
		account = "";
		position = UNSET_DECIMAL;
		positionDesired = UNSET_DECIMAL;
		positionAfter = UNSET_DECIMAL;
		desiredAllocQty = UNSET_DECIMAL;
		allowedAllocQty = UNSET_DECIMAL;
		isMonetary = false;
	}

	std::string account;
	Decimal position;
	Decimal positionDesired;
	Decimal positionAfter;
	Decimal desiredAllocQty;
	Decimal allowedAllocQty;
	bool isMonetary;

	bool operator==(const OrderAllocation& other) const
	{
		return (account == other.account);
	}
};

typedef std::shared_ptr<OrderAllocation> OrderAllocationSPtr;
typedef std::vector<OrderAllocationSPtr> OrderAllocationList;
typedef std::shared_ptr<OrderAllocationList> OrderAllocationListSPtr;

struct OrderState {

	explicit OrderState()
		:
		commissionAndFees(UNSET_DOUBLE),
		minCommissionAndFees(UNSET_DOUBLE),
		maxCommissionAndFees(UNSET_DOUBLE),
		initMarginBeforeOutsideRTH(UNSET_DOUBLE),
		maintMarginBeforeOutsideRTH(UNSET_DOUBLE),
		equityWithLoanBeforeOutsideRTH(UNSET_DOUBLE),
		initMarginChangeOutsideRTH(UNSET_DOUBLE),
		maintMarginChangeOutsideRTH(UNSET_DOUBLE),
		equityWithLoanChangeOutsideRTH(UNSET_DOUBLE),
		initMarginAfterOutsideRTH(UNSET_DOUBLE),
		maintMarginAfterOutsideRTH(UNSET_DOUBLE),
		equityWithLoanAfterOutsideRTH(UNSET_DOUBLE),
		suggestedSize(UNSET_DECIMAL)
	{}

	std::string status;

	std::string initMarginBefore;
	std::string maintMarginBefore;
	std::string equityWithLoanBefore;
	std::string initMarginChange;
	std::string maintMarginChange;
	std::string equityWithLoanChange;
	std::string initMarginAfter;
	std::string maintMarginAfter;
	std::string equityWithLoanAfter;

	double  commissionAndFees;
	double  minCommissionAndFees;
	double  maxCommissionAndFees;
	std::string commissionAndFeesCurrency;
	std::string marginCurrency;
	double initMarginBeforeOutsideRTH;
	double maintMarginBeforeOutsideRTH;
	double equityWithLoanBeforeOutsideRTH;
	double initMarginChangeOutsideRTH;
	double maintMarginChangeOutsideRTH;
	double equityWithLoanChangeOutsideRTH;
	double initMarginAfterOutsideRTH;
	double maintMarginAfterOutsideRTH;
	double equityWithLoanAfterOutsideRTH;
	Decimal suggestedSize;
	std::string rejectReason;
	OrderAllocationListSPtr orderAllocations;
	std::string warningText;

	std::string completedTime;
	std::string completedStatus;
};

#endif
