#pragma once

#include "core/models/OrderData.h"

// ============================================================================
// OrderEdit — pure mapping helpers for inline order-modify in the blotters.
//
// IB accepts a live-order modification by re-issuing placeOrder() with the same
// orderId. Only a subset of fields may change on an existing order: quantity,
// TIF, and the order's price legs (limit / stop / trigger). Side and order type
// are fixed — changing them means cancel + re-place. These helpers say, per
// order type, which price field the "Price" (primary) and "Aux" (secondary)
// blotter columns edit, so the Order Book and Orders windows share one rule.
//
// Trailing orders (Trail / TrailLimit) expose no inline price edit because the
// $/% trailing amount is ambiguous across the two columns; qty + TIF stay
// editable and the user can cancel/replace for a trail-amount change.
// ============================================================================

namespace core::services {

enum class OrderPriceField { None, Limit, Stop, Aux };

// Finished orders (filled / cancelled / rejected) stay in the Orders History
// tab and its file for a week. A row with no timestamp can't be aged, so it
// counts as expired rather than staying forever.
inline constexpr int kOrderHistoryKeepDays = 7;
inline bool OrderHistoryExpired(std::time_t updatedAt, std::time_t now,
                                int keepDays = kOrderHistoryKeepDays) {
    return updatedAt <= 0 || now - updatedAt > static_cast<std::time_t>(keepDays) * 86400;
}

struct OrderEditSpec {
    bool            qty       = true;   // quantity is always modifiable
    bool            tif       = true;   // TIF is always modifiable
    OrderPriceField primary   = OrderPriceField::None;  // "Price" column
    OrderPriceField secondary = OrderPriceField::None;  // "Aux" column
};

inline OrderEditSpec OrderEditFields(core::OrderType t) {
    OrderEditSpec s;
    using T = core::OrderType;
    switch (t) {
        case T::Limit:
        case T::LOC:       s.primary = OrderPriceField::Limit;                                       break;
        case T::Stop:      s.primary = OrderPriceField::Stop;                                        break;
        case T::StopLimit: s.primary = OrderPriceField::Stop; s.secondary = OrderPriceField::Limit;  break;
        case T::MIT:       s.primary = OrderPriceField::Aux;                                         break;
        case T::LIT:       s.primary = OrderPriceField::Aux;  s.secondary = OrderPriceField::Limit;  break;
        case T::Relative:  s.primary = OrderPriceField::Aux;                                         break;
        case T::Midprice:  s.secondary = OrderPriceField::Limit;                                     break;
        // Market / MOC / MTL — no price leg. Trail / TrailLimit — see note above.
        default: break;
    }
    return s;
}

inline double GetOrderPriceField(const core::Order& o, OrderPriceField f) {
    switch (f) {
        case OrderPriceField::Limit: return o.limitPrice;
        case OrderPriceField::Stop:  return o.stopPrice;
        case OrderPriceField::Aux:   return o.auxPrice;
        default:                     return 0.0;
    }
}

inline void SetOrderPriceField(core::Order& o, OrderPriceField f, double v) {
    switch (f) {
        case OrderPriceField::Limit: o.limitPrice = v; break;
        case OrderPriceField::Stop:  o.stopPrice  = v; break;
        case OrderPriceField::Aux:   o.auxPrice   = v; break;
        default:                                       break;
    }
}

}  // namespace core::services
