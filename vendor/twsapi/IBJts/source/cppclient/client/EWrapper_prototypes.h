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

#ifndef TWS_API_CLIENT_EWRAPPER_PROTOTYPES_H
#define TWS_API_CLIENT_EWRAPPER_PROTOTYPES_H

#define EWrapper_methods_regular(X, ARGS) \
  X(tickPrice, (int reqId, TickType field, double price, const TickAttrib& attrib)) \
  X(tickSize, (int reqId, TickType field, Decimal size)) \
  X(tickOptionComputation, (int reqId, TickType tickType, int tickAttrib, double impliedVol, double delta, \
    double optPrice, double pvDividend, double gamma, double vega, double theta, double undPrice)) \
  X(tickGeneric, (int reqId, TickType tickType, double value)) \
  X(tickString, (int reqId, TickType tickType, const std::string& value)) \
  X(tickEFP, (int reqId, TickType tickType, double basisPoints, const std::string& formattedBasisPoints, \
    double totalDividends, int holdDays, const std::string& futureLastTradeDate, double dividendImpact, double dividendsToLastTradeDate)) \
  X(orderStatus, (int orderId, const std::string& status, Decimal filled, \
    Decimal remaining, double avgFillPrice, long long permId, int parentId, \
    double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice)) \
  X(openOrder, (int orderId, const Contract&, const Order&, const OrderState&)) \
  X(openOrderEnd, ()) \
  X(winError, (const std::string& str, int lastError)) \
  X(connectionClosed, ()) \
  X(updateAccountValue, (const std::string& key, const std::string& val, \
    const std::string& currency, const std::string& accountName)) \
  X(updatePortfolio, (const Contract& contract, Decimal position, \
    double marketPrice, double marketValue, double averageCost, \
    double unrealizedPNL, double realizedPNL, const std::string& accountName)) \
  X(updateAccountTime, (const std::string& timeStamp)) \
  X(accountDownloadEnd, (const std::string& accountName)) \
  X(nextValidId, (int orderId)) \
  X(contractDetails, (int reqId, const ContractDetails& contractDetails)) \
  X(bondContractDetails, (int reqId, const ContractDetails& contractDetails)) \
  X(contractDetailsEnd, (int reqId)) \
  X(execDetails, (int reqId, const Contract& contract, const Execution& execution)) \
  X(execDetailsEnd, (int reqId)) \
  X(error, (int id, long long errorTimeMs, int errorCode,const std::string& errorString, const std::string& advancedOrderRejectJson)) \
  X(updateMktDepth, (int reqId, int position, int operation, int side, \
    double price, Decimal size)) \
  X(updateMktDepthL2, (int reqId, int position, const std::string& marketMaker, int operation, \
    int side, double price, Decimal size, bool isSmartDepth)) \
  X(updateNewsBulletin, (int msgId, int msgType, const std::string& newsMessage, const std::string& originExch)) \
  X(managedAccounts, (const std::string& accountsList)) \
  X(receiveFA, (faDataType pFaDataType, const std::string& cxml)) \
  X(historicalData, (int reqId, const Bar& bar)) \
  X(historicalDataEnd, (int reqId, const std::string& startDateStr, const std::string& endDateStr)) \
  X(scannerParameters, (const std::string& xml)) \
  X(scannerData, (int reqId, int rank, const ContractDetails& contractDetails, \
    const std::string& distance, const std::string& benchmark, const std::string& projection, \
    const std::string& legsStr)) \
  X(scannerDataEnd, (int reqId)) \
  X(realtimeBar, (int reqId, long long time, double open, double high, double low, double close, \
    Decimal volume, Decimal wap, int count)) \
  X(currentTime, (long long time)) \
  X(deltaNeutralValidation, (int reqId, const DeltaNeutralContract& deltaNeutralContract)) \
  X(tickSnapshotEnd, (int reqId)) \
  X(marketDataType, (int reqId, int marketDataType)) \
  X(commissionAndFeesReport, (const CommissionAndFeesReport& commissionAndFeesReport)) \
  X(position, (const std::string& account, const Contract& contract, Decimal position, double avgCost)) \
  X(positionEnd, ()) \
  X(accountSummary, (int reqId, const std::string& account, const std::string& tag, const std::string& value, const std::string& currency)) \
  X(accountSummaryEnd, (int reqId)) \
  X(verifyMessageAPI, (const std::string& apiData)) \
  X(verifyCompleted, (bool isSuccessful, const std::string& errorText)) \
  X(displayGroupList, (int reqId, const std::string& groups)) \
  X(displayGroupUpdated, (int reqId, const std::string& contractInfo)) \
  X(verifyAndAuthMessageAPI, (const std::string& apiData, const std::string& xyzChallange)) \
  X(verifyAndAuthCompleted, (bool isSuccessful, const std::string& errorText)) \
  X(connectAck, ()) \
  X(positionMulti, (int reqId, const std::string& account,const std::string& modelCode, const Contract& contract, Decimal pos, double avgCost)) \
  X(positionMultiEnd, (int reqId)) \
  X(accountUpdateMulti, (int reqId, const std::string& account, const std::string& modelCode, const std::string& key, const std::string& value, const std::string& currency)) \
  X(accountUpdateMultiEnd, (int reqId)) \
  X(securityDefinitionOptionalParameter, (int reqId, const std::string& exchange, int underlyingConId, const std::string& tradingClass, \
    const std::string& multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes)) \
  X(securityDefinitionOptionalParameterEnd, (int reqId)) \
  X(softDollarTiers, (int reqId, const std::vector<SoftDollarTier> &tiers)) \
  X(familyCodes, (const std::vector<FamilyCode> &familyCodes)) \
  X(symbolSamples, (int reqId, const std::vector<ContractDescription> &contractDescriptions)) \
  X(mktDepthExchanges, (const std::vector<DepthMktDataDescription> &depthMktDataDescriptions)) \
  X(tickNews, (int reqId, long long timeStampMs, const std::string& providerCode, const std::string& articleId, const std::string& headline, const std::string& extraData)) \
  X(smartComponents, (int reqId, const SmartComponentsMap& theMap)) \
  X(tickReqParams, (int reqId, double minTick, const std::string& bboExchange, int snapshotPermissions)) \
  X(newsProviders, (const std::vector<NewsProvider> &newsProviders)) \
  X(newsArticle, (int requestId, int articleType, const std::string& articleText)) \
  X(historicalNews, (int requestId, const std::string& time, const std::string& providerCode, const std::string& articleId, const std::string& headline)) \
  X(historicalNewsEnd, (int requestId, bool hasMore)) \
  X(headTimestamp, (int reqId, const std::string& headTimestamp)) \
  X(histogramData, (int reqId, const HistogramDataVector& data)) \
  X(historicalDataUpdate, (int reqId, const Bar& bar)) \
  X(rerouteMktDataReq, (int reqId, int conid, const std::string& exchange)) \
  X(rerouteMktDepthReq, (int reqId, int conid, const std::string& exchange)) \
  X(marketRule, (int marketRuleId, const std::vector<PriceIncrement> &priceIncrements)) \
  X(pnl, (int reqId, double dailyPnL, double unrealizedPnL, double realizedPnL)) \
  X(pnlSingle, (int reqId, Decimal pos, double dailyPnL, double unrealizedPnL, double realizedPnL, double value)) \
  X(historicalTicks, (int reqId, const std::vector<HistoricalTick> &ticks, bool done)) \
  X(historicalTicksBidAsk, (int reqId, const std::vector<HistoricalTickBidAsk> &ticks, bool done)) \
  X(historicalTicksLast, (int reqId, const std::vector<HistoricalTickLast> &ticks, bool done)) \
  X(tickByTickAllLast, (int reqId, int tickType, long long time, double price, Decimal size, const TickAttribLast& tickAttribLast, const std::string& exchange, const std::string& specialConditions)) \
  X(tickByTickBidAsk, (int reqId, long long time, double bidPrice, double askPrice, Decimal bidSize, Decimal askSize, const TickAttribBidAsk& tickAttribBidAsk)) \
  X(tickByTickMidPoint, (int reqId, long long time, double midPoint)) \
  X(orderBound, (long long permId, int clientId, int orderId)) \
  X(completedOrder, (const Contract& contract, const Order& order, const OrderState& orderState)) \
  X(completedOrdersEnd, ()) \
  X(replaceFAEnd, (int reqId, const std::string& text)) \
  X(wshMetaData, (int reqId, const std::string& dataJson)) \
  X(wshEventData, (int reqId, const std::string& dataJson)) \
  X(historicalSchedule, (int reqId, const std::string& startDateTime, const std::string& endDateTime, const std::string& timeZone, const std::vector<HistoricalSession>& sessions)) \
  X(userInfo, (int reqId, const std::string& whiteBrandingId)) \
  X(currentTimeInMillis, (long long timeInMillis))

// protobuf
#if !defined(USE_WIN_DLL)
#define EWrapper_methods_proto(X, ARGS) \
  X(execDetailsProtoBuf, (const protobuf::ExecutionDetails& executionDetailsProto)) \
  X(execDetailsEndProtoBuf, (const protobuf::ExecutionDetailsEnd& executionDetailsEndProto)) \
  X(orderStatusProtoBuf, (const protobuf::OrderStatus& orderStatusProto)) \
  X(openOrderProtoBuf, (const protobuf::OpenOrder& openOrderProto)) \
  X(openOrdersEndProtoBuf, (const protobuf::OpenOrdersEnd& openOrderEndProto)) \
  X(errorProtoBuf, (const protobuf::ErrorMessage& errorProto)) \
  X(completedOrderProtoBuf, (const protobuf::CompletedOrder& completedOrderProto)) \
  X(completedOrdersEndProtoBuf, (const protobuf::CompletedOrdersEnd& completedOrdersEndProto)) \
  X(orderBoundProtoBuf, (const protobuf::OrderBound& orderBoundProto)) \
  X(contractDataProtoBuf, (const protobuf::ContractData& contractDataProto)) \
  X(bondContractDataProtoBuf, (const protobuf::ContractData& contractDataProto)) \
  X(contractDataEndProtoBuf, (const protobuf::ContractDataEnd& contractDataEndProto)) \
  X(tickPriceProtoBuf, (const protobuf::TickPrice& tickPriceProto)) \
  X(tickSizeProtoBuf, (const protobuf::TickSize& tickSizeProto)) \
  X(tickOptionComputationProtoBuf, (const protobuf::TickOptionComputation& tickOptionComputationProto)) \
  X(tickGenericProtoBuf, (const protobuf::TickGeneric& tickGenericProto)) \
  X(tickStringProtoBuf, (const protobuf::TickString& tickStringProto)) \
  X(tickSnapshotEndProtoBuf, (const protobuf::TickSnapshotEnd& tickSnapshotEndProto)) \
  X(updateMarketDepthProtoBuf, (const protobuf::MarketDepth& marketDepthProto)) \
  X(updateMarketDepthL2ProtoBuf, (const protobuf::MarketDepthL2& marketDepthL2Proto)) \
  X(marketDataTypeProtoBuf, (const protobuf::MarketDataType& marketDataTypeProto)) \
  X(tickReqParamsProtoBuf, (const protobuf::TickReqParams& tickReqParamsProto)) \
  X(updateAccountValueProtoBuf, (const protobuf::AccountValue& accountValueProto)) \
  X(updatePortfolioProtoBuf, (const protobuf::PortfolioValue& portfolioValueProto)) \
  X(updateAccountTimeProtoBuf, (const protobuf::AccountUpdateTime& accountUpdateTimeProto)) \
  X(accountDataEndProtoBuf, (const protobuf::AccountDataEnd& accountDataEndProto)) \
  X(managedAccountsProtoBuf, (const protobuf::ManagedAccounts& managedAccountsProto)) \
  X(positionProtoBuf, (const protobuf::Position& positionProto)) \
  X(positionEndProtoBuf, (const protobuf::PositionEnd& positionEndProto)) \
  X(accountSummaryProtoBuf, (const protobuf::AccountSummary& accountSummaryProto)) \
  X(accountSummaryEndProtoBuf, (const protobuf::AccountSummaryEnd& accountSummaryEndProto)) \
  X(positionMultiProtoBuf, (const protobuf::PositionMulti& positionMultiProto)) \
  X(positionMultiEndProtoBuf, (const protobuf::PositionMultiEnd& positionMultiEndProto)) \
  X(accountUpdateMultiProtoBuf, (const protobuf::AccountUpdateMulti& accountUpdateMultiProto)) \
  X(accountUpdateMultiEndProtoBuf, (const protobuf::AccountUpdateMultiEnd& accountUpdateMultiEndProto)) \
  X(historicalDataProtoBuf, (const protobuf::HistoricalData& historicalDataProto)) \
  X(historicalDataUpdateProtoBuf, (const protobuf::HistoricalDataUpdate& historicalDataUpdateProto)) \
  X(historicalDataEndProtoBuf, (const protobuf::HistoricalDataEnd& historicalDataEndProto)) \
  X(realTimeBarTickProtoBuf, (const protobuf::RealTimeBarTick& realTimeBarTickProto)) \
  X(headTimestampProtoBuf, (const protobuf::HeadTimestamp& headTimestampProto)) \
  X(histogramDataProtoBuf, (const protobuf::HistogramData& histogramDataProto)) \
  X(historicalTicksProtoBuf, (const protobuf::HistoricalTicks& historicalTicksProto)) \
  X(historicalTicksBidAskProtoBuf, (const protobuf::HistoricalTicksBidAsk& historicalTicksBidAskProto)) \
  X(historicalTicksLastProtoBuf, (const protobuf::HistoricalTicksLast& historicalTicksLastProto)) \
  X(tickByTickDataProtoBuf, (const protobuf::TickByTickData& tickByTickDataProto)) \
  X(updateNewsBulletinProtoBuf, (const protobuf::NewsBulletin& newsBulletinProto)) \
  X(newsArticleProtoBuf, (const protobuf::NewsArticle& newsArticleProto)) \
  X(newsProvidersProtoBuf, (const protobuf::NewsProviders& newsProvidersProto)) \
  X(historicalNewsProtoBuf, (const protobuf::HistoricalNews& historicalNewsProto)) \
  X(historicalNewsEndProtoBuf, (const protobuf::HistoricalNewsEnd& historicalNewsEndProto)) \
  X(wshMetaDataProtoBuf, (const protobuf::WshMetaData& wshMetaDataProto)) \
  X(wshEventDataProtoBuf, (const protobuf::WshEventData& wshEventDataProto)) \
  X(tickNewsProtoBuf, (const protobuf::TickNews& tickNewsProto)) \
  X(scannerParametersProtoBuf, (const protobuf::ScannerParameters& scannerParametersProto)) \
  X(scannerDataProtoBuf, (const protobuf::ScannerData& scannerDataProto)) \
  X(pnlProtoBuf, (const protobuf::PnL& pnlProto)) \
  X(pnlSingleProtoBuf, (const protobuf::PnLSingle& pnlSingleProto)) \
  X(receiveFAProtoBuf, (const protobuf::ReceiveFA& receiveFAProto)) \
  X(replaceFAEndProtoBuf, (const protobuf::ReplaceFAEnd& replaceFAEndProto)) \
  X(commissionAndFeesReportProtoBuf, (const protobuf::CommissionAndFeesReport& commissionAndFeesReportProto)) \
  X(historicalScheduleProtoBuf, (const protobuf::HistoricalSchedule& historicalScheduleProto)) \
  X(rerouteMarketDataRequestProtoBuf, (const protobuf::RerouteMarketDataRequest& rerouteMarketDataRequestProto)) \
  X(rerouteMarketDepthRequestProtoBuf, (const protobuf::RerouteMarketDepthRequest& rerouteMarketDepthRequestProto)) \
  X(secDefOptParameterProtoBuf, (const protobuf::SecDefOptParameter& secDefOptParameterProto)) \
  X(secDefOptParameterEndProtoBuf, (const protobuf::SecDefOptParameterEnd& secDefOptParameterEndProto)) \
  X(softDollarTiersProtoBuf, (const protobuf::SoftDollarTiers& softDollarTiersProto)) \
  X(familyCodesProtoBuf, (const protobuf::FamilyCodes& familyCodesProto)) \
  X(symbolSamplesProtoBuf, (const protobuf::SymbolSamples& symbolSamplesProto)) \
  X(smartComponentsProtoBuf, (const protobuf::SmartComponents& smartComponentsProto)) \
  X(marketRuleProtoBuf, (const protobuf::MarketRule& marketRuleProto)) \
  X(userInfoProtoBuf, (const protobuf::UserInfo& userInfoProto)) \
  X(nextValidIdProtoBuf, (const protobuf::NextValidId& nextValidIdProto)) \
  X(currentTimeProtoBuf, (const protobuf::CurrentTime& currentTimeProto)) \
  X(currentTimeInMillisProtoBuf, (const protobuf::CurrentTimeInMillis& currentTimeInMillisProto)) \
  X(verifyMessageApiProtoBuf, (const protobuf::VerifyMessageApi& verifyMessageApiProto)) \
  X(verifyCompletedProtoBuf, (const protobuf::VerifyCompleted& verifyCompletedProto)) \
  X(displayGroupListProtoBuf, (const protobuf::DisplayGroupList& displayGroupListProto)) \
  X(displayGroupUpdatedProtoBuf, (const protobuf::DisplayGroupUpdated& displayGroupUpdatedProto)) \
  X(marketDepthExchangesProtoBuf, (const protobuf::MarketDepthExchanges& marketDepthExchangesProto)) \
  X(configResponseProtoBuf, (const protobuf::ConfigResponse& configResponseProto)) \
  X(updateConfigResponseProtoBuf, (const protobuf::UpdateConfigResponse& updateConfigResponseProto))
#else
#define EWrapper_methods_proto(X, ARGS)
#endif

#define EWrapper_methods(X, ARGS) EWrapper_methods_regular(X, ARGS) EWrapper_methods_proto(X, ARGS)
#define EWrapper_methods_pure_virtual(name, args) virtual void name args = 0;
#define EWrapper_methods_override(name, args) virtual void name args override;
#define EWrapper_methods_deprecated(name, args) virtual void name args EWRAPPER_VIRTUAL_IMPL;
#define EWrapper_methods_def_value(v) v
#define EWrapper_methods_nodef_value(v)

#define EWrapper_declare(X) EWrapper_methods(X, EWrapper_methods_def_value)
#define EWrapper_define(X) EWrapper_methods(X, EWrapper_methods_nodef_value)

#else

#if 0
// Note, you should replace #include "EWrapper_prototypes.h" with EWrapper_declare(EWrapper_methods_override)
#ifdef _MSC_VER
#pragma message(__FILE__ "(" _CRT_STRINGIZE(__LINE__) "): this header is deprecated, use EWrapper_declare macro")
#else
#warning "this header is deprecated, use EWrapper_declare macro"
#endif
#endif

#if ! defined(EWRAPPER_VIRTUAL_IMPL)
# define EWRAPPER_VIRTUAL_IMPL
#endif
EWrapper_declare(EWrapper_methods_deprecated)
#undef EWRAPPER_VIRTUAL_IMPL

#endif // TWS_API_CLIENT_EWRAPPER_PROTOTYPES_H
