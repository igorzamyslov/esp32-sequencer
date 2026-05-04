# Sequence Builder — Design

Date: 2026-05-04
Status: Draft → review

## 1. Motivation

Today the firmware runs one hardcoded `Sequence::run()` that hops networks, wakes a PC, wakes a Samsung TV, and navigates to HDMI3. Every change — different TV brand, different routine, different button — requires editing C++ and reflashing.

The goal: turn the project into a generic *sequence builder* where:

- Functional pieces are uniform abstractions (wake host, switch input, hop WiFi, wait, …).
- Sequences compose those pieces and are authored at runtime in a web editor.
- Sequences are bound to triggers (BLE detection, HTTP route, …) at runtime.
- Vendor-specific code (Samsung Tizen, Sony WoL, etc.) lives in a clearly separated layer so anyone can swap it for their own hardware.

## 2. Layering

Three layers, strict directional dependency (Web → Core → Adapters; Core never includes adapter headers):

```
Web         → HTML+JS editor served from PROGMEM, drives JSON API
Core        → Block / Predicate / Trigger interfaces, Registry,
              SequenceStore, Interpreter, TriggerManager
Adapters    → Concrete blocks (WoL, WifiHop, SamsungTizenKeys, …),
              concrete predicates, concrete triggers — one .cpp each
```

To repurpose for an LG TV: add `adapters/blocks/LgWebOsKeysBlock.cpp`, register it, remove the Samsung one. No edits to Core, no edits to the editor.

## 3. Core abstractions

### 3.1 Block

```cpp
struct RunCtx {
    Config*         config;
    NetworkManager* net;
    StatusLed*      led;
    // Scratchpad for blocks that share transient state within one run.
    // Prefer not to use this — keep blocks self-contained when possible.
    std::map<String, String> scratch;
};

enum class RunStatus { Ok, Failed };
struct RunResult { RunStatus status; String error; };

class Block {
public:
    virtual ~Block() = default;
    virtual const BlockSchema& schema() const = 0;
    virtual RunResult run(const JsonVariantConst& params,
                          const std::vector<Node>& children,
                          RunCtx& ctx, Interpreter& interp) = 0;
};
```

Most blocks ignore `children` (leaves). `RepeatBlock` and `IfBlock` use them.

### 3.2 Predicate

```cpp
class Predicate {
public:
    virtual ~Predicate() = default;
    virtual const PredicateSchema& schema() const = 0;
    virtual bool test(const JsonVariantConst& params, RunCtx& ctx) = 0;
};
```

### 3.3 Trigger

```cpp
class Trigger {
public:
    virtual ~Trigger() = default;
    virtual const TriggerSchema& schema() const = 0;
    // Called when a binding is created/loaded. The trigger arms itself
    // (registers HTTP route, subscribes to BLE, etc.) and calls onFire()
    // when its condition is met. Must be idempotent on repeated bind.
    virtual void bind(const String& bindingId,
                      const JsonVariantConst& params,
                      std::function<void()> onFire) = 0;
    virtual void unbind(const String& bindingId) = 0;
    // Whether sequences fired by this trigger should pause BLE scanning.
    virtual bool pausesBleScan() const { return true; }
};
```

### 3.4 Schema

Blocks/predicates/triggers each declare a schema describing their identifier, label, category, and editable fields. `GET /api/schema` dumps all schemas as JSON; the editor renders fields purely from this.

```cpp
enum class FieldType { Bool, Int, String, StringList, MacAddress, Enum, PredicateRef };

struct FieldDef {
    const char* key;
    FieldType   type;
    const char* label;
    const char* defaultValue; // optional
    const char* enumValues;   // CSV, only for Enum
    bool        required;
};

struct BlockSchema {
    const char*  type;        // stable id, e.g. "samsung-keys"
    const char*  label;       // human, e.g. "Samsung TV: send keys"
    const char*  category;    // grouping in palette, e.g. "TV (Samsung)"
    const FieldDef* fields;
    size_t       fieldCount;
    // Named child slots. Empty for leaf blocks.
    // Repeat declares ["body"]; If declares ["then", "else"].
    const char* const* childSlots;
    size_t       childSlotCount;
};
```

Schemas are `static constexpr` in each adapter `.cpp`.

### 3.5 Registry & self-registration

```cpp
class Registry {
public:
    static Registry& instance();
    void registerBlock(Block* b);
    void registerPredicate(Predicate* p);
    void registerTrigger(Trigger* t);
    Block*     resolveBlock(const String& type);
    Predicate* resolvePredicate(const String& type);
    Trigger*   resolveTrigger(const String& type);
    String     dumpSchemaJson(); // for /api/schema
    // ... iterators for boot-time setup
};

// In each adapter .cpp:
static SamsungTizenKeysBlock _instance;
static struct _Reg { _Reg(){ Registry::instance().registerBlock(&_instance); } } _r;
```

Static-initializer registration runs before `setup()`. Adding a block = adding a `.cpp` file and listing it in `platformio.ini` `build_src_filter`.

## 4. Data model

```cpp
struct Node {
    String type;             // "wol", "wifi-hop", "samsung-keys", "if", "repeat", ...
    JsonDocument params;     // type-specific
    // Named child slots. Leaves have an empty map.
    // Repeat uses {"body": [...]}. If uses {"then": [...], "else": [...]}.
    // The slot names a block declares come from its schema (`childSlots`).
    std::map<String, std::vector<Node>> children;
};

struct Sequence {
    String id;               // 8-char hex, server-assigned
    String name;
    uint32_t cooldownMs;     // default 60000, 0 = no cooldown
    std::vector<Node> nodes;
};

struct TriggerBinding {
    String id;               // 8-char hex
    String type;             // "ble-mac", "http-route", ...
    JsonDocument params;
    String sequenceId;
    bool   enabled;
};
```

### 4.1 Storage

Two NVS keys: `sequences` and `triggers`, each holding a single JSON array. Loaded once into RAM stores at boot. Saving rewrites the whole blob — sequences are kilobytes, partial-update complexity isn't worth it.

Forward-compat: a stored node with an unknown `type` causes a load-time warning; the sequence still loads but is marked broken in `/api/sequences` (`broken: true`) and refuses to run.

### 4.2 Identity

`Sequence.id` and `TriggerBinding.id` are server-generated on first save. Triggers reference sequences by id — renaming a sequence does not break bindings.

## 5. Execution

### 5.1 Single-threaded interpreter

`loop()` owns execution. One sequence runs at a time. Triggers and `POST /api/sequences/:id/run` push the id to a 1-deep queue. If full → 429.

```cpp
RunResult Interpreter::run(const Node& n, RunCtx& ctx) {
    if (n.type == "if") {
        const auto& pp = n.params["predicate"];
        Predicate* p = registry_.resolvePredicate(pp["type"].as<const char*>());
        if (!p) return {Failed, "unknown predicate: " + String(pp["type"].as<const char*>())};
        bool ok = p->test(pp["params"], ctx);
        const auto& slot = n.children.count(ok ? "then" : "else")
            ? n.children.at(ok ? "then" : "else")
            : std::vector<Node>{};
        return runAll(slot, ctx);
    }
    if (n.type == "repeat") {
        int count = n.params["count"] | 1;
        const auto& body = n.children.count("body") ? n.children.at("body")
                                                    : std::vector<Node>{};
        for (int i = 0; i < count; ++i) {
            auto r = runAll(body, ctx);
            if (r.status == Failed) return r;
        }
        return {Ok, ""};
    }
    Block* b = registry_.resolveBlock(n.type);
    if (!b) return {Failed, "unknown block: " + n.type};
    return b->run(n.params, n.children, ctx, *this);
}
```

### 5.2 Errors

`RunResult.error` propagates to `/api/status`. `StatusLed` reflects current state (running/success/error). No retry/recovery in core — wrap in `RepeatBlock` if needed.

### 5.3 BLE scanning during runs

`TriggerManager` pauses BLE scanning before a sequence starts (if any active trigger has `pausesBleScan() == true`) and resumes after.

## 6. HTTP API

All under `/api/`, JSON in/out:

| Method | Path                            | Purpose                              |
|--------|---------------------------------|--------------------------------------|
| GET    | `/api/schema`                   | Dump all registered schemas.         |
| GET    | `/api/sequences`                | List sequences.                      |
| PUT    | `/api/sequences`                | Replace whole list (atomic save).    |
| POST   | `/api/sequences/:id/run`        | Run sequence now.                    |
| GET    | `/api/triggers`                 | List trigger bindings.               |
| PUT    | `/api/triggers`                 | Replace whole list.                  |
| GET    | `/api/status`                   | `{running, lastRunId, lastError, ts}`|

Existing routes kept (Phase 1) for compatibility:
- `POST /trigger` — implemented as a default `HttpRouteTrigger{path:"/trigger"}` shipped with firmware.
- `POST /tv-key` — kept verbatim until Phase 2 deletion.
- `POST /reset`, `POST /setup`, `GET /settings`, `POST /save` — unchanged.

## 7. Initial adapter set

### Blocks
| type             | category      | fields                                                |
|------------------|---------------|-------------------------------------------------------|
| `wifi-hop`       | Network       | `target` (Enum: tplink, fritzbox)                     |
| `wol`            | Network       | `mac` (MacAddress)                                    |
| `wait`           | Flow          | `ms` (Int)                                            |
| `wait-for-trigger` | Flow        | (none) — pauses sequence until next trigger fires     |
| `repeat`         | Flow          | `count` (Int); child slot: `body`                     |
| `if`             | Flow          | `predicate` (PredicateRef); child slots: `then`, `else` |
| `samsung-keys`   | TV (Samsung)  | `keys` (StringList), `settle_ms` (Int, default 250)   |

### Predicates
| type             | fields                                                |
|------------------|-------------------------------------------------------|
| `host-reachable` | `ip` (String), `port` (Int, optional)                 |
| `on-wifi`        | `target` (Enum: tplink, fritzbox)                     |

**Deliberately not registered**: any predicate testing TV power state. Per `CLAUDE.md` this is not reliably detectable on the Q6 and reintroducing it regresses behavior.

### Triggers
| type           | fields                              | pausesBleScan |
|----------------|-------------------------------------|---------------|
| `ble-mac`      | `mac` (MacAddress)                  | true          |
| `http-route`   | `path` (String, e.g. "/play")       | true          |

## 8. Web editor

Single HTML+JS file, vanilla, served gzipped from PROGMEM. Budget ~15KB gzipped.

### 8.1 Routes
- `GET /` — landing: list sequences + triggers, edit/run buttons.
- `GET /edit?id=X` — sequence editor.
- `GET /triggers` — trigger bindings list.
- `GET /settings` — existing config form (unchanged).

### 8.2 Editor layout
Three columns desktop / stacked mobile:
- **Palette** (left) — block types grouped by category (from schema).
- **Sequence** (center) — ordered list of nodes; drag handle to reorder; indent for nesting under `repeat`/`if`. Click selects.
- **Inspector** (right) — fields rendered from schema for the selected node.

Field rendering: `Int` → `<input type=number>`, `String` → text, `StringList` → textarea, `MacAddress` → masked input with validation, `Enum` → select, `PredicateRef` → nested mini-inspector for the chosen predicate type.

"Test run" button → `POST /api/sequences/:id/run`, polls `/api/status` every 500 ms while running.

### 8.3 Visual style
- Ink `#111` on paper `#fafaf7`; one accent `#c8553d`. Dark mode via `prefers-color-scheme`.
- 1px borders, 4px radii, no drop shadows, no gradients.
- Monospace for type names; sans-serif for labels and copy.
- Tactile, tool-like — closer to Linear's editor or htop than to Notion.

### 8.4 No framework
~200 lines of vanilla JS for render + diff-by-id + event delegation. A framework would 5–10× the JS payload and force a build step into a project that has none.

### 8.5 Build pipeline
`tools/gen_web.py` reads `src/web/editor.html` (and `landing.html`), gzips, emits `editor.html.gz.h` with `PROGMEM` byte arrays. Run via PlatformIO `extra_scripts` pre-build, so editing the HTML rebuilds firmware automatically.

## 9. Migration

**Phase 1 (this spec):** build Core + adapters + editor. Ship a default `sequences.json` baked into firmware that reproduces today's exact behavior — same blocks, same delays, same key list. On first boot with empty NVS, this default is written. Existing users keep working with no manual editing.

Default sequence "wake everything":
1. `wifi-hop{tplink}`
2. `wol{cfg.pcMac}`
3. `wifi-hop{fritzbox}`
4. `wol{cfg.tvMac}`
5. `samsung-keys{["KEY_SOURCE"], settle_ms:900}`
6. `repeat{6}`: `samsung-keys{["KEY_LEFT"], settle_ms:350}`
7. `samsung-keys{["KEY_RIGHT","KEY_RIGHT","KEY_RIGHT","KEY_ENTER"], settle_ms:350}`

Default triggers:
- `ble-mac{cfg.dualsenseMac}` → "wake everything"
- `http-route{/trigger}` → "wake everything"

**Phase 2 (separate effort, after Phase 1 is field-verified):** delete `src/Sequence.cpp/.h`, the inline `MENU_HTML`, the `/trigger`/`/tv-key` handlers in `main.cpp`. Pure deletion.

## 10. Testing

### 10.1 Native unit tests (`pio test -e native`) — Core only
- `Registry` registers and resolves all three kinds.
- `Interpreter` runs flat sequences, nested `repeat`, `if` with both branches, error propagation aborts.
- `SequenceStore` round-trips JSON ↔ tree, marks unknown-block sequences as `broken`.
- `TriggerManager` binds/unbinds without leaking handlers.

Fakes for `Block`/`Predicate`/`Trigger` in `test/fakes/`. Core has no Arduino includes; `platformio.ini` `build_src_filter` is extended to include core but exclude adapters.

### 10.2 On-device smoke tests (manual, documented)
- DualSense → wake-everything (regression vs today).
- `POST /play` (new dynamic route).
- Edit sequence in web UI → save → "Test run".
- `POST /reset` → re-pair → sequences restored from defaults.

### 10.3 No integration tests against real TV/PC
Too flaky and hardware-specific. Adapters are thin enough to verify by eye + smoke.

## 11. File layout

```
src/
  core/
    Block.h
    Predicate.h
    Trigger.h
    Schema.h
    Registry.h Registry.cpp
    Sequence.h          (new data model — replaces today's Sequence.h)
    SequenceStore.h SequenceStore.cpp
    TriggerStore.h  TriggerStore.cpp
    Interpreter.h   Interpreter.cpp
    TriggerManager.h TriggerManager.cpp
    RunCtx.h
  adapters/
    blocks/
      WolBlock.cpp
      WifiHopBlock.cpp
      WaitBlock.cpp
      WaitForTriggerBlock.cpp
      RepeatBlock.cpp
      IfBlock.cpp
      SamsungTizenKeysBlock.cpp
    predicates/
      HostReachablePredicate.cpp
      OnWifiPredicate.cpp
    triggers/
      BleMacTrigger.cpp
      HttpRouteTrigger.cpp
  web/
    editor.html
    landing.html
    triggers.html
    editor.html.gz.h     (generated)
  ApiServer.h ApiServer.cpp
  main.cpp               (slimmed)
  Config.{h,cpp}         (kept)
  NetworkManager.{h,cpp} (kept)
  TvController.{h,cpp}   (kept)
  StatusLed.{h,cpp}      (kept)
  BleScanner.{h,cpp}     (kept)
  Wol.{h,cpp}            (kept)
  ConfigForm.{h,cpp}     (kept)
  SetupServer.{h,cpp}    (kept)
tools/
  gen_web.py
test/
  test_registry/
  test_interpreter/
  test_store/
  fakes/
```

Removed in Phase 2: `src/Sequence.cpp`, `src/Sequence.h` (old).

## 12. Non-goals (explicit YAGNI)

- No multi-sequence concurrency. One at a time; second is queued or 429'd.
- No expressions, variables, or arithmetic in the editor.
- No undo/redo. Save-on-click; user can re-edit.
- No import/export UI. PUT/GET of `/api/sequences` JSON works for power users.
- No auth on the API. Local-network assumption stays.
- No TV-power-state predicate. Per CLAUDE.md, unreliable on Q6.
- No scheduled triggers in v1. Interface allows them later without changes.
- No PATCH semantics. PUT-the-whole-list keeps the API trivial.

## 13. Open questions

None blocking implementation. Items deferred:
- Whether to add a `ScheduleTrigger` (cron) — trivially fits the `Trigger` interface; ship in a follow-up.
- Whether to support multiple "wake everything"-style sequences sharing a sub-sequence — would require sequences-calling-sequences (`CallSequenceBlock`); deferred.
