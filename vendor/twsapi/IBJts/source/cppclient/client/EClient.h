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
#ifndef TWS_API_CLIENT_ECLIENT_H
#define TWS_API_CLIENT_ECLIENT_H

#include <memory>
#include <string>
#include <vector>
#include <ostream>
#include <map>
#include "platformspecific.h"
#include "CommonDefs.h"
#include "TagValue.h"
#include "Contract.h"
#include "WshEventData.h"
#include "OrderCancel.h"
#include "EDecoder.h"
#include "ExecutionRequest.pb.h"
#include "PlaceOrderRequest.pb.h"
#include "CancelOrderRequest.pb.h"
#include "GlobalCancelRequest.pb.h"
#include "AllOpenOrdersRequest.pb.h"
#include "AutoOpenOrdersRequest.pb.h"
#include "OpenOrdersRequest.pb.h"
#include "CompletedOrdersRequest.pb.h"
#include "ContractDataRequest.pb.h"
#include "MarketDataRequest.pb.h"
#include "CancelMarketData.pb.h"
#include "MarketDepthRequest.pb.h"
#include "CancelMarketDepth.pb.h"
#include "MarketDataTypeRequest.pb.h"
#include "AccountDataRequest.pb.h"
#include "ManagedAccountsRequest.pb.h"
#include "PositionsRequest.pb.h"
#include "AccountSummaryRequest.pb.h"
#include "CancelAccountSummary.pb.h"
#include "CancelPositions.pb.h"
#include "PositionsMultiRequest.pb.h"
#include "CancelPositionsMulti.pb.h"
#include "AccountUpdatesMultiRequest.pb.h"
#include "CancelAccountUpdatesMulti.pb.h"
#include "HistoricalDataRequest.pb.h"
#include "CancelHistoricalData.pb.h"
#include "RealTimeBarsRequest.pb.h"
#include "CancelRealTimeBars.pb.h"
#include "HeadTimestampRequest.pb.h"
#include "CancelHeadTimestamp.pb.h"
#include "HistogramDataRequest.pb.h"
#include "CancelHistogramData.pb.h"
#include "HistoricalTicksRequest.pb.h"
#include "TickByTickRequest.pb.h"
#include "CancelTickByTick.pb.h"
#include "NewsBulletinsRequest.pb.h"
#include "CancelNewsBulletins.pb.h"
#include "NewsArticleRequest.pb.h"
#include "NewsProvidersRequest.pb.h"
#include "HistoricalNewsRequest.pb.h"
#include "WshMetaDataRequest.pb.h"
#include "CancelWshMetaData.pb.h"
#include "WshEventDataRequest.pb.h"
#include "CancelWshEventData.pb.h"
#include "ScannerParametersRequest.pb.h"
#include "ScannerSubscriptionRequest.pb.h"
#include "CancelScannerSubscription.pb.h"
#include "PnLRequest.pb.h"
#include "CancelPnL.pb.h"
#include "PnLSingleRequest.pb.h"
#include "CancelPnLSingle.pb.h"
#include "FARequest.pb.h"
#include "FAReplace.pb.h"
#include "ExerciseOptionsRequest.pb.h"
#include "CalculateImpliedVolatilityRequest.pb.h"
#include "CancelCalculateImpliedVolatility.pb.h"
#include "CalculateOptionPriceRequest.pb.h"
#include "CancelCalculateOptionPrice.pb.h"
#include "SecDefOptParamsRequest.pb.h"
#include "SoftDollarTiersRequest.pb.h"
#include "FamilyCodesRequest.pb.h"
#include "MatchingSymbolsRequest.pb.h"
#include "SmartComponentsRequest.pb.h"
#include "MarketRuleRequest.pb.h"
#include "UserInfoRequest.pb.h"
#include "IdsRequest.pb.h"
#include "CurrentTimeRequest.pb.h"
#include "CurrentTimeInMillisRequest.pb.h"
#include "StartApiRequest.pb.h"
#include "SetServerLogLevelRequest.pb.h"
#include "VerifyRequest.pb.h"
#include "VerifyMessageRequest.pb.h"
#include "QueryDisplayGroupsRequest.pb.h"
#include "SubscribeToGroupEventsRequest.pb.h"
#include "UpdateDisplayGroupRequest.pb.h"
#include "UnsubscribeFromGroupEventsRequest.pb.h"
#include "MarketDepthExchangesRequest.pb.h"
#include "CancelContractData.pb.h"
#include "CancelHistoricalTicks.pb.h"
#include "ConfigRequest.pb.h"
#include "UpdateConfigRequest.pb.h"


namespace ibapi {
namespace client_constants {

/////////////////////////////////////////////////////////////////////////////////
// SOCKET CLIENT VERSION CHANGE LOG : Incremented when the format of incoming
//                                    server responses change
/////////////////////////////////////////////////////////////////////////////////
// constants
// 6 = Added parentId to orderStatus
// 7 = The new execDetails event returned for an order filled status and reqExecDetails
//     Also added market depth support.
// 8 = Added 'lastFillPrice' to orderStatus and 'permId' to execDetails
// 9 = Added 'avgCost', 'unrealizedPNL', and 'unrealizedPNL' to updatePortfolio event
// 10 = Added 'serverId' to the 'open order' & 'order status' events.
//      We send back all the API open orders upon connection.
//      Added new methods reqAllOpenOrders, reqAutoOpenOrders()
//      Added FA support - reqExecution has filter.
//                       - reqAccountUpdates takes acct code.
// 11 = Added permId to openOrder event.
// 12 = Added IgnoreRth, hidden, and discretionaryAmt
// 13 = Added GoodAfterTime
// 14 = always send size on bid/ask/last tick
// 15 = send allocation string with open order
// 16 = can receive account name in account and portfolio updates, and fa params in openOrder
// 17 = can receive liquidation field in exec reports, and notAutoAvailable field in mkt data
// 18 = can receive good till date field in open order messages, and send backfill requests
// 19 = can receive new extended order attributes in OPEN_ORDER
// 20 = expects TWS time string on connection after server version >= 20, and parentId in open order
// 21 = can receive bond contract details.
// 22 = can receive price magnifier in contract details
// 23 = support for scanner
// 24 = can receive volatility order parameters in open order messages
// 25 = can receive HMDS query start and end times
// 26 = can receive option vols in option market data messages
// 27 = can receive delta neutral order type and delta neutral aux price
// 28 = can receive option model computation ticks
// 29 = can receive trail stop limit price in open order and can place them: API 8.91
// 30 = can receive extended bond contract def, new ticks, and trade count in bars
// 31 = can receive EFP extensions to scanner and market data, and combo legs on open orders
//    ; can receive RT bars
// 32 = can receive TickType.LAST_TIMESTAMP
// 33 = can receive ScaleNumComponents and ScaleComponentSize is open order messages
// 34 = can receive whatIf orders / order state
// 35 = can receive contId field for Contract objects
// 36 = can receive outsideRth field for Order objects
// 37 = can receive clearingAccount and clearingIntent for Order objects
// 38 = can receive multiplier and primaryExchange in portfolio updates
//    ; can receive cumQty and avgPrice in execution
//    ; can receive deltaNeutralContract for Contract objects
//    ; can receive reqId and end marker in contractDetails/bondContractDetails
//    ; can receive ScaleInitComponentSize and ScaleSubsComponentSize for Order objects
// 39 = can receive underConId in contractDetails
// 40 = can receive algoStrategy/algoParams in openOrder
// 41 = can receive end marker for openOrder
//    ; can receive end marker for account download
//    ; can receive end marker for executions download
// 42 = can receive deltaNeutralValidation
// 43 = can receive longName(companyName)
//    ; can receive listingExchange
//    ; can receive RTVolume tick
// 44 = can receive end market for ticker snapshot
// 45 = can receive notHeld field in openOrder
// 46 = can receive contractMonth, industry, category, subcategory fields in contractDetails
//    ; can receive timeZoneId, tradingHours, liquidHours fields in contractDetails
// 47 = can receive gamma, vega, theta, undPrice fields in TICK_OPTION_COMPUTATION
// 48 = can receive exemptCode in openOrder
// 49 = can receive hedgeType and hedgeParam in openOrder
// 50 = can receive optOutSmartRouting field in openOrder
// 51 = can receive smartComboRoutingParams in openOrder
// 52 = can receive deltaNeutralConId, deltaNeutralSettlingFirm, deltaNeutralClearingAccount and deltaNeutralClearingIntent in openOrder
// 53 = can receive orderRef in execution
// 54 = can receive scale order fields (PriceAdjustValue, PriceAdjustInterval, ProfitOffset, AutoReset,
//      InitPosition, InitFillQty and RandomPercent) in openOrder
// 55 = can receive orderComboLegs (price) in openOrder
// 56 = can receive trailingPercent in openOrder
// 57 = can receive commissionAndFeesReport message
// 58 = can receive CUSIP/ISIN/etc. in contractDescription/bondContractDescription
// 59 = can receive evRule, evMultiplier in contractDescription/bondContractDescription/executionDetails
//      can receive multiplier in executionDetails
// 60 = can receive deltaNeutralOpenClose, deltaNeutralShortSale, deltaNeutralShortSaleSlot
//      and deltaNeutralDesignatedLocation in openOrder
//      can receive position, positionEnd, accountSummary and accountSummaryEnd
// 61 = can receive multiplier in openOrder
//      can receive tradingClass in openOrder, updatePortfolio, execDetails and position
// 62 = can receive avgCost in position message
// 63 = can receive verifyMessageAPI, verifyCompleted, displayGroupList and displayGroupUpdated messages
// 64 = can receive solicited attrib in openOrder message
// 65 = can receive verifyAndAuthMessageAPI and verifyAndAuthCompleted messages
// 66 = can receive randomize size and randomize price order fields

const int CLIENT_VERSION    = 66;


enum OutgoingMsgId : int {
    REQ_MKT_DATA                  = 1,
    CANCEL_MKT_DATA               = 2,
    PLACE_ORDER                   = 3,
    CANCEL_ORDER                  = 4,
    REQ_OPEN_ORDERS               = 5,
    REQ_ACCT_DATA                 = 6,
    REQ_EXECUTIONS                = 7,
    REQ_IDS                       = 8,
    REQ_CONTRACT_DATA             = 9,
    REQ_MKT_DEPTH                 = 10,
    CANCEL_MKT_DEPTH              = 11,
    REQ_NEWS_BULLETINS            = 12,
    CANCEL_NEWS_BULLETINS         = 13,
    SET_SERVER_LOGLEVEL           = 14,
    REQ_AUTO_OPEN_ORDERS          = 15,
    REQ_ALL_OPEN_ORDERS           = 16,
    REQ_MANAGED_ACCTS             = 17,
    REQ_FA                        = 18,
    REPLACE_FA                    = 19,
    REQ_HISTORICAL_DATA           = 20,
    EXERCISE_OPTIONS              = 21,
    REQ_SCANNER_SUBSCRIPTION      = 22,
    CANCEL_SCANNER_SUBSCRIPTION   = 23,
    REQ_SCANNER_PARAMETERS        = 24,
    CANCEL_HISTORICAL_DATA        = 25,
    REQ_CURRENT_TIME              = 49,
    REQ_REAL_TIME_BARS            = 50,
    CANCEL_REAL_TIME_BARS         = 51,
    REQ_CALC_IMPLIED_VOLAT        = 54,
    REQ_CALC_OPTION_PRICE         = 55,
    CANCEL_CALC_IMPLIED_VOLAT     = 56,
    CANCEL_CALC_OPTION_PRICE      = 57,
    REQ_GLOBAL_CANCEL             = 58,
    REQ_MARKET_DATA_TYPE          = 59,
    REQ_POSITIONS                 = 61,
    REQ_ACCOUNT_SUMMARY           = 62,
    CANCEL_ACCOUNT_SUMMARY        = 63,
    CANCEL_POSITIONS              = 64,
    VERIFY_REQUEST                = 65,
    VERIFY_MESSAGE                = 66,
    QUERY_DISPLAY_GROUPS          = 67,
    SUBSCRIBE_TO_GROUP_EVENTS     = 68,
    UPDATE_DISPLAY_GROUP          = 69,
    UNSUBSCRIBE_FROM_GROUP_EVENTS = 70,
    START_API                     = 71,
    VERIFY_AND_AUTH_REQUEST       = 72,
    VERIFY_AND_AUTH_MESSAGE       = 73,
    REQ_POSITIONS_MULTI           = 74,
    CANCEL_POSITIONS_MULTI        = 75,
    REQ_ACCOUNT_UPDATES_MULTI     = 76,
    CANCEL_ACCOUNT_UPDATES_MULTI  = 77,
    REQ_SEC_DEF_OPT_PARAMS        = 78,
    REQ_SOFT_DOLLAR_TIERS         = 79,
    REQ_FAMILY_CODES              = 80,
    REQ_MATCHING_SYMBOLS          = 81,
    REQ_MKT_DEPTH_EXCHANGES       = 82,
    REQ_SMART_COMPONENTS          = 83,
    REQ_NEWS_ARTICLE              = 84,
    REQ_NEWS_PROVIDERS            = 85,
    REQ_HISTORICAL_NEWS           = 86,
    REQ_HEAD_TIMESTAMP            = 87,
    REQ_HISTOGRAM_DATA            = 88,
    CANCEL_HISTOGRAM_DATA         = 89,
    CANCEL_HEAD_TIMESTAMP         = 90,
    REQ_MARKET_RULE               = 91,
    REQ_PNL                       = 92,
    CANCEL_PNL                    = 93,
    REQ_PNL_SINGLE                = 94,
    CANCEL_PNL_SINGLE             = 95,
    REQ_HISTORICAL_TICKS          = 96,
    REQ_TICK_BY_TICK_DATA         = 97,
    CANCEL_TICK_BY_TICK_DATA      = 98,
    REQ_COMPLETED_ORDERS          = 99,
    REQ_WSH_META_DATA             = 100,
    CANCEL_WSH_META_DATA          = 101,
    REQ_WSH_EVENT_DATA            = 102,
    CANCEL_WSH_EVENT_DATA         = 103,
    REQ_USER_INFO                 = 104,
    REQ_CURRENT_TIME_IN_MILLIS    = 105,
    CANCEL_CONTRACT_DATA          = 106,
    CANCEL_HISTORICAL_TICKS       = 107,
    REQ_CONFIG                    = 108,
    UPDATE_CONFIG                 = 109,
};
// TWS New Bulletins constants
const int NEWS_MSG              = 1;    // standard IB news bulleting message
const int EXCHANGE_AVAIL_MSG    = 2;    // control message specifying that an exchange is available for trading
const int EXCHANGE_UNAVAIL_MSG  = 3;    // control message specifying that an exchange is unavailable for trading

const int PROTOBUF_MSG_ID = 200;
inline int GetServerVersionForMessage(int msgId)
{
	switch (msgId)
	{
	case REQ_EXECUTIONS: return MIN_SERVER_VER_PROTOBUF;
	case PLACE_ORDER: return MIN_SERVER_VER_PROTOBUF_PLACE_ORDER;
	case CANCEL_ORDER: return MIN_SERVER_VER_PROTOBUF_PLACE_ORDER;
	case REQ_GLOBAL_CANCEL: return MIN_SERVER_VER_PROTOBUF_PLACE_ORDER;
	case REQ_ALL_OPEN_ORDERS: return MIN_SERVER_VER_PROTOBUF_COMPLETED_ORDER;
	case REQ_AUTO_OPEN_ORDERS: return MIN_SERVER_VER_PROTOBUF_COMPLETED_ORDER;
	case REQ_OPEN_ORDERS: return MIN_SERVER_VER_PROTOBUF_COMPLETED_ORDER;
	case REQ_COMPLETED_ORDERS: return MIN_SERVER_VER_PROTOBUF_COMPLETED_ORDER;
	case REQ_CONTRACT_DATA: return MIN_SERVER_VER_PROTOBUF_CONTRACT_DATA;
	case REQ_MKT_DATA: return MIN_SERVER_VER_PROTOBUF_MARKET_DATA;
	case CANCEL_MKT_DATA: return MIN_SERVER_VER_PROTOBUF_MARKET_DATA;
	case REQ_MKT_DEPTH: return MIN_SERVER_VER_PROTOBUF_MARKET_DATA;
	case CANCEL_MKT_DEPTH: return MIN_SERVER_VER_PROTOBUF_MARKET_DATA;
	case REQ_MARKET_DATA_TYPE: return MIN_SERVER_VER_PROTOBUF_MARKET_DATA;
	case REQ_ACCT_DATA: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case REQ_MANAGED_ACCTS: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case REQ_POSITIONS: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case CANCEL_POSITIONS: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case REQ_ACCOUNT_SUMMARY: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case CANCEL_ACCOUNT_SUMMARY: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case REQ_POSITIONS_MULTI: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case CANCEL_POSITIONS_MULTI: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case REQ_ACCOUNT_UPDATES_MULTI: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case CANCEL_ACCOUNT_UPDATES_MULTI: return MIN_SERVER_VER_PROTOBUF_ACCOUNTS_POSITIONS;
	case REQ_HISTORICAL_DATA: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case CANCEL_HISTORICAL_DATA: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case REQ_REAL_TIME_BARS: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case CANCEL_REAL_TIME_BARS: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case REQ_HEAD_TIMESTAMP: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case CANCEL_HEAD_TIMESTAMP: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case REQ_HISTOGRAM_DATA: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case CANCEL_HISTOGRAM_DATA: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case REQ_HISTORICAL_TICKS: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case REQ_TICK_BY_TICK_DATA: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case CANCEL_TICK_BY_TICK_DATA: return MIN_SERVER_VER_PROTOBUF_HISTORICAL_DATA;
	case REQ_NEWS_BULLETINS: return MIN_SERVER_VER_PROTOBUF_NEWS_DATA;
	case CANCEL_NEWS_BULLETINS: return MIN_SERVER_VER_PROTOBUF_NEWS_DATA;
	case REQ_NEWS_ARTICLE: return MIN_SERVER_VER_PROTOBUF_NEWS_DATA;
	case REQ_NEWS_PROVIDERS: return MIN_SERVER_VER_PROTOBUF_NEWS_DATA;
	case REQ_HISTORICAL_NEWS: return MIN_SERVER_VER_PROTOBUF_NEWS_DATA;
	case REQ_WSH_META_DATA: return MIN_SERVER_VER_PROTOBUF_NEWS_DATA;
	case CANCEL_WSH_META_DATA: return MIN_SERVER_VER_PROTOBUF_NEWS_DATA;
	case REQ_WSH_EVENT_DATA: return MIN_SERVER_VER_PROTOBUF_NEWS_DATA;
	case CANCEL_WSH_EVENT_DATA: return MIN_SERVER_VER_PROTOBUF_NEWS_DATA;
	case REQ_SCANNER_PARAMETERS: return MIN_SERVER_VER_PROTOBUF_SCAN_DATA;
	case REQ_SCANNER_SUBSCRIPTION: return MIN_SERVER_VER_PROTOBUF_SCAN_DATA;
	case CANCEL_SCANNER_SUBSCRIPTION: return MIN_SERVER_VER_PROTOBUF_SCAN_DATA;
	case REQ_PNL: return MIN_SERVER_VER_PROTOBUF_SCAN_DATA;
	case CANCEL_PNL: return MIN_SERVER_VER_PROTOBUF_SCAN_DATA;
	case REQ_PNL_SINGLE: return MIN_SERVER_VER_PROTOBUF_SCAN_DATA;
	case CANCEL_PNL_SINGLE: return MIN_SERVER_VER_PROTOBUF_SCAN_DATA;
	case REQ_FA: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_1;
	case REPLACE_FA: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_1;
	case EXERCISE_OPTIONS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_1;
	case REQ_CALC_IMPLIED_VOLAT: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_1;
	case CANCEL_CALC_IMPLIED_VOLAT: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_1;
	case REQ_CALC_OPTION_PRICE: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_1;
	case CANCEL_CALC_OPTION_PRICE: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_1;
	case REQ_SEC_DEF_OPT_PARAMS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_2;
	case REQ_SOFT_DOLLAR_TIERS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_2;
	case REQ_FAMILY_CODES: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_2;
	case REQ_MATCHING_SYMBOLS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_2;
	case REQ_SMART_COMPONENTS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_2;
	case REQ_MARKET_RULE: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_2;
	case REQ_USER_INFO: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_2;
	case REQ_IDS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case REQ_CURRENT_TIME: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case REQ_CURRENT_TIME_IN_MILLIS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case START_API: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case SET_SERVER_LOGLEVEL: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case VERIFY_REQUEST: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case VERIFY_MESSAGE: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case QUERY_DISPLAY_GROUPS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case SUBSCRIBE_TO_GROUP_EVENTS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case UPDATE_DISPLAY_GROUP: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case UNSUBSCRIBE_FROM_GROUP_EVENTS: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	case REQ_MKT_DEPTH_EXCHANGES: return MIN_SERVER_VER_PROTOBUF_REST_MESSAGES_3;
	}
	return INT_MAX; // unknown id, not supported
};

} // namespace client_constants
} // namespace ibapi

struct Contract;
struct Order;
struct ExecutionFilter;
struct ScannerSubscription;
struct ETransport;

class EWrapper;


class TWSAPIDLLEXP EClient
{
    friend class ContractCondition;
    friend class ExecutionCondition;
    friend class OperatorCondition;
    friend class OrderCondition;
    friend class PriceCondition;

public:

	explicit EClient(EWrapper *ptr, ETransport *pTransport);
	virtual ~EClient();

	virtual bool eConnect(const char* host, int port, int clientId = 0, bool extraAuth = false) = 0;
	virtual void eDisconnect(bool resetState) = 0;

	int clientId() const { return m_clientId; }

	const std::string& optionalCapabilities() const;
	void setOptionalCapabilities(const std::string& optCapts);

	void setConnectOptions(const std::string& connectOptions);
	void disableUseV100Plus();
	bool usingV100Plus();
	bool useProtoBuf(int msgId);

protected:

	void eConnectBase();
	void eDisconnectBase();

public:

	enum ConnState {
		CS_DISCONNECTED,
		CS_CONNECTING,
		CS_CONNECTED
	};

	// connection state
	ConnState connState() const;
	bool isConnected() const;

	const std::string& host() const { return m_host; }
	int port() const { return m_port; }
	void validateInvalidSymbols(const std::string& host);

public:

	// access to protected variables
	EWrapper * getWrapper() const;
protected:
	void setClientId(int clientId);
	void setExtraAuth(bool extraAuth);
	void setHost(const std::string& host);
	void setPort(int port);

public:

	bool isInBufferEmpty() const;

	// override virtual funcs from EClient
	int serverVersion();
	std::string TwsConnectionTime();
	void reqMktData(int reqId, const Contract& contract,
		const std::string& genericTicks, bool snapshot, bool regulatorySnaphsot, const TagValueListSPtr& mktDataOptions);
	void cancelMktData(int reqId);
	void placeOrder(int id, const Contract& contract, const Order& order);
	void cancelOrder(int id, const OrderCancel& orderCancel);
	void reqOpenOrders();
	void reqAccountUpdates(bool subscribe, const std::string& acctCode);
	void reqExecutions(int reqId, const ExecutionFilter& filter);
	void reqIds(int numIds);
	void reqContractDetails(int reqId, const Contract& contract);
	void reqMktDepth(int reqId, const Contract& contract, int numRows, bool isSmartDepth, const TagValueListSPtr& mktDepthOptions);
	void cancelMktDepth(int reqId, bool isSmartDepth);
	void reqNewsBulletins(bool allMsgs);
	void cancelNewsBulletins();
	void setServerLogLevel(int level);
	void reqAutoOpenOrders(bool bAutoBind);
	void reqAllOpenOrders();
	void reqManagedAccts();
	void requestFA(faDataType pFaDataType);
	void replaceFA(int reqId, faDataType pFaDataType, const std::string& cxml);
	void reqHistoricalData(int reqId, const Contract& contract,
		const std::string& endDateTime, const std::string& durationStr,
		const std::string&  barSizeSetting, const std::string& whatToShow,
		int useRTH, int formatDate, bool keepUpToDate, const TagValueListSPtr& chartOptions);
	void exerciseOptions(int reqId, const Contract& contract,
		int exerciseAction, int exerciseQuantity,
		const std::string& account, int override, const std::string& manualOrderTime, const std::string& customerAccount, bool professionalCustomer);
	void cancelHistoricalData(int reqId);
	void reqRealTimeBars(int reqId, const Contract& contract, int barSize,
		const std::string& whatToShow, bool useRTH, const TagValueListSPtr& realTimeBarsOptions);
	void cancelRealTimeBars(int reqId);
	void cancelScannerSubscription(int reqId);
	void reqScannerParameters();
	void reqScannerSubscription(int reqId, const ScannerSubscription& subscription, const TagValueListSPtr& scannerSubscriptionOptions, const TagValueListSPtr& scannerSubscriptionFilterOptions);
	void reqCurrentTime();
	void calculateImpliedVolatility(int reqId, const Contract& contract, double optionPrice, double underPrice,
        //reserved for future use, must be blank
        const TagValueListSPtr& miscOptions);
	void calculateOptionPrice(int reqId, const Contract& contract, double volatility, double underPrice,
        //reserved for future use, must be blank
        const TagValueListSPtr& miscOptions);
	void cancelCalculateImpliedVolatility(int reqId);
	void cancelCalculateOptionPrice(int reqId);
	void reqGlobalCancel(const OrderCancel& orderCancel);
	void reqMarketDataType(int marketDataType);
	void reqPositions();
	void cancelPositions();
	void reqAccountSummary(int reqId, const std::string& groupName, const std::string& tags);
	void cancelAccountSummary(int reqId);
	void verifyRequest(const std::string& apiName, const std::string& apiVersion);
	void verifyMessage(const std::string& apiData);
	void verifyAndAuthRequest(const std::string& apiName, const std::string& apiVersion, const std::string& opaqueIsvKey);
	void verifyAndAuthMessage(const std::string& apiData, const std::string& xyzResponse);
	void queryDisplayGroups(int reqId);
	void subscribeToGroupEvents(int reqId, int groupId);
	void updateDisplayGroup(int reqId, const std::string& contractInfo);
	void unsubscribeFromGroupEvents(int reqId);
	void reqPositionsMulti(int reqId, const std::string& account, const std::string& modelCode);
	void cancelPositionsMulti(int reqId);
	void reqAccountUpdatesMulti(int reqId, const std::string& account, const std::string& modelCode, bool ledgerAndNLV);
	void cancelAccountUpdatesMulti(int reqId);
	void reqSecDefOptParams(int reqId, const std::string& underlyingSymbol, const std::string& futFopExchange, const std::string& underlyingSecType, int underlyingConId);
	void reqSoftDollarTiers(int reqId);
	void reqFamilyCodes();
	void reqMatchingSymbols(int reqId, const std::string& pattern);
	void reqMktDepthExchanges();
	void reqSmartComponents(int reqId, std::string bboExchange);
	void reqNewsProviders();
	void reqNewsArticle(int requestId, const std::string& providerCode, const std::string& articleId, const TagValueListSPtr& newsArticleOptions);
	void reqHistoricalNews(int requestId, int conId, const std::string& providerCodes, const std::string& startDateTime, const std::string& endDateTime, int totalResults,
		const TagValueListSPtr& historicalNewsOptions);
	void reqHeadTimestamp(int reqId, const Contract &contract, const std::string& whatToShow, int useRTH, int formatDate);
	void cancelHeadTimestamp(int reqId);
	void reqHistogramData(int reqId, const Contract &contract, bool useRTH, const std::string& timePeriod);
	void cancelHistogramData(int reqId);
	void reqMarketRule(int marketRuleId);

	void reqPnL(int reqId, const std::string& account, const std::string& modelCode);
	void cancelPnL(int reqId);
	void reqPnLSingle(int reqId, const std::string& account, const std::string& modelCode, int conId);
	void cancelPnLSingle(int reqId);
    void reqHistoricalTicks(int reqId, const Contract &contract, const std::string& startDateTime,
            const std::string& endDateTime, int numberOfTicks, const std::string& whatToShow, int useRth, bool ignoreSize, const TagValueListSPtr& miscOptions);
    void reqTickByTickData(int reqId, const Contract &contract, const std::string& tickType, int numberOfTicks, bool ignoreSize);
    void cancelTickByTickData(int reqId);
    void reqCompletedOrders(bool apiOnly);
	void reqWshMetaData(int reqId);
	void reqWshEventData(int reqId, const WshEventData &wshEventData);
	void cancelWshMetaData(int reqId);
	void cancelWshEventData(int reqid);
    void reqUserInfo(int reqId);
	void reqCurrentTimeInMillis();
	void cancelContractData(int reqId);
	void cancelHistoricalTicks(int reqId);

public:
	void reqExecutionsProtoBuf(const protobuf::ExecutionRequest& executionRequestProto);
	void placeOrderProtoBuf(const protobuf::PlaceOrderRequest& placeOrderRequestProto);
	void cancelOrderProtoBuf(const protobuf::CancelOrderRequest& cancelOrderRequestProto);
	void reqGlobalCancelProtoBuf(const protobuf::GlobalCancelRequest & globalCancelRequestProto);
	void reqAllOpenOrdersProtoBuf(const protobuf::AllOpenOrdersRequest& allOpenOrdersRequestProto);
	void reqAutoOpenOrdersProtoBuf(const protobuf::AutoOpenOrdersRequest& autoOpenOrdersRequestProto);
	void reqOpenOrdersProtoBuf(const protobuf::OpenOrdersRequest& openOrdersRequestProto);
	void reqCompletedOrdersProtoBuf(const protobuf::CompletedOrdersRequest& completedOrdersRequestProto);
	void reqContractDataProtoBuf(const protobuf::ContractDataRequest& contractDataRequestProto);
	void reqMarketDataProtoBuf(const protobuf::MarketDataRequest& marketDataRequestProto);
	void cancelMarketDataProtoBuf(const protobuf::CancelMarketData& cancelMarketDataProto);
	void reqMarketDepthProtoBuf(const protobuf::MarketDepthRequest& marketDepthRequestProto);
	void cancelMarketDepthProtoBuf(const protobuf::CancelMarketDepth& cancelMarketDepthProto);
	void reqMarketDataTypeProtoBuf(const protobuf::MarketDataTypeRequest& marketDataTypeRequestProto);
    void reqAccountUpdatesProtoBuf(const protobuf::AccountDataRequest &accountDataRequestProto);
    void reqManagedAcctsProtoBuf(const protobuf::ManagedAccountsRequest &managedAccountsRequestProto);
    void reqPositionsProtoBuf(const protobuf::PositionsRequest &positionsRequestProto);
    void reqAccountSummaryProtoBuf(const protobuf::AccountSummaryRequest &accountSummaryRequestProto);
    void cancelAccountSummaryProtoBuf(const protobuf::CancelAccountSummary &cancelAccountSummaryProto);
    void cancelPositionsProtoBuf(const protobuf::CancelPositions &cancelPositionsProto);
    void reqPositionsMultiProtoBuf(const protobuf::PositionsMultiRequest &positionsMultiRequestProto);
    void cancelPositionsMultiProtoBuf(const protobuf::CancelPositionsMulti &cancelPositionsMultiProto);
    void reqAccountUpdatesMultiProtoBuf(const protobuf::AccountUpdatesMultiRequest &accountUpdatesMultiRequestProto);
    void cancelAccountUpdatesMultiProtoBuf(const protobuf::CancelAccountUpdatesMulti &cancelAccountUpdatesMultiProto);
	void reqHistoricalDataProtoBuf(const protobuf::HistoricalDataRequest& historicalDataRequestProto);
	void cancelHistoricalDataProtoBuf(const protobuf::CancelHistoricalData& cancelHistoricalDataProto);
	void reqRealTimeBarsProtoBuf(const protobuf::RealTimeBarsRequest& realTimeBarsRequestProto);
	void cancelRealTimeBarsProtoBuf(const protobuf::CancelRealTimeBars& cancelRealTimeBarsProto);
	void reqHeadTimestampProtoBuf(const protobuf::HeadTimestampRequest& headTimestampRequestProto);
	void cancelHeadTimestampProtoBuf(const protobuf::CancelHeadTimestamp& cancelHeadTimestampProto);
	void reqHistogramDataProtoBuf(const protobuf::HistogramDataRequest& histogramDataRequestProto);
	void cancelHistogramDataProtoBuf(const protobuf::CancelHistogramData& cancelHistogramDataProto);
	void reqHistoricalTicksProtoBuf(const protobuf::HistoricalTicksRequest& historicalTicksRequestProto);
	void reqTickByTickDataProtoBuf(const protobuf::TickByTickRequest& tickByTickRequestProto);
	void cancelTickByTickDataProtoBuf(const protobuf::CancelTickByTick& cancelTickByTickProto);
	void reqNewsBulletinsProtoBuf(const protobuf::NewsBulletinsRequest& newsBulletinsRequestProto);
	void cancelNewsBulletinsProtoBuf(const protobuf::CancelNewsBulletins& cancelNewsBulletinsProto);
	void reqNewsArticleProtoBuf(const protobuf::NewsArticleRequest& newsArticleRequestProto);
	void reqNewsProvidersProtoBuf(const protobuf::NewsProvidersRequest& newsProvidersRequestProto);
	void reqHistoricalNewsProtoBuf(const protobuf::HistoricalNewsRequest& historicalNewsRequestProto);
	void reqWshMetaDataProtoBuf(const protobuf::WshMetaDataRequest& wshMetaDataRequestProto);
	void cancelWshMetaDataProtoBuf(const protobuf::CancelWshMetaData& cancelWshMetaDataProto);
	void reqWshEventDataProtoBuf(const protobuf::WshEventDataRequest& wshEventDataRequestProto);
	void cancelWshEventDataProtoBuf(const protobuf::CancelWshEventData& cancelWshEventDataProto);
	void reqScannerParametersProtoBuf(const protobuf::ScannerParametersRequest& scannerParametersRequestProto);
	void reqScannerSubscriptionProtoBuf(const protobuf::ScannerSubscriptionRequest& scannerSubscriptionRequestProto);
	void cancelScannerSubscriptionProtoBuf(const protobuf::CancelScannerSubscription& cancelScannerSubscriptionProto);
	void reqPnLProtoBuf(const protobuf::PnLRequest& pnlRequestProto);
	void cancelPnLProtoBuf(const protobuf::CancelPnL& cancelPnLProto);
	void reqPnLSingleProtoBuf(const protobuf::PnLSingleRequest& pnlSingleRequestProto);
	void cancelPnLSingleProtoBuf(const protobuf::CancelPnLSingle& cancelPnLSingleProto);
	void reqFAProtoBuf(const protobuf::FARequest& faRequestProto);
	void replaceFAProtoBuf(const protobuf::FAReplace& faReplaceProto);
	void exerciseOptionsProtoBuf(const protobuf::ExerciseOptionsRequest& exerciseOptionsRequestProto);
	void calculateImpliedVolatilityProtoBuf(const protobuf::CalculateImpliedVolatilityRequest& calculateImpliedVolatilityRequestProto);
	void cancelCalculateImpliedVolatilityProtoBuf(const protobuf::CancelCalculateImpliedVolatility& cancelCalculateImpliedVolatilityProto);
	void calculateOptionPriceProtoBuf(const protobuf::CalculateOptionPriceRequest& calculateOptionPriceRequestProto);
	void cancelCalculateOptionPriceProtoBuf(const protobuf::CancelCalculateOptionPrice& cancelCalculateOptionPriceProto);
	void reqSecDefOptParamsProtoBuf(const protobuf::SecDefOptParamsRequest& secDefOptParamsRequestProto);
	void reqSoftDollarTiersProtoBuf(const protobuf::SoftDollarTiersRequest& softDollarTiersRequestProto);
	void reqFamilyCodesProtoBuf(const protobuf::FamilyCodesRequest& familyCodesRequestProto);
	void reqMatchingSymbolsProtoBuf(const protobuf::MatchingSymbolsRequest& matchingSymbolsRequestProto);
	void reqSmartComponentsProtoBuf(const protobuf::SmartComponentsRequest& smartComponentsRequestProto);
	void reqMarketRuleProtoBuf(const protobuf::MarketRuleRequest& marketRuleRequestProto);
	void reqUserInfoProtoBuf(const protobuf::UserInfoRequest& userInfoRequestProto);
	void reqIdsProtoBuf(const protobuf::IdsRequest& idsRequestProto);
	void reqCurrentTimeProtoBuf(const protobuf::CurrentTimeRequest& currentTimeRequestProto);
	void reqCurrentTimeInMillisProtoBuf(const protobuf::CurrentTimeInMillisRequest& currentTimeInMillisRequestProto);
	void startApiProtoBuf(const protobuf::StartApiRequest& startApiRequestProto);
	void setServerLogLevelProtoBuf(const protobuf::SetServerLogLevelRequest& setServerLogLevelRequestProto);
	void verifyRequestProtoBuf(const protobuf::VerifyRequest& verifyRequestProto);
	void verifyMessageProtoBuf(const protobuf::VerifyMessageRequest& verifyMessageRequestProto);
	void queryDisplayGroupsProtoBuf(const protobuf::QueryDisplayGroupsRequest& queryDisplayGroupsRequestProto);
	void subscribeToGroupEventsProtoBuf(const protobuf::SubscribeToGroupEventsRequest& subscribeToGroupEventsRequestProto);
	void updateDisplayGroupProtoBuf(const protobuf::UpdateDisplayGroupRequest& updateDisplayGroupRequestProto);
	void unsubscribeFromGroupEventsProtoBuf(const protobuf::UnsubscribeFromGroupEventsRequest& unsubscribeFromGroupEventsRequestProto);
	void reqMarketDepthExchangesProtoBuf(const protobuf::MarketDepthExchangesRequest& marketDepthExchangesRequestProto);
	void cancelContractDataProtoBuf(const protobuf::CancelContractData& cancelContractDataProto);
	void cancelHistoricalTicksProtoBuf(const protobuf::CancelHistoricalTicks& cancelHistoricalTicksProto);
	void reqConfigProtoBuf(const protobuf::ConfigRequest& configRequestProto);
	void updateConfigProtoBuf(const protobuf::UpdateConfigRequest& updateConfigRequestProto);

private:

	virtual int receive(char* buf, size_t sz) = 0;
	static bool isAsciiPrintable(const std::string& s);
	std::string validateOrderParameters(const protobuf::Order& order);
	std::string validateAttachedOrdersParameters(const protobuf::AttachedOrders& attachedOrders);

protected:

	virtual void prepareBufferImpl(std::ostream&) const = 0;
	virtual void prepareBuffer(std::ostream&) const = 0;
	virtual bool closeAndSend(std::string msg, unsigned offset = 0) = 0;
	virtual int bufferedSend(const std::string& msg);


   	// encoders
	template<class T> static void EncodeField(std::ostream&, T);

public:
	void startApi();



    void EncodeContract(std::ostream& os, const Contract &contract);
    void EncodeTagValueList(std::ostream& os, const TagValueListSPtr &tagValueList);
	void EncodeMsgId(std::ostream& os, int msgId);

	// "max" encoders
	static void EncodeFieldMax(std::ostream& os, int);
	static void EncodeFieldMax(std::ostream& os, double);

	// "raw" encoders
	static void EncodeRawInt(std::ostream& buf, int intValue);

	// socket state
private:
	virtual bool isSocketOK() const = 0;

protected:

	bool isConnecting() const;
	int sendConnectRequest();
    bool extraAuth();

protected:

	EWrapper *m_pEWrapper;
	std::unique_ptr<ETransport> m_transport;

private:
	std::string m_host;
	int m_port;

	int m_clientId;

	ConnState m_connState;
	bool m_extraAuth;

protected:
	int m_serverVersion;
	std::string m_TwsTime;

private:
	std::string m_optionalCapabilities;

	std::string m_connectOptions;

protected:
	bool m_useV100Plus;

};

template<> void EClient::EncodeField<bool>(std::ostream& os, bool);
template<> void EClient::EncodeField<double>(std::ostream& os, double);
template<> void EClient::EncodeField<Decimal>(std::ostream& os, Decimal);
template<> void EClient::EncodeField<std::string> (std::ostream& os, std::string);

#define ENCODE_CONTRACT(x) EClient::EncodeContract(msg, x);
#define ENCODE_TAGVALUELIST(x) EClient::EncodeTagValueList(msg, x);
#define ENCODE_FIELD(x) EClient::EncodeField(msg, x);
#define ENCODE_FIELD_MAX(x) EClient::EncodeFieldMax(msg, x);
#define ENCODE_MSG_ID(x) EClient::EncodeMsgId(msg, x);
#define ENCODE_RAW_INT(x) EClient::EncodeRawInt(msg, x);


#endif
