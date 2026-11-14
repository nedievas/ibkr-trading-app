# WP-6 Vulkan / ImGui resources (read-only audit, 2026-11-14, 1.6.14)

Overall: nothing here leaks or can run out in normal use. One latent Debug-build item and a few cosmetic ones.

## Leads closed

- **Descriptor pool size** - refuted as a problem. `src/main.cpp:1394-1405`: one `COMBINED_IMAGE_SAMPLER` size of `IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE` (= 8, `imgui_impl_vulkan.h:82`, "minimum per atlas"), `maxSets = 8`, `FREE_DESCRIPTOR_SET_BIT` set. The app creates no textures itself (no `ImGui_ImplVulkan_AddTexture`, no other `vkCreate*` in main.cpp). Secondary (multi-viewport) windows get their own swapchain / render pass / frames from the backend's `ImGui_ImplVulkanH_Window` and use no pool sets; only the font atlas texture(s) use the pool (one live, a second briefly while the atlas grows). 8 is enough.
- **Swapchain rebuild** - refuted. `main.cpp:8465-8475` calls `ImGui_ImplVulkanH_CreateOrResizeWindow`, which destroys the old swapchain / framebuffers / frame objects itself; nothing app-side is created per resize. Only cost: the helper does `vkDeviceWaitIdle`, so dragging the borderless window's edge stalls the GPU once per resized frame (cosmetic).
- **Shutdown order** - confirmed correct and identical to ImGui's `example_glfw_vulkan` (`main.cpp:8523-8547`): `SaveAppPrefsFile` -> client teardown -> `DestroyTradingWindows` -> `vkDeviceWaitIdle` -> notification windows/service -> `ImGui_ImplVulkan_Shutdown` -> `ImGui_ImplGlfw_Shutdown` -> `ImPlot::DestroyContext` -> `ImGui::DestroyContext` -> `CleanupVulkanWindow` -> `CleanupVulkan` (pool, device, instance) -> `glfwDestroyWindow` -> `glfwTerminate`. `ImGui::DestroyContext` also writes `imgui.ini`.
- **Per-viewport resources not destroyed** - none found in app code.
- **Font atlas, 10-24 px** - one atlas, one font (`main.cpp:8193`, 14 px, loaded once). Size changes go through `io.FontGlobalScale` (`main.cpp:2910-2915`). In 1.92 each effective size is rasterised into the same atlas as an `ImFontBaked`; unused bakes are discarded by ImGui's own GC (`imgui_draw.cpp:3940-3966`). Bounded; no per-size atlas.

### WP6-01  Error paths before the main loop skip cleanup
Where:      src/main.cpp:1413 (`exit(-1)`), 8114, 8129
What:       `"No WSI support"` calls `exit(-1)` with instance/device/window alive; `Vulkan not supported` and `Failed to create window` return after `glfwTerminate()` / `CleanupVulkan()` only. The process is exiting, so the OS reclaims everything.
Trigger:    Machine without Vulkan WSI. Startup only.
Cost:       none (process exits)
Severity:   cosmetic
Confidence: high
Fix sketch: none needed.

### WP6-02  `FontGlobalScale` is the removed-in-1.92 API; an assert guards it in Debug builds
Where:      src/main.cpp:2912 `ImGui::GetIO().FontGlobalScale = scale;`; imgui.cpp:11472
What:       ImGui 1.92 moved this to `style.FontScaleMain`; the old field still works under `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` not defined, but `imgui.cpp:11472` asserts `style.FontScaleMain == 1.0f` whenever `FontGlobalScale > 1.0f`. It holds today because `ApplyAppPrefsToStyle` restores `g_baseStyle` first and `ScaleAllSizes` does not touch `FontScaleMain` (checked). If someone later saves `g_baseStyle` after a `FontScaleMain` change, or the obsolete block is compiled out, sizes above 14 px break (assert in Debug, ignored scale in Release).
Trigger:    Only on an ImGui upgrade / config change; not today.
Cost:       none today
Severity:   cosmetic (latent)
Confidence: medium (read the assert and the one write site)
Fix sketch: set `ImGui::GetStyle().FontScaleMain = scale` after the `g_baseStyle` copy instead of `io.FontGlobalScale`. Not urgent.

### WP6-03  Stale entries accumulate in imgui.ini and are copied into every saved preset
Where:      imgui.ini written by `ImGui::DestroyContext` / autosave; `main.cpp:6654` `p.layout = ImGui::SaveIniSettingsToMemory();`
What:       ImGui keeps every window / table settings record it has ever loaded and writes them all back. Real files, counts only:
            - Windows host `.../ibkr-trading-app/imgui.ini`: 7,996 bytes, 40 `[Window]` records, 17 `[Table]` records, of which 11 are `Replay <SYMBOL>##replayN` records (old builds titled the replay window with `##`, so each symbol became its own window id; current code uses `###replay%d`, `ReplayWindow.cpp:83`, so no new ones are created).
            - Linux `~/.config/...` copy (older, Oct 31): 11,157 bytes, 79 `[Window]` records (54 `Replay <SYMBOL>##replayN`), 24 `[Table]` records. Code today has about 19 distinct `BeginTable("##...")` ids, so 5+ tables are orphans (renamed ids such as `##open`, `##history`, `##tradeHist`, `##optchain`; could not attribute hashes to names because table ids also depend on child-window ids).
            - `window-presets.cfg` (Windows host): 7,772 bytes for 1 preset (one escaped `LAYOUT` line holds the whole ini text, including the stale records).
Trigger:    Passive. Each preset adds ~8-11 KB; 10 presets would be ~80-110 KB, read once at startup (`ParseCustomPresets`) and held in memory.
Cost:       a few KB of disk and one startup parse; no per-frame cost (ImGui looks settings up by hash).
Severity:   cosmetic
Confidence: high (counts), medium (which tables are orphans)
Fix sketch: none required. If wanted: drop `[Window]` records whose name starts with `Replay ` and ends `##replay<N>` when a preset is captured. Not worth a version by itself.

## Ideas (no severity)
- `FrameRender` returns early on `VK_ERROR_OUT_OF_DATE_KHR` before resetting fences, which is the sample's behaviour and correct.
- `g_PipelineCache` is `VK_NULL_HANDLE` (`main.cpp:1306`): every start recompiles the ImGui pipelines; sub-millisecond, ignore.
