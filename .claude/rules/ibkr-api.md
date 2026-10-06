# IB API Rules

## TWS API Location
- Sources: `vendor/twsapi/IBJts/source/cppclient/client/`
- Version: 1037.02

## Known API Quirks — Never Get These Wrong

- Contract field is `primaryExchange` — NOT `primaryExch`
- `EWrapper::historicalData` Bar override must use `::Bar&` (global namespace) — NOT `core::Bar`
- `IBKRClient.h` forward-declares `::Bar` so overrides compile inside `core::services` namespace
- Connection readiness is signaled by `nextValidId()` — NOT `connectAck()`

## Connection Ports

| Mode | Gateway | TWS |
|---|---|---|
| Live | 4001 | 7497 |
| Paper | 4002 | 7496 |

IB Gateway or TWS must be running with API enabled before connecting.

## IBKRClient Bridge Pattern
- `IBKRClient.{h,cpp}` implements `EWrapper` and bridges callbacks → UI thread via `std::variant` message queue
- UI thread polls the queue each frame — never call ImGui from EWrapper callbacks directly

## Protobuf
- System protobuf: 3.21.12
- `protobufUnix/*.pb.{h,cc}` were regenerated with `protoc` 3.21.12 (originals were 3.12, incompatible)
- `protoc` binary was downloaded to `/tmp/protoc/` (not permanently installed — re-download if needed)
- Proto sources: `vendor/twsapi/IBJts/source/proto/`

## bid_stubs
- `src/bid_stubs/bid_stubs.c` provides Intel BID64 double bit-cast stubs required by ibapi-lib
- Must be compiled as C (not C++) — requires `C` in CMake project languages
