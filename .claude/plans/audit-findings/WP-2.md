# WP-2 Threads and locks: findings

Audited 1.6.14 (`86a9b4b`). Read-only; vendor code read where the app's own claims depend on it.

Note: the plan lists three extra threads. There is a fourth: the TWS library's own
`EReader` thread (`EReader::start()` -> `pthread_create(readToQueueThread)`,
vendor `EReader.cpp:67`). It runs `select()` + `recv()` and calls
`EClientSocket::onError()`, `eDisconnect()` and reads `isOutBufferEmpty()`.

## 1. Thread / data / lock map

| Thread | Reads / writes | Lock held |
|---|---|---|
| UI (main loop) | `Req*`/`Cancel*` (IBKRClient.cpp:212-740) write the socket via `m_client->...` | `m_socketMutex` (lock_guard per call) |
| UI | `ProcessMessages` swaps `m_queue` out (747-753), re-queues the tail (928-940) | `m_queueMutex`, only around the swap / re-queue; callbacks run unlocked |
| UI | `PostSend` (150) appends to `m_sendQueue` | `m_sendMutex` |
| UI | `m_filterReqId` write (687) | none (see WP2-04) |
| UI | `IsConnected()` reads `EClient::m_connState` (plain enum) | none |
| Our reader thread (IBKRClient.cpp:71) | `m_reader->processMsgs()` -> EWrapper callbacks -> `Push` (`m_queue`); `execDetails`/`commissionAndFeesReport` touch `m_pendingFills`; `m_externalOrderIds`, `m_lastOpenOrderLog`, `m_lastOrderStatusLog`, `m_lastTickPrice`, `m_filterReqId` (read, 1231) | `m_queueMutex` (Push), `m_fillsMutex` (fills only; released before Push). The other maps are reader-thread-only: OK. `processMsgs()` also calls `m_pClientSocket->onSend()` (vendor EReader.cpp:303) -> `ESocket::sendBufferedData()` on `m_outBuffer` with NO `m_socketMutex` (see WP2-02) |
| IB library `EReader` thread | `select`/`recv`, `isOutBufferEmpty()`, `eDisconnect()` on select error | its own `m_csMsgQueue`; touches `m_outBuffer` emptiness and `m_connState` without ours |
| IB send loop (IBKRClient.cpp:130) | drains `m_sendQueue`; runs `placeOrder`/`cancelOrder`/`reqAllOpenOrders`/smart-components/bulletins/display-group lambdas | `m_sendMutex` to swap, then `m_socketMutex` for the whole batch (142) |
| Connect worker (main.cpp:6125) | `client->Connect()`: `eConnect` (blocking `gethostbyname` + blocking `connect()` + handshake wait), writes `m_reader`, `m_readerThread`, `m_sendThread`, `m_running`, `m_sendRunning`; writes atomic `g_connectResult` | none inside `Connect()` (UI does not touch the client until `PollConnectState` joins it, except `AbortConnect` and WP2-01) |
| miniaudio audio thread | mixes inlined sounds; no app data | engine's own; app calls `ma_engine_play_sound`/`set_volume` from the UI thread only |
| NotificationService callers | `Notify*` called from UI callbacks only (all EWrapper callbacks only `Push`) | `m_mutex` for queue/history/pending/settings. `m_assetDir` read unlocked in `PlayNow` (set once at init: harmless) |

Members touched by two threads with no common lock/atomic: `m_filterReqId` (WP2-04), `ESocket::m_outBuffer` (WP2-02), `EClient::m_connState` (vendor, plain enum, UI/our reader/lib thread; benign in practice). Everything else is covered.

## 2. Socket writes: direct vs queued

Direct on the UI thread under `m_socketMutex`: every market-data / depth / historical /
contract / WSH / account / PnL / scanner / executions / `ReqOpenOrders` / `ReqAllOpenOrders`
call. Through the send thread: `PlaceOrder`, `CancelOrder`, `RefreshOpenOrders`,
`ReqSmartComponents`, bulletins, display-group calls.

The order split is deliberate and commented (IBKRClient.cpp:128-141, `Connect` comment at 79-81).
The smart-components / bulletins / display-group calls are queued for no stated reason;
harmless. Ordering caveat: a direct `ReqAllOpenOrders` (652) can overtake a queued
`placeOrder`; `RefreshOpenOrders` was already moved to the queue for that reason.

What the UI does if the socket blocks: **it does not block on the write.** After
`eConnect` the library sets the socket non-blocking (vendor EClientSocket.cpp:176
`SetSocketNonBlocking`). `ESocket::bufferedSend` buffers the unsent tail in `m_outBuffer`
and `handleSocketError` returns false on EWOULDBLOCK. The UI can wait only on
`m_socketMutex`, held by `SendLoop` for one batch of non-blocking writes (short). The
price is that `m_outBuffer` is unbounded if the Gateway stops reading, and it is then flushed
by the unlocked path in WP2-02. The blocking phases (DNS, `connect()`, handshake `recv`)
are on the connect worker, not the UI thread.

## 3. `m_queue` growth

`ProcessMessages()` is called at main.cpp:8447 *before* the `GLFW_ICONIFIED` check
(8475), so it keeps draining while minimised (~100 passes/s with the 10 ms sleep). Modals are
inside `RenderMainUI`, after the drain, so a modal does not stop it. States:
Connecting (also drained at 6556), SelectingAccount, Connected, LostConnection: all drained by the
main-loop call (`if (g_IBClient)`). In LostConnection the client is deleted a frame later
(`g_clientDeletePending`), which frees the queue.

Budget: 5 ms, clock checked every 64 messages (IBKRClient.cpp:918-924), so at least 64
messages per frame (~3,800/s at 60 fps) even if every handler is slow. A 300-level depth
snapshot for 10 books is ~3,000 small messages: under one frame. A backlog needs a handler cost
above ~80 us/message sustained, or a long frame. Message size: `IBMessage` is a `std::variant`,
so every element is `sizeof` the largest alternative (`MsgOpenOrder` holds a whole
`core::Order` with ~10 strings and a `ContractSpec` with a vector). I could not compile a
`sizeof` (needs generated protobuf headers); by reading the structs it is several hundred
bytes to ~1 KB per message, even for a `MsgTickPrice`. A 10 s stall at 5,000 msg/s would be
~50k x ~1 KB = ~50 MB, drained afterwards. Not a leak.

## 4. Disconnect / destructor

`~IBKRClient` -> `Disconnect()` -> joins send thread, then `eDisconnect` + joins reader
thread; `eDisconnect` joins the lib thread (vendor `EReader::stop`). Paths:

- failed connect (`eConnect` false): no threads started; destructor trivially OK.
- connect OK then connection closed during Connecting: client kept (Error state), threads self-exit or idle; the next `LaunchConnectWorker` / exit deletes it and joins. OK.
- cancel (Esc): `AbortConnect` (shutdown + close + flags) then `PollConnectState` joins the worker and calls `Disconnect()` before delete (6145-6149). If abort races the thread creation in `Connect()` the send thread waits on the cv until that `Disconnect()`. OK.
- exit while connecting: worker detached, client leaked (8517-8520). Worker touches only the leaked client and the atomic `g_connectResult`; leaked client's `std::thread`s are never destroyed so no `terminate`. Refuted as a bug.
- disconnect from inside a callback: the only in-callback teardown is `g_clientDeletePending` (4370, deferred to 8452). The menu `Disconnect()` runs from `RenderTradingUI`, outside `ProcessMessages`. OK, except WP2-01.

Lock order: `SendLoop` takes `m_sendMutex` (released) then `m_socketMutex`; UI takes `m_socketMutex` and, via the lib's synchronous `wrapper->error()` (e.g. not connected), `m_queueMutex` inside it; `ProcessMessages` holds `m_queueMutex` only for the swap and never while calling a callback; reader holds `m_fillsMutex` and releases it before `Push`. No path takes them in the opposite order: **no deadlock**.

## 5. NotificationService

- miniaudio: `ma_engine_play_sound` uses *inlined* sounds that are pooled and recycled, not freed per play (miniaudio.h:76453-76570: it scans `pInlinedSoundHead` for `ma_sound_at_end`, uninits and reuses it; otherwise mallocs one and links it). They are all freed in `ma_engine_uninit` (76040) which `Shutdown()` calls. Pool size = peak concurrent sounds, each with the shared WAV data (tones 232 KB + voice 516 KB on disk in total). Bounded; not a leak. A missing WAV logs one line per play.
- Caps: `m_queue` (`kMaxQueued` 64) and `m_history` (`kMaxHistory` 500) are only ever inserted in `NotifyForce` (single insert path) and capped there. Enforced on every path. **`m_pendingPlays` has no cap** (WP2-03).

## Findings

### WP2-01  File -> Disconnect during an in-flight reconnect deletes the client under the connect worker
Where:      src/main.cpp:6107-6128 (worker), 6210-6222 (`Disconnect()`), 7479 (menu), 8442-8457 (reconnect timer)
What:       `StartSilentReconnect()` -> `LaunchConnectWorker(true)` starts a worker running `client->Connect(...)` (blocking `gethostbyname` + `connect()` + handshake). In LostConnection the File menu is live (`RenderTradingUI`). `Disconnect()` does `g_IBClient->Disconnect(); delete g_IBClient;` with no check of `g_connectInFlight` and no `AbortConnect()`/join:
            `g_IBClient->Disconnect();`  `delete g_IBClient;  g_IBClient = nullptr;`
            `~IBKRClient` -> `Disconnect()` skips `eDisconnect` (`isConnected()` is false mid-handshake) and `delete m_client` frees the `EClientSocket` the worker is inside `eConnect` on. `g_connectInFlight`/`g_connectThread` stay set and are only reaped by a later `PollConnectState`.
Trigger:    Gateway restarting or the host dropping SYNs (so `connect()` hangs for the OS timeout, ~20 s Windows / longer Linux); the 5 s reconnect timer starts an attempt; user clicks File -> Disconnect during it. With a refused port the window is milliseconds, so rare.
Cost:       use-after-free of `EClientSocket` / `m_signal` / the client's mutexes -> crash (or silent heap corruption). Not a leak.
Severity:   crash-risk
Confidence: medium (read both paths end to end; not reproduced, timing window)
Fix sketch: in `Disconnect()`, if `g_connectInFlight` call `CancelConnect()`-style teardown (set `g_connectCancelled`, `AbortConnect()`) and let `PollConnectState` delete the client, or `join` the worker before the `delete`.

### WP2-02  Reader thread flushes the socket's out-buffer without `m_socketMutex`
Where:      src/core/services/IBKRClient.h:541-547 (comment), IBKRClient.cpp:71-77 (reader loop); vendor EReader.cpp:301-304, ESocket.cpp (`sendBufferedData`, `bufferedSend`)
What:       The comment claims "Every send holds this lock". `EReader::processMsgs()` begins with `m_pClientSocket->onSend()`, which calls `ESocket::sendBufferedData()` on `m_outBuffer` from our reader thread, holding no lock. UI-thread requests and the send thread run `ESocket::bufferedSend()` (`m_outBuffer.insert(...)`, `CleanupBuffer`) under `m_socketMutex` only. The library's own thread also reads `isOutBufferEmpty()` each `select`. `m_outBuffer` is non-empty whenever a send was partial or hit EWOULDBLOCK (the socket is non-blocking).
Trigger:    Gateway/network slow to read while a burst of requests goes out (e.g. connect-time subscriptions, a style switch re-subscribing 10 charts, a 60-option-leg chain load), then any tick arriving makes `processMsgs()` run `onSend()` while the UI thread appends -> two threads erase/insert the same `std::vector<char>`: duplicated or lost wire bytes, a corrupt request, or a heap crash. With an empty buffer the race is only a read of `empty()`.
Cost:       corrupt/duplicated outgoing message; on a live account that could be an order. Not measured; needs backpressure, which loopback rarely gives.
Severity:   crash-risk
Confidence: medium-low (vendor path read; no demonstration that `m_outBuffer` is ever non-empty in practice)
Fix sketch: none in app code short of locking around `processMsgs()` (not safe: callbacks only Push, so locking `m_socketMutex` around `m_reader->processMsgs()` at IBKRClient.cpp:76 would be correct and cheap, but it also serialises the UI's sends behind decoding); decide in Phase 2.

### WP2-03  Notification voice stalls while minimised; `m_pendingPlays` is uncapped
Where:      src/main.cpp:7867 (`Tick()` only in `RenderTradingUI`), 8475-8478 (minimised `continue`); NotificationService.cpp:271-278 (`push_back`, no cap)
What:       The tone is due immediately and plays from `NotifyForce`'s own `Tick()`; the voice is due `kVoiceDelayMs` (150 ms) later and only plays on the next `RenderTradingUI`. While minimised the loop does `ImGui_ImplGlfw_Sleep(10); continue;` before `RenderMainUI`, so no `Tick`. Pending plays accumulate (`m_queue` and `m_history` are capped; `m_pendingPlays` is not) and all play at once when the window is restored.
Trigger:    minimise with a price alert, fill or hold notification arriving (tone sounds, voice does not); restore after N events -> N voices at once.
Cost:       ~24 bytes per pending play; N is the number of notifications while minimised (tens at most). Functional cost is the missing/stacked voice.
Severity:   leak-bounded (cosmetic)
Confidence: high
Fix sketch: call `g_NotificationService->Tick()` in the minimised branch (one line) and optionally cap `m_pendingPlays` like `kMaxQueued`.

### WP2-04  `m_filterReqId` is written by the UI thread and read by the reader thread
Where:      IBKRClient.cpp:687 (write), 1231 (read), IBKRClient.h:572
What:       `m_filterReqId = isFiltered ? reqId : -1;` on the UI thread (under `m_socketMutex`) vs `bool fromQuery = (m_filterReqId >= 0 && reqId == m_filterReqId);` in `execDetails` on the reader thread (which does not take `m_socketMutex`). Plain `int`, no atomic.
Trigger:    the user presses Load in the Orders history filter while executions are arriving.
Cost:       formal data race; in practice a stale value misclassifies one fill's `fromQuery` flag (tint / append-vs-replace).
Severity:   cosmetic
Confidence: high
Fix sketch: `std::atomic<int>`.

### WP2-05  Retrying Connect right after Esc can freeze the UI in `join()`
Where:      src/main.cpp:6108, 6182-6189; IBKRClient.cpp:92-110
What:       `LaunchConnectWorker` begins with `if (g_connectThread.joinable()) g_connectThread.join();` on the UI thread. After Esc, `CancelConnect` sets state Disconnected but `g_connectInFlight` and the thread stay until `PollConnectState` sees `g_connectResult != 0`. `AbortConnect` only helps if the fd already exists: `m_fd` is created after `gethostbyname`, so an abort during DNS (or before `socket()`) is missed and the worker proceeds into a blocking `connect()`. The login button is enabled again immediately, so a quick second Connect joins a worker that is still in `connect()`.
Trigger:    Connect to a hostname/IP that drops SYNs, Esc, press Connect again within the OS connect timeout.
Cost:       UI frozen until the TCP connect timeout (~20 s Windows / ~2 min Linux).
Severity:   crash-risk (hang)
Confidence: low (the abort-miss window is narrow; with the fd present `shutdown()` on SYN_SENT does abort `connect()` on Linux)
Fix sketch: disable the Connect button while `g_connectInFlight`, or skip the join (poll `g_connectResult`) and defer the launch.

### WP2-06  IB error 1100 is treated as a lost connection, forcing a full teardown and resubscribe
Where:      IBKRClient.cpp:975-978; main.cpp:4362-4380
What:       `if (errorCode == 1100 || errorCode == 1300 || ...) { Push(MsgConnection{false, ...}); return; }`. 1100 means "connectivity between IB and TWS lost" with the socket still up; IB sends 1101/1102 when it recovers and data resumes. The app instead sets LostConnection, deletes the client (closing a healthy socket) and reconnects after 5 s, then re-subscribes every chart/book/chain/watchlist.
Trigger:    any short IB-server connectivity blip (nightly Gateway reset window, ISP flap). Count not measured.
Cost:       one full re-subscription burst per blip (dozens to ~200 market-data / history lines) and one `reqHistoricalData` per chart, i.e. pacing budget; data gap for the 5+ s reconnect.
Severity:   waste-ib
Confidence: medium (the code is explicit; whether 1100 is also followed by a real socket close on this user's Gateway versions is not known)
Fix sketch: treat 1100 as a warning (badge), act only on 1300 / socket close; 1101/1102 clear the badge.

### WP2-07  `ProcessMessages` re-queues the backlog by moving the whole tail under `m_queueMutex` every frame
Where:      IBKRClient.cpp:928-940
What:       When the 5 ms budget is hit, the unprocessed tail is moved into a new vector, the current `m_queue` is appended behind it, and `m_queue` is replaced, all under `m_queueMutex`, so the reader thread's `Push` waits for it. Cost is O(backlog) element moves of ~1 KB variants per frame.
Trigger:    only a sustained backlog (see section 3), e.g. thousands of messages with slow handlers.
Cost:       at 20k queued messages ~20 MB of moves per frame; not seen to happen in normal use.
Severity:   waste-cpu
Confidence: low (cost only matters if a backlog forms; none shown)
Fix sketch: pop from a `std::deque` instead, or keep an index into `batch` between frames.

## Leads closed

- UI blocks on a stalled socket write: refuted (non-blocking socket, see section 2). Replaced by the unbounded `m_outBuffer` note.
- `m_queue` not drained while minimised / modal / connecting / LostConnection: refuted (section 3).
- Deadlock between `m_queueMutex` / `m_sendMutex` / `m_socketMutex`: refuted (section 4).
- Threads not joined on failed connect / cancel / exit: refuted; exit-while-connecting detach is safe (section 4). The one unsafe teardown is WP2-01.
- miniaudio sounds leak: refuted (pooled and recycled, freed at `ma_engine_uninit`).
- Queue/history caps bypassed: refuted (single insert path), except `m_pendingPlays` (WP2-03).
- Could not decide: the library calls `pthread_join` on the same `EReader` thread up to three times on a normal `Disconnect()` (`eDisconnect` -> `stop()`, `~EReader` -> `stop()`, `eDisconnect` again) with `m_hReadThread` never cleared (vendor EReader.cpp:53-84). A second join of an already-joined pthread is undefined; it has not been seen to fail here. Vendor code, left alone.
- Small per-session growth for WP-3: `m_lastOpenOrderLog` / `m_lastOrderStatusLog` (one string per order id, ~100-300 B each, reader thread only), `m_externalOrderIds`, `m_pendingFills` (an exec whose commission report never arrives). Hundreds of entries per day at most; lost with the client on every reconnect.
- `m_sendQueue` is unbounded if the send thread stalls (the `RefreshOpenOrders` closure is added every 5 s): a few hundred bytes per 5 s, only while stalled.

## Ideas (no severity)

- Move the smart-components / bulletin / display-group calls to the same path as the other `Req*` (or document why they are queued).
- A `static_assert(sizeof(IBMessage) <= N)` or boxing `MsgOpenOrder` / `MsgPosition` would shrink every queued tick.
