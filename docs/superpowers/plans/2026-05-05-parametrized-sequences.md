# Parametrized & Reusable Sequences — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make sequences parametrizable (typed params with defaults), composable (one sequence calls another with args), and overridable per-trigger (binding-time defaults + HTTP query overrides).

**Architecture:** Additive changes to the existing sequence-builder core. New core types `ParamDef`, `ParamScope` and a single `resolveParams` helper that the interpreter calls before each block runs. `call-sequence` is a control-flow block hardcoded in `Interpreter` (alongside `if`/`repeat`). `Trigger::bind` gains `defaultArgs` + `SequenceLookup` so HTTP can coerce query strings against the bound sequence's `ParamDef` list. Old stored data deserializes unchanged (empty params/args).

**Tech Stack:** C++17, Arduino-ESP32, ArduinoJson v7, Unity for tests, PlatformIO. Web: vanilla JS gzipped to PROGMEM via `tools/gen_web.py`.

**Spec:** `docs/superpowers/specs/2026-05-05-parametrized-sequences-design.md`

**Branch / worktree:** `feat/parametrized-sequences` at `.worktrees/feat-parametrized-sequences`

**Pre-commit notes:**
- The repo blocks `git commit` to `main`. All commits go to the feature branch.
- The hooks include `conventional-commit`. Use `feat:`, `test:`, `refactor:`, `docs:`, `chore:` prefixes.
- Sign-off / co-author trailers must NOT be added per project policy.

**Run tests with:** `pio test -e native`

**Build firmware with:** `pio run` (don't flash unless asked).

---

## File Structure (locked-in decomposition)

| File | Responsibility | Action |
|---|---|---|
| `src/core/Sequence.h` | data model (`ParamDef`, `Sequence`, `TriggerBinding`) | EDIT |
| `src/core/ParamScope.h` | scope type + helpers | NEW |
| `src/core/ResolveParams.h` `.cpp` | substitution helper (`resolveParams`) | NEW |
| `src/core/Interpreter.h` `.cpp` | scope stack, call-sequence, resolve-before-run | EDIT |
| `src/core/Trigger.h` | extended `bind()` signature + new `FireCallback` | EDIT |
| `src/core/TriggerManager.h` `.cpp` | passes lookup + defaultArgs through | EDIT |
| `src/core/SequenceCodec.h` `.cpp` | encode/decode `params` and `args` | EDIT |
| `src/core/SequenceStore.h` `.cpp` | post-load broken-on-bad-callee-args check | EDIT |
| `src/core/Block.h` | extends `RunCtx` (scopes, callStack, sequenceLookup) | EDIT |
| `src/adapters/triggers/HttpRouteTrigger.h` `.cpp` | query-param coercion + new bind sig | EDIT |
| `src/adapters/triggers/BleMacTrigger.h` `.cpp` | conform to new bind sig (ignores extras) | EDIT |
| `src/main.cpp` | queue carries args; `FireCallback` signature | EDIT |
| `src/ApiServer.cpp` | `/api/run` accepts optional JSON body `{"args":{...}}` | EDIT |
| `src/web/editor.html` | params pane, fx toggle, call-sequence inspector | EDIT |
| `src/web/triggers.html` | binding form renders args from sequence's params | EDIT |
| `test/fakes/FakeTrigger.h` | bind signature update | EDIT |
| `test/test_resolve_params/test_resolve_params.cpp` | resolveParams unit tests | NEW |
| `test/test_call_sequence/test_call_sequence.cpp` | call-sequence + cycles + depth | NEW |
| `test/test_http_query_coercion/test_http_query_coercion.cpp` | string→typed coercion | NEW |
| `test/test_codec/test_codec.cpp` | extend with params/args round-trip | EDIT |
| `test/test_interpreter/test_interpreter.cpp` | parametrized run tests | EDIT |
| `test/test_triggermanager/test_triggermanager.cpp` | new bind signature | EDIT |
| `platformio.ini` | add new test files to `[env:native]` build_src_filter (none needed; new test dirs are auto-discovered) | check only |

`platformio.ini` already has `build_src_filter` for `[env:native]`; new files in `src/core/` are included by `+<*>` and `-<excluded>`. No filter changes needed.

---

## Task 1: Add `ParamDef` and extend data model + codec

**Why first:** every later task needs the new fields. This is a no-runtime-behavior change (just new fields default-empty). Old JSON still round-trips.

**Files:**
- Modify: `src/core/Sequence.h`
- Modify: `src/core/SequenceCodec.cpp`
- Modify: `test/test_codec/test_codec.cpp`

- [ ] **Step 1: Add new test cases to `test_codec.cpp` for params + args (paste before `int main`)**

```cpp
void test_decode_sequence_with_params() {
    const char* json = R"([
      { "id":"p1","name":"P","cooldownMs":1000,
        "params":[
          {"key":"hostMac","type":"mac","label":"Host MAC","default":"AA:BB:CC:DD:EE:FF","required":true},
          {"key":"steps","type":"int","label":"Steps","default":3},
          {"key":"loud","type":"bool","label":"Loud","default":true},
          {"key":"src","type":"enum","label":"Src","default":"hdmi3","enumValues":"hdmi1,hdmi2,hdmi3"}
        ],
        "nodes":[ {"type":"wait","params":{"ms":50},"children":{}} ]
      }
    ])";
    auto seqs = SequenceCodec::decodeList(json);
    TEST_ASSERT_EQUAL(1, (int)seqs.size());
    TEST_ASSERT_EQUAL(4, (int)seqs[0].params.size());
    TEST_ASSERT_EQUAL_STRING("hostMac", seqs[0].params[0].key.c_str());
    TEST_ASSERT_EQUAL((int)FieldType::MacAddress, (int)seqs[0].params[0].type);
    TEST_ASSERT_TRUE(seqs[0].params[0].required);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF",
        seqs[0].params[0].defaultValue.as<const char*>());
    TEST_ASSERT_EQUAL(3, seqs[0].params[1].defaultValue.as<int>());
    TEST_ASSERT_TRUE(seqs[0].params[2].defaultValue.as<bool>());
    TEST_ASSERT_EQUAL_STRING("hdmi1,hdmi2,hdmi3", seqs[0].params[3].enumValues.c_str());
}

void test_encode_sequence_with_params_round_trip() {
    Sequence s;
    s.id = "id"; s.name = "x"; s.cooldownMs = 60000;
    ParamDef p;
    p.key = "n"; p.type = FieldType::Int; p.label = "N";
    p.defaultValue.set(7); p.required = false;
    s.params.push_back(std::move(p));
    Node n; n.type = "wait"; n.params["ms"] = 10;
    s.nodes.push_back(n);
    auto j = SequenceCodec::encodeList({s});
    auto out = SequenceCodec::decodeList(j.c_str());
    TEST_ASSERT_EQUAL(1, (int)out[0].params.size());
    TEST_ASSERT_EQUAL_STRING("n", out[0].params[0].key.c_str());
    TEST_ASSERT_EQUAL(7, out[0].params[0].defaultValue.as<int>());
}

void test_decode_trigger_with_args() {
    const char* json = R"([{
      "id":"t","type":"http-route","params":{"path":"/play"},
      "sequenceId":"abc","enabled":true,
      "args":{"hostMac":"AA:BB:CC:DD:EE:FF","steps":3}
    }])";
    auto trigs = SequenceCodec::decodeTriggers(json);
    TEST_ASSERT_EQUAL(1, (int)trigs.size());
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF",
        trigs[0].args["hostMac"].as<const char*>());
    TEST_ASSERT_EQUAL(3, trigs[0].args["steps"].as<int>());
}

void test_encode_trigger_with_args_round_trip() {
    TriggerBinding b;
    b.id="t"; b.type="http-route"; b.sequenceId="s"; b.enabled=true;
    b.params["path"] = "/x";
    b.args["k"] = "v";
    auto j = SequenceCodec::encodeTriggers({b});
    auto out = SequenceCodec::decodeTriggers(j.c_str());
    TEST_ASSERT_EQUAL_STRING("v", out[0].args["k"].as<const char*>());
}
```

Add these to `RUN_TEST` lines in `main`:
```cpp
RUN_TEST(test_decode_sequence_with_params);
RUN_TEST(test_encode_sequence_with_params_round_trip);
RUN_TEST(test_decode_trigger_with_args);
RUN_TEST(test_encode_trigger_with_args_round_trip);
```

- [ ] **Step 2: Run tests, expect FAIL (compile error: `params` not a member of `Sequence`)**

```
pio test -e native -f test_codec
```

- [ ] **Step 3: Add `ParamDef`, `Sequence::params`, `TriggerBinding::args` to `src/core/Sequence.h`**

```cpp
#pragma once
#include "Schema.h"
#include <ArduinoJson.h>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace seqb {

struct Node {
    std::string type;
    JsonDocument params;
    std::map<std::string, std::vector<Node>> children;
};

struct ParamDef {
    std::string key;
    FieldType   type = FieldType::String;
    std::string label;
    JsonDocument defaultValue;   // empty doc when no default
    std::string enumValues;      // CSV when type == Enum
    bool        required = false;
};

struct Sequence {
    std::string id;
    std::string name;
    uint32_t    cooldownMs = 60000;
    std::vector<ParamDef> params;
    std::vector<Node> nodes;
    bool        broken = false;
    std::string brokenReason;
};

struct TriggerBinding {
    std::string id;
    std::string type;
    JsonDocument params;
    std::string sequenceId;
    JsonDocument args;
    bool enabled = true;
};

}  // namespace seqb
```

`Sequence.h` includes `Schema.h` for `FieldType`. (`Schema.h` already defines the enum and is small/safe to include here.)

- [ ] **Step 4: Update `src/core/SequenceCodec.cpp` encode/decode**

Add a small helper near the top of the anon namespace:

```cpp
FieldType parseFieldType(const char* s) {
    if (!s) return FieldType::String;
    std::string t = s;
    if (t == "bool") return FieldType::Bool;
    if (t == "int") return FieldType::Int;
    if (t == "string") return FieldType::String;
    if (t == "stringlist") return FieldType::StringList;
    if (t == "mac") return FieldType::MacAddress;
    if (t == "enum") return FieldType::Enum;
    if (t == "predicate") return FieldType::PredicateRef;
    return FieldType::String;
}
const char* fieldTypeName(FieldType t) {
    switch (t) {
      case FieldType::Bool: return "bool";
      case FieldType::Int: return "int";
      case FieldType::String: return "string";
      case FieldType::StringList: return "stringlist";
      case FieldType::MacAddress: return "mac";
      case FieldType::Enum: return "enum";
      case FieldType::PredicateRef: return "predicate";
    }
    return "string";
}
```

In `decodeList`, after the existing `s.cooldownMs = ...` line, decode params:

```cpp
for (JsonVariantConst pv : sv["params"].as<JsonArrayConst>()) {
    ParamDef p;
    p.key   = pv["key"].as<const char*>() ? pv["key"].as<const char*>() : "";
    p.type  = parseFieldType(pv["type"].as<const char*>());
    p.label = pv["label"].as<const char*>() ? pv["label"].as<const char*>() : "";
    if (pv["enumValues"].is<const char*>())
        p.enumValues = pv["enumValues"].as<const char*>();
    p.required = pv["required"] | false;
    if (pv["default"].is<JsonVariantConst>())
        p.defaultValue.set(pv["default"]);
    s.params.push_back(std::move(p));
}
```

In `encodeList`, after the existing `o["cooldownMs"] = s.cooldownMs;`, before the nodes array:

```cpp
JsonArray params = o["params"].to<JsonArray>();
for (const auto& p : s.params) {
    JsonObject po = params.add<JsonObject>();
    po["key"]   = p.key;
    po["type"]  = fieldTypeName(p.type);
    po["label"] = p.label;
    if (!p.enumValues.empty()) po["enumValues"] = p.enumValues;
    po["required"] = p.required;
    if (!p.defaultValue.isNull())
        po["default"].set(p.defaultValue.as<JsonVariantConst>());
}
```

In `decodeTriggers`, after `if (tv["params"]...)`, add:

```cpp
if (tv["args"].is<JsonVariantConst>()) copyJson(b.args, tv["args"]);
```

In `encodeTriggers`, after `o["params"].set(...)`, add:

```cpp
if (!t.args.isNull())
    o["args"].set(t.args.as<JsonVariantConst>());
else
    o["args"].to<JsonObject>();
```

- [ ] **Step 5: Run tests — should PASS**

```
pio test -e native -f test_codec
```

Expected: all `test_codec` tests pass.

- [ ] **Step 6: Run full test suite to confirm no regression**

```
pio test -e native
```

- [ ] **Step 7: Commit**

```
git add src/core/Sequence.h src/core/SequenceCodec.cpp test/test_codec/test_codec.cpp
git commit -m "feat(core): add ParamDef and TriggerBinding.args to data model + codec"
```

---

## Task 2: `ParamScope` + `resolveParams` helper

**Why:** Pure helper, easy to TDD without interpreter changes.

**Files:**
- Create: `src/core/ParamScope.h`
- Create: `src/core/ResolveParams.h` `.cpp`
- Create: `test/test_resolve_params/test_resolve_params.cpp`

- [ ] **Step 1: Create `src/core/ParamScope.h`**

```cpp
#pragma once
#include <ArduinoJson.h>
#include <map>
#include <string>

namespace seqb {

// One activation frame's parameter values, keyed by ParamDef.key.
// JsonDocument owns the value (string/int/bool/array etc.).
struct ParamScope {
    std::map<std::string, JsonDocument> values;
};

}  // namespace seqb
```

- [ ] **Step 2: Create `src/core/ResolveParams.h`**

```cpp
#pragma once
#include "ParamScope.h"
#include <ArduinoJson.h>
#include <string>

namespace seqb {

struct ResolveResult {
    bool ok = true;
    std::string error;        // when !ok
    JsonDocument doc;         // when ok
};

// Walk `raw`. For every:
//   - String containing "${name}", interpolate scope[name] (stringified for non-strings).
//   - Object that is exactly {"$param":"name"}, replace with scope[name] (typed).
//   - Array/object: recurse.
// Returns a fresh JsonDocument with substitutions applied, or {ok=false, error=…}.
ResolveResult resolveParams(JsonVariantConst raw, const ParamScope& scope);

}  // namespace seqb
```

- [ ] **Step 3: Create `test/test_resolve_params/test_resolve_params.cpp`**

```cpp
#include <unity.h>
#include "core/ResolveParams.h"

using namespace seqb;

void setUp() {}
void tearDown() {}

static ParamScope makeScope() {
    ParamScope s;
    JsonDocument a; a.set("AA:BB:CC:DD:EE:FF"); s.values["mac"] = std::move(a);
    JsonDocument b; b.set(7);                    s.values["n"] = std::move(b);
    JsonDocument c; c.set(true);                 s.values["loud"] = std::move(c);
    JsonDocument d; d.set("hdmi3");              s.values["src"] = std::move(d);
    return s;
}

void test_string_placeholder_full() {
    JsonDocument in; in.set("${mac}");
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", r.doc.as<const char*>());
}

void test_string_placeholder_interpolation() {
    JsonDocument in; in.set("/api/${src}/light/${n}");
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("/api/hdmi3/light/7", r.doc.as<const char*>());
}

void test_object_param_typed_int() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["$param"] = "n";
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(7, r.doc.as<int>());
}

void test_object_param_typed_bool() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["$param"] = "loud";
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.doc.as<bool>());
}

void test_object_recurse_keeps_keys() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["mac"] = "${mac}";
    o["count"]["$param"] = "n";
    o["literal"] = 42;
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", r.doc["mac"].as<const char*>());
    TEST_ASSERT_EQUAL(7, r.doc["count"].as<int>());
    TEST_ASSERT_EQUAL(42, r.doc["literal"].as<int>());
}

void test_array_recurse() {
    JsonDocument in;
    JsonArray a = in.to<JsonArray>();
    a.add("KEY_${src}");
    a.add(1);
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("KEY_hdmi3", r.doc[0].as<const char*>());
    TEST_ASSERT_EQUAL(1, r.doc[1].as<int>());
}

void test_unknown_param_string() {
    JsonDocument in; in.set("${nope}");
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.error.find("nope") != std::string::npos);
}

void test_unknown_param_object() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["$param"] = "missing";
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.error.find("missing") != std::string::npos);
}

void test_literal_passthrough() {
    JsonDocument in;
    JsonObject o = in.to<JsonObject>();
    o["x"] = 1; o["y"] = "plain";
    auto r = resolveParams(in.as<JsonVariantConst>(), makeScope());
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(1, r.doc["x"].as<int>());
    TEST_ASSERT_EQUAL_STRING("plain", r.doc["y"].as<const char*>());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_string_placeholder_full);
    RUN_TEST(test_string_placeholder_interpolation);
    RUN_TEST(test_object_param_typed_int);
    RUN_TEST(test_object_param_typed_bool);
    RUN_TEST(test_object_recurse_keeps_keys);
    RUN_TEST(test_array_recurse);
    RUN_TEST(test_unknown_param_string);
    RUN_TEST(test_unknown_param_object);
    RUN_TEST(test_literal_passthrough);
    return UNITY_END();
}
```

- [ ] **Step 4: Run tests, expect FAIL (link error: `resolveParams` not defined)**

```
pio test -e native -f test_resolve_params
```

- [ ] **Step 5: Implement `src/core/ResolveParams.cpp`**

```cpp
#include "ResolveParams.h"

namespace seqb {

namespace {

bool isParamObject(JsonVariantConst v, const char*& nameOut) {
    if (!v.is<JsonObjectConst>()) return false;
    auto o = v.as<JsonObjectConst>();
    if (o.size() != 1) return false;
    auto first = *o.begin();
    if (std::string(first.key().c_str()) != "$param") return false;
    if (!first.value().is<const char*>()) return false;
    nameOut = first.value().as<const char*>();
    return true;
}

void appendStringified(std::string& out, JsonVariantConst v) {
    if (v.is<const char*>())     out.append(v.as<const char*>());
    else if (v.is<int>())        out.append(std::to_string(v.as<int>()));
    else if (v.is<bool>())       out.append(v.as<bool>() ? "true" : "false");
    else if (v.is<float>())      out.append(std::to_string(v.as<float>()));
    else                         /* arrays/objects → JSON */
                                 { std::string j; serializeJson(v, j); out.append(j); }
}

bool interpolate(const char* in, const ParamScope& scope,
                 std::string& out, std::string& err) {
    const char* p = in;
    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            const char* end = strchr(p + 2, '}');
            if (!end) { out.push_back(*p++); continue; }
            std::string name(p + 2, end - (p + 2));
            auto it = scope.values.find(name);
            if (it == scope.values.end()) {
                err = "unknown param: " + name;
                return false;
            }
            appendStringified(out, it->second.as<JsonVariantConst>());
            p = end + 1;
        } else {
            out.push_back(*p++);
        }
    }
    return true;
}

bool resolveInto(JsonVariantConst src, JsonVariant dst,
                 const ParamScope& scope, std::string& err) {
    const char* paramName = nullptr;
    if (isParamObject(src, paramName)) {
        auto it = scope.values.find(paramName);
        if (it == scope.values.end()) {
            err = "unknown param: " + std::string(paramName);
            return false;
        }
        dst.set(it->second.as<JsonVariantConst>());
        return true;
    }
    if (src.is<JsonObjectConst>()) {
        JsonObject d = dst.to<JsonObject>();
        for (JsonPairConst kv : src.as<JsonObjectConst>()) {
            if (!resolveInto(kv.value(), d[kv.key()].to<JsonVariant>(), scope, err))
                return false;
        }
        return true;
    }
    if (src.is<JsonArrayConst>()) {
        JsonArray d = dst.to<JsonArray>();
        for (JsonVariantConst el : src.as<JsonArrayConst>()) {
            JsonVariant slot = d.add<JsonVariant>();
            if (!resolveInto(el, slot, scope, err)) return false;
        }
        return true;
    }
    if (src.is<const char*>()) {
        const char* s = src.as<const char*>();
        if (strstr(s, "${")) {
            std::string out;
            if (!interpolate(s, scope, out, err)) return false;
            dst.set(out);
        } else {
            dst.set(s);
        }
        return true;
    }
    dst.set(src);  // numbers, bools, null
    return true;
}

}  // namespace

ResolveResult resolveParams(JsonVariantConst raw, const ParamScope& scope) {
    ResolveResult r;
    JsonVariant root = r.doc.to<JsonVariant>();
    if (!resolveInto(raw, root, scope, r.error)) {
        r.ok = false;
        r.doc.clear();
    }
    return r;
}

}  // namespace seqb
```

- [ ] **Step 6: Run tests — PASS**

```
pio test -e native -f test_resolve_params
```

- [ ] **Step 7: Run full suite, no regressions**

```
pio test -e native
```

- [ ] **Step 8: Commit**

```
git add src/core/ParamScope.h src/core/ResolveParams.h src/core/ResolveParams.cpp test/test_resolve_params/
git commit -m "feat(core): resolveParams helper for \${name} and {\$param} substitution"
```

---

## Task 3: Wire `ParamScope` + args into `Interpreter::runSequence`

**Why:** Once interpreter consumes args + scope, parametrized sequences run end-to-end (without `call-sequence` yet).

**Files:**
- Modify: `src/core/Block.h` (extend `RunCtx`)
- Modify: `src/core/Interpreter.h` `.cpp`
- Modify: `test/test_interpreter/test_interpreter.cpp`

- [ ] **Step 1: Extend `RunCtx` in `src/core/Block.h`**

Add after the existing `scratch` field:

```cpp
#include <vector>           // already brought in transitively, but list explicitly
#include <functional>
class Sequence;             // not needed — full type used below

// (replace existing struct)
struct RunCtx {
    Config* config = nullptr;
    NetworkManager* net = nullptr;
    StatusLed* led = nullptr;
    std::map<std::string, std::string> scratch;

    // NEW — populated by Interpreter::runSequence:
    std::vector<ParamScope> scopeStack;            // back() = current frame
    std::vector<std::string> callStack;            // sequenceIds in flight (cycle guard)
    std::function<const struct Sequence*(const std::string&)> sequenceLookup;  // optional
};
```

Add `#include "ParamScope.h"` to `Block.h`. Forward-declare `Sequence` via `struct Sequence;` (already in `Sequence.h`, which `Block.h` already includes — so no extra fwd-decl needed; remove the `struct Sequence;` in the snippet above if it generates a warning).

- [ ] **Step 2: Extend `Interpreter::runSequence` API in `src/core/Interpreter.h`**

```cpp
#pragma once
#include "Block.h"
#include "Sequence.h"
#include <ArduinoJson.h>

namespace seqb {
class Registry;
class Interpreter {
public:
    explicit Interpreter(Registry& r) : reg_(r) {}

    // New entry: seeds the scope from sequence defaults overlaid with `args`,
    // then runs the body. `args` may be a null/undefined variant -> defaults only.
    RunResult runSequence(const Sequence& s, RunCtx& ctx,
                          JsonVariantConst args = JsonVariantConst());

    RunResult runNode(const Node& n, RunCtx& ctx);
    RunResult runSlot(const std::vector<Node>& nodes, RunCtx& ctx);

private:
    static constexpr int MAX_CALL_DEPTH = 8;
    Registry& reg_;
};
}  // namespace seqb
```

- [ ] **Step 3: Update `runSequence` in `Interpreter.cpp` to seed scope**

Replace the existing `runSequence` body. Add a small helper above it:

```cpp
namespace {
ParamScope buildScope(const Sequence& s, JsonVariantConst args) {
    ParamScope sc;
    for (const auto& p : s.params) {
        if (!p.defaultValue.isNull())
            sc.values[p.key].set(p.defaultValue.as<JsonVariantConst>());
    }
    if (!args.isNull() && args.is<JsonObjectConst>()) {
        for (JsonPairConst kv : args.as<JsonObjectConst>()) {
            sc.values[std::string(kv.key().c_str())].set(kv.value());
        }
    }
    return sc;
}
}  // namespace

RunResult Interpreter::runSequence(const Sequence& s, RunCtx& ctx,
                                   JsonVariantConst args) {
    if ((int)ctx.callStack.size() >= MAX_CALL_DEPTH) {
        return RunResult::failed("call depth exceeded");
    }
    ctx.callStack.push_back(s.id);
    ctx.scopeStack.push_back(buildScope(s, args));
    auto r = runSlot(s.nodes, ctx);
    ctx.scopeStack.pop_back();
    ctx.callStack.pop_back();
    return r;
}
```

- [ ] **Step 4: Apply `resolveParams` to each node's `params` (and `if`'s `predicate.params`) in `runNode`**

In `Interpreter.cpp`, add `#include "ResolveParams.h"`. Modify `runNode` so the block call passes resolved params:

Replace the final two lines:
```cpp
auto* b = reg_.resolveBlock(n.type);
if (!b) return RunResult::failed("unknown block: " + n.type);
return b->run(n.params, n.children, ctx, *this);
```
with:
```cpp
auto* b = reg_.resolveBlock(n.type);
if (!b) return RunResult::failed("unknown block: " + n.type);
const ParamScope& scope = ctx.scopeStack.empty()
    ? *(new (alloca(sizeof(ParamScope))) ParamScope())  // shouldn't happen, defensive
    : ctx.scopeStack.back();
auto resolved = resolveParams(n.params.as<JsonVariantConst>(), scope);
if (!resolved.ok) return RunResult::failed(resolved.error);
return b->run(resolved.doc.as<JsonVariantConst>(), n.children, ctx, *this);
```

(Remove the `alloca` defensive path — it's safer to require a scope and assert. Replace with: `if (ctx.scopeStack.empty()) return RunResult::failed("internal: no scope");`)

For `if`, replace the `auto pp = n.params["predicate"];` block to resolve the predicate's `params` against the scope before testing:

```cpp
if (n.type == "if") {
    if (ctx.scopeStack.empty()) return RunResult::failed("internal: no scope");
    auto pp = n.params["predicate"];
    const char* ptype = pp["type"].as<const char*>();
    if (!ptype) return RunResult::failed("if: missing predicate.type");
    auto* p = reg_.resolvePredicate(ptype);
    if (!p) return RunResult::failed(std::string("unknown predicate: ") + ptype);
    auto resolvedPp = resolveParams(pp["params"].as<JsonVariantConst>(),
                                    ctx.scopeStack.back());
    if (!resolvedPp.ok) return RunResult::failed(resolvedPp.error);
    bool ok = p->test(resolvedPp.doc.as<JsonVariantConst>(), ctx);
    const std::string slot = ok ? "then" : "else";
    auto it = n.children.find(slot);
    if (it == n.children.end()) return RunResult::ok();
    return runSlot(it->second, ctx);
}
```

For `repeat`, also resolve the `count` and `interval_ms` (so `${...}` works there):

```cpp
if (n.type == "repeat") {
    if (ctx.scopeStack.empty()) return RunResult::failed("internal: no scope");
    auto resolvedRp = resolveParams(n.params.as<JsonVariantConst>(),
                                    ctx.scopeStack.back());
    if (!resolvedRp.ok) return RunResult::failed(resolvedRp.error);
    int count = resolvedRp.doc["count"] | 1;
    uint32_t intervalMs = resolvedRp.doc["interval_ms"] | 0;
    auto it = n.children.find("body");
    if (it == n.children.end()) return RunResult::ok();
    for (int i = 0; i < count; ++i) {
        auto r = runSlot(it->second, ctx);
        if (r.status == RunStatus::Failed) return r;
        if (intervalMs && i + 1 < count) interpDelay(intervalMs);
    }
    return RunResult::ok();
}
```

- [ ] **Step 5: Add interpreter tests for parametrized run**

Append to `test/test_interpreter/test_interpreter.cpp` (before `int main`):

```cpp
void test_param_substitution_in_block_params() {
    Registry::reset();
    struct CapturingBlock : public Block {
        BlockSchema sch{"cap","C","F",nullptr,0,nullptr,0};
        JsonDocument seen;
        const BlockSchema& schema() const override { return sch; }
        RunResult run(JsonVariantConst params,
                      const std::map<std::string, std::vector<Node>>&,
                      RunCtx&,
                      Interpreter&) override {
            seen.set(params);
            return RunResult::ok();
        }
    };
    static CapturingBlock cb;
    Registry::instance().registerBlock(&cb);

    Interpreter interp(Registry::instance());
    Sequence s; s.id = "x";
    ParamDef p; p.key = "host"; p.type = FieldType::String;
    p.defaultValue.set("h.example");
    s.params.push_back(std::move(p));
    Node n; n.type = "cap"; n.params["url"] = "http://${host}/";
    s.nodes.push_back(n);

    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_EQUAL_STRING("http://h.example/", cb.seen["url"].as<const char*>());
}

void test_args_override_default() {
    Registry::reset();
    struct CapturingBlock : public Block {
        BlockSchema sch{"cap","C","F",nullptr,0,nullptr,0};
        JsonDocument seen;
        const BlockSchema& schema() const override { return sch; }
        RunResult run(JsonVariantConst params,
                      const std::map<std::string, std::vector<Node>>&,
                      RunCtx&,
                      Interpreter&) override { seen.set(params); return RunResult::ok(); }
    };
    static CapturingBlock cb;
    Registry::instance().registerBlock(&cb);

    Sequence s;
    ParamDef p; p.key="host"; p.type=FieldType::String; p.defaultValue.set("default");
    s.params.push_back(std::move(p));
    Node n; n.type="cap"; n.params["v"]="${host}"; s.nodes.push_back(n);

    Interpreter interp(Registry::instance());
    RunCtx ctx;
    JsonDocument args; args["host"] = "override";
    auto r = interp.runSequence(s, ctx, args.as<JsonVariantConst>());
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_EQUAL_STRING("override", cb.seen["v"].as<const char*>());
}

void test_unknown_param_fails_run() {
    Registry::reset();
    static FakeBlock cap("cap","C","F");
    Registry::instance().registerBlock(&cap);
    Sequence s;
    Node n; n.type="cap"; n.params["x"]="${nope}"; s.nodes.push_back(n);
    Interpreter interp(Registry::instance());
    RunCtx ctx;
    auto r = interp.runSequence(s, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_TRUE(r.error.find("nope") != std::string::npos);
}
```

Wire into `RUN_TEST` in `main`:
```cpp
RUN_TEST(test_param_substitution_in_block_params);
RUN_TEST(test_args_override_default);
RUN_TEST(test_unknown_param_fails_run);
```

- [ ] **Step 6: Run interpreter tests**

```
pio test -e native -f test_interpreter
```

Expected: PASS.

- [ ] **Step 7: Run full suite**

```
pio test -e native
```

- [ ] **Step 8: Commit**

```
git add src/core/Block.h src/core/Interpreter.h src/core/Interpreter.cpp test/test_interpreter/test_interpreter.cpp
git commit -m "feat(core): seed scope from defaults+args; resolve node params before run"
```

---

## Task 4: `call-sequence` block + cycle/depth guards

**Why:** Composability. Builds on Task 3's scope/call stack.

**Files:**
- Modify: `src/core/Interpreter.cpp` (add `call-sequence` branch)
- Create: `test/test_call_sequence/test_call_sequence.cpp`

- [ ] **Step 1: Create the test file `test/test_call_sequence/test_call_sequence.cpp`**

```cpp
#include <unity.h>
#include "core/Interpreter.h"
#include "core/Registry.h"
#include "fakes/FakeBlock.h"

using namespace seqb;

static FakeBlock* leaf_;

void setUp() {
    Registry::reset();
    leaf_ = new FakeBlock("leaf","L","F");
    Registry::instance().registerBlock(leaf_);
}
void tearDown() { delete leaf_; }

static Sequence makeCallee(const char* id) {
    Sequence s; s.id = id; s.name = id;
    Node n; n.type = "leaf"; s.nodes.push_back(n);
    return s;
}

static const Sequence* lookup(const std::vector<const Sequence*>& list,
                              const std::string& id) {
    for (auto* s : list) if (s->id == id) return s;
    return nullptr;
}

void test_call_sequence_runs_callee() {
    auto callee = makeCallee("CB");
    std::vector<const Sequence*> all{&callee};

    Sequence caller; caller.id="CA";
    Node call; call.type = "call-sequence";
    call.params["sequenceId"] = "CB";
    caller.nodes.push_back(call);

    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id){ return lookup(all, id); };
    auto r = interp.runSequence(caller, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_TRUE(leaf_->ran);
}

void test_call_sequence_args_resolved_against_caller_scope() {
    Registry::reset();
    struct CapturingBlock : public Block {
        BlockSchema sch{"cap","C","F",nullptr,0,nullptr,0};
        JsonDocument seen;
        const BlockSchema& schema() const override { return sch; }
        RunResult run(JsonVariantConst params,
                      const std::map<std::string, std::vector<Node>>&,
                      RunCtx&,
                      Interpreter&) override {
            seen.set(params);
            return RunResult::ok();
        }
    };
    static CapturingBlock cb;
    Registry::instance().registerBlock(&cb);

    Sequence callee; callee.id="CB";
    ParamDef p; p.key="x"; p.type=FieldType::String;
    callee.params.push_back(std::move(p));
    Node leaf; leaf.type="cap"; leaf.params["v"]="${x}";
    callee.nodes.push_back(leaf);

    Sequence caller; caller.id="CA";
    ParamDef cp; cp.key="who"; cp.type=FieldType::String; cp.defaultValue.set("alice");
    caller.params.push_back(std::move(cp));
    Node call; call.type="call-sequence";
    call.params["sequenceId"] = "CB";
    call.params["args"]["x"] = "${who}";
    caller.nodes.push_back(call);

    std::vector<const Sequence*> all{&callee};
    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id){ return lookup(all, id); };
    auto r = interp.runSequence(caller, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Ok, (int)r.status);
    TEST_ASSERT_EQUAL_STRING("alice", cb.seen["v"].as<const char*>());
}

void test_call_sequence_cycle_detected() {
    Sequence a; a.id="A";
    Node ca; ca.type="call-sequence"; ca.params["sequenceId"]="B"; a.nodes.push_back(ca);
    Sequence b; b.id="B";
    Node cb; cb.type="call-sequence"; cb.params["sequenceId"]="A"; b.nodes.push_back(cb);
    std::vector<const Sequence*> all{&a, &b};

    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id){ return lookup(all, id); };
    auto r = interp.runSequence(a, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_TRUE(r.error.find("cycle") != std::string::npos);
}

void test_call_sequence_unknown_id_fails() {
    Sequence a; a.id="A";
    Node ca; ca.type="call-sequence"; ca.params["sequenceId"]="GHOST"; a.nodes.push_back(ca);
    std::vector<const Sequence*> all{&a};
    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id){ return lookup(all, id); };
    auto r = interp.runSequence(a, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_TRUE(r.error.find("GHOST") != std::string::npos);
}

void test_call_sequence_broken_callee_fails() {
    Sequence callee; callee.id="C"; callee.broken = true; callee.brokenReason = "bad";
    Sequence caller; caller.id="A";
    Node call; call.type="call-sequence"; call.params["sequenceId"]="C";
    caller.nodes.push_back(call);
    std::vector<const Sequence*> all{&callee, &caller};
    Interpreter interp(Registry::instance());
    RunCtx ctx;
    ctx.sequenceLookup = [&](const std::string& id){ return lookup(all, id); };
    auto r = interp.runSequence(caller, ctx);
    TEST_ASSERT_EQUAL((int)RunStatus::Failed, (int)r.status);
    TEST_ASSERT_TRUE(r.error.find("broken") != std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_call_sequence_runs_callee);
    RUN_TEST(test_call_sequence_args_resolved_against_caller_scope);
    RUN_TEST(test_call_sequence_cycle_detected);
    RUN_TEST(test_call_sequence_unknown_id_fails);
    RUN_TEST(test_call_sequence_broken_callee_fails);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests, expect FAIL (call-sequence unknown to interpreter)**

```
pio test -e native -f test_call_sequence
```

- [ ] **Step 3: Add `call-sequence` branch to `Interpreter::runNode`**

In `src/core/Interpreter.cpp`, before the registry-resolveBlock fallback, add:

```cpp
if (n.type == "call-sequence") {
    if (ctx.scopeStack.empty()) return RunResult::failed("internal: no scope");
    if (!ctx.sequenceLookup) return RunResult::failed("call-sequence: no lookup");

    // Resolve args against caller's current scope.
    auto resolved = resolveParams(n.params.as<JsonVariantConst>(),
                                  ctx.scopeStack.back());
    if (!resolved.ok) return RunResult::failed(resolved.error);
    const char* sid = resolved.doc["sequenceId"].as<const char*>();
    if (!sid || !*sid) return RunResult::failed("call-sequence: missing sequenceId");

    // Cycle detection.
    for (const auto& on : ctx.callStack) {
        if (on == sid) return RunResult::failed(std::string("cycle: ") + sid);
    }

    const Sequence* callee = ctx.sequenceLookup(sid);
    if (!callee) return RunResult::failed(std::string("unknown sequence: ") + sid);
    if (callee->broken) return RunResult::failed(
        std::string("callee broken: ") + sid + " (" + callee->brokenReason + ")");

    JsonVariantConst args = resolved.doc["args"].as<JsonVariantConst>();
    return runSequence(*callee, ctx, args);
}
```

- [ ] **Step 4: Run tests — PASS**

```
pio test -e native -f test_call_sequence
```

- [ ] **Step 5: Run full suite**

```
pio test -e native
```

- [ ] **Step 6: Commit**

```
git add src/core/Interpreter.cpp test/test_call_sequence/
git commit -m "feat(core): call-sequence block with cycle and depth guards"
```

---

## Task 5: Update `Trigger::bind` signature + propagate args through `TriggerManager`

**Why:** Required so HTTP can know the bound sequence's `ParamDef` list. Touches every Trigger implementation.

**Files:**
- Modify: `src/core/Trigger.h`
- Modify: `src/core/TriggerManager.h` `.cpp`
- Modify: `src/adapters/triggers/BleMacTrigger.h` `.cpp`
- Modify: `src/adapters/triggers/HttpRouteTrigger.h` `.cpp` (signature only — coercion in Task 6)
- Modify: `test/fakes/FakeTrigger.h`
- Modify: `test/test_triggermanager/test_triggermanager.cpp`

- [ ] **Step 1: Update `src/core/Trigger.h`**

```cpp
#pragma once
#include "Schema.h"
#include "Sequence.h"
#include <ArduinoJson.h>
#include <functional>
#include <string>

namespace seqb {

class Trigger {
public:
    using FireCallback = std::function<void(const std::string& sequenceId,
                                            JsonVariantConst args)>;
    using SequenceLookup = std::function<const Sequence*(const std::string&)>;

    virtual ~Trigger() = default;
    virtual const TriggerSchema& schema() const = 0;
    virtual void bind(const std::string& bindingId,
                      JsonVariantConst params,
                      const std::string& sequenceId,
                      JsonVariantConst defaultArgs,
                      SequenceLookup lookup,
                      FireCallback onFire) = 0;
    virtual void unbind(const std::string& bindingId) = 0;
};

}  // namespace seqb
```

- [ ] **Step 2: Update `src/core/TriggerManager.h` and `.cpp`**

Header:

```cpp
#pragma once
#include "Trigger.h"
#include "Sequence.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace seqb {
class Registry;
class SequenceStore;

class TriggerManager {
public:
    using FireCallback = std::function<void(const std::string& sequenceId,
                                            JsonVariantConst args)>;
    using SequenceLookup = std::function<const Sequence*(const std::string&)>;

    TriggerManager(Registry& r, FireCallback cb, SequenceLookup lookup);
    void applyBindings(const std::vector<TriggerBinding>& bs);
    bool anyPausesBleScan() const;

private:
    Registry& reg_;
    FireCallback cb_;
    SequenceLookup lookup_;
    std::map<std::string, Trigger*> active_;
    bool anyPauses_ = false;
};
}  // namespace seqb
```

Cpp:

```cpp
#include "TriggerManager.h"
#include "Registry.h"

namespace seqb {

TriggerManager::TriggerManager(Registry& r, FireCallback cb, SequenceLookup lookup)
    : reg_(r), cb_(std::move(cb)), lookup_(std::move(lookup)) {}

void TriggerManager::applyBindings(const std::vector<TriggerBinding>& bs) {
    for (auto& kv : active_) kv.second->unbind(kv.first);
    active_.clear();
    anyPauses_ = false;
    for (auto& b : bs) {
        if (!b.enabled) continue;
        auto* t = reg_.resolveTrigger(b.type);
        if (!t) continue;
        t->bind(b.id, b.params.as<JsonVariantConst>(), b.sequenceId,
                b.args.as<JsonVariantConst>(), lookup_, cb_);
        active_[b.id] = t;
        if (t->schema().pausesBleScan) anyPauses_ = true;
    }
}

bool TriggerManager::anyPausesBleScan() const { return anyPauses_; }

}  // namespace seqb
```

- [ ] **Step 3: Update `BleMacTrigger.{h,cpp}` to conform**

Header — change the override signature to match. Cpp — accept the new params, store the cooldown, and store `defaultArgs` (owned `JsonDocument`) per binding so it can be passed to `cb` on fire:

In `BleMacTrigger.cpp`, update `Active`:

```cpp
struct Active {
    std::string mac;
    std::string sequenceId;
    Trigger::FireCallback cb;
    uint32_t cooldownMs;
    uint32_t lastFireMs = 0;
    JsonDocument args;   // owned snapshot of defaultArgs
};
```

Update `bind`:

```cpp
void BleMacTrigger::bind(const std::string& id,
                         JsonVariantConst params,
                         const std::string& seq,
                         JsonVariantConst defaultArgs,
                         SequenceLookup /*lookup*/,
                         FireCallback cb) {
    const char* mac = params["mac"].as<const char*>();
    if (!mac) return;
    Active a;
    a.mac = toLower(mac);
    a.sequenceId = seq;
    a.cb = cb;
    a.cooldownMs = params["cooldown_ms"] | 60000U;
    if (!defaultArgs.isNull()) a.args.set(defaultArgs);
    g_active[id] = std::move(a);
}
```

Update `onHit`:

```cpp
a.cb(a.sequenceId, a.args.as<JsonVariantConst>());
```

Header `BleMacTrigger.h` `bind` declaration:

```cpp
void bind(const std::string& bindingId,
          JsonVariantConst params,
          const std::string& sequenceId,
          JsonVariantConst defaultArgs,
          SequenceLookup lookup,
          FireCallback onFire) override;
```

- [ ] **Step 4: Update `HttpRouteTrigger.{h,cpp}` signature only (coercion in Task 6)**

Header `bind` declaration mirrors BleMacTrigger's. Cpp: accept the new params, store them in `Active`, but for now keep the existing handler that ignores query params and fires with `JsonVariantConst()` for args (we will replace in Task 6). Use the existing `defaultArgs` as the args passed to `cb`:

```cpp
struct Active {
    std::string path;
    std::string sequenceId;
    Trigger::FireCallback cb;
    Trigger::SequenceLookup lookup;
    JsonDocument defaultArgs;
};

void HttpRouteTrigger::bind(const std::string& id,
                            JsonVariantConst params,
                            const std::string& seq,
                            JsonVariantConst defaultArgs,
                            SequenceLookup lookup,
                            FireCallback cb) {
    const char* path = params["path"].as<const char*>();
    if (!path || !path[0]) return;
    Active a;
    a.path = path; a.sequenceId = seq; a.cb = cb; a.lookup = lookup;
    if (!defaultArgs.isNull()) a.defaultArgs.set(defaultArgs);
    g_active[id] = std::move(a);
    registerHandler(g_active[id]);
}

// internal helper now takes the stored Active
static void registerHandler(Active& a) {
    if (!g_server) return;
    g_server->on(a.path.c_str(), HTTP_POST, [&a](AsyncWebServerRequest* req) {
        a.cb(a.sequenceId, a.defaultArgs.as<JsonVariantConst>());
        req->send(200, "text/plain", "queued");
    });
}
```

NOTE: the `[&a]` capture is safe because `g_active` is a long-lived static map and entries are not erased while a handler exists (rebind reuses the slot). If you prefer, capture by id and look up inside the handler.

- [ ] **Step 5: Update `test/fakes/FakeTrigger.h` to match new signature**

```cpp
#pragma once
#include "core/Trigger.h"
namespace seqb {
class FakeTrigger : public Trigger {
public:
    explicit FakeTrigger(const char* t) { sch_ = {t,t,nullptr,0,true}; }
    const TriggerSchema& schema() const override { return sch_; }
    void bind(const std::string& id,
              JsonVariantConst,
              const std::string& seq,
              JsonVariantConst,
              SequenceLookup,
              FireCallback) override { binds.push_back({id,seq}); }
    void unbind(const std::string& id) override { unbinds.push_back(id); }

    struct Bind { std::string id, seq; };
    std::vector<Bind> binds;
    std::vector<std::string> unbinds;
private:
    TriggerSchema sch_;
};
}  // namespace seqb
```

- [ ] **Step 6: Update `test_triggermanager.cpp` ctor call**

Replace any `TriggerManager(reg, cb)` with `TriggerManager(reg, cb, lookup)`. Pass a no-op lookup `[](const std::string&){ return nullptr; }` where the test doesn't care.

Where the test asserts `cb` invocation, update `cb` to take `(const std::string&, JsonVariantConst)`:

```cpp
seqb::TriggerManager::FireCallback cb = [&](const std::string& sid, JsonVariantConst){
    fired.push_back(sid);
};
seqb::TriggerManager::SequenceLookup lookup = [](const std::string&){ return nullptr; };
TriggerManager tm(reg, cb, lookup);
```

- [ ] **Step 7: Update `src/main.cpp`**

Change the `TriggerManager` construction to provide a lookup over `seqStore`:

```cpp
trigMgr = new seqb::TriggerManager(
    seqb::Registry::instance(),
    [](const std::string& sid, JsonVariantConst args) { enqueueRun(sid, args); },
    [](const std::string& id) -> const seqb::Sequence* { return seqStore->findById(id); });
```

(`enqueueRun` will be updated to take args in Task 7. Adjust the lambda body accordingly when you reach that task; for now, ignore the variant.)

For this task only, write a temporary forwarding: the lambda body is `enqueueRun(sid)` — single-arg. We'll update in Task 7.

- [ ] **Step 8: Build firmware to confirm everything links**

```
pio run
```

Expected: clean build (no upload).

- [ ] **Step 9: Run tests**

```
pio test -e native
```

Expected: all green.

- [ ] **Step 10: Commit**

```
git add src/core/Trigger.h src/core/TriggerManager.h src/core/TriggerManager.cpp \
        src/adapters/triggers/BleMacTrigger.h src/adapters/triggers/BleMacTrigger.cpp \
        src/adapters/triggers/HttpRouteTrigger.h src/adapters/triggers/HttpRouteTrigger.cpp \
        test/fakes/FakeTrigger.h test/test_triggermanager/test_triggermanager.cpp \
        src/main.cpp
git commit -m "refactor(triggers): extend bind() with defaultArgs and SequenceLookup"
```

---

## Task 6: HTTP query-param coercion

**Files:**
- Create: `src/adapters/triggers/QueryCoerce.h` `.cpp` (helper, native-testable; no Arduino deps)
- Modify: `src/adapters/triggers/HttpRouteTrigger.cpp` (use the helper inside the handler)
- Create: `test/test_http_query_coercion/test_http_query_coercion.cpp`
- Modify: `platformio.ini` `[env:native]` `build_src_filter` — exclude `HttpRouteTrigger.cpp` already, but include `QueryCoerce.cpp` (it has no Arduino deps).

The helper lives under `src/adapters/triggers/` for cohesion with `HttpRouteTrigger`, but compiles natively (no `<Arduino.h>`).

- [ ] **Step 1: Define `QueryCoerce.h`**

```cpp
#pragma once
#include "core/Sequence.h"
#include <ArduinoJson.h>
#include <map>
#include <string>

namespace seqb {

struct CoerceResult {
    bool ok = true;
    std::string error;
    JsonDocument doc;   // resolved args object
};

// Apply query overrides on top of `defaultArgs`, coerced per `params` types.
// Unknown keys are ignored. Missing required without default -> NOT enforced
// here (interpreter handles that at run time).
CoerceResult coerceQueryArgs(JsonVariantConst defaultArgs,
                             const std::vector<ParamDef>& params,
                             const std::map<std::string, std::string>& query);

}  // namespace seqb
```

- [ ] **Step 2: Write `test/test_http_query_coercion/test_http_query_coercion.cpp`**

```cpp
#include <unity.h>
#include "adapters/triggers/QueryCoerce.h"

using namespace seqb;

void setUp() {} void tearDown() {}

static std::vector<ParamDef> mkParams() {
    std::vector<ParamDef> ps;
    auto add = [&](const char* k, FieldType t, const char* def, const char* enums=nullptr) {
        ParamDef p; p.key=k; p.type=t;
        if (def) p.defaultValue.set(def);
        if (enums) p.enumValues = enums;
        ps.push_back(std::move(p));
    };
    add("host", FieldType::String, "h.local");
    add("n",    FieldType::Int,    nullptr); ps.back().defaultValue.set(3);
    add("loud", FieldType::Bool,   nullptr); ps.back().defaultValue.set(false);
    add("src",  FieldType::Enum,   "hdmi1", "hdmi1,hdmi2,hdmi3");
    add("keys", FieldType::StringList, nullptr);
    return ps;
}

void test_defaults_when_no_query() {
    auto ps = mkParams();
    auto r = coerceQueryArgs(JsonVariantConst(), ps, {});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("h.local", r.doc["host"].as<const char*>());
    TEST_ASSERT_EQUAL(3, r.doc["n"].as<int>());
    TEST_ASSERT_FALSE(r.doc["loud"].as<bool>());
}

void test_query_override_string() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"host","other"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("other", r.doc["host"].as<const char*>());
}

void test_query_override_int_ok() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"n","42"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(42, r.doc["n"].as<int>());
}

void test_query_override_int_fail() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"n","oops"}});
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.error.find("'n'") != std::string::npos);
}

void test_query_override_bool() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"loud","yes"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.doc["loud"].as<bool>());
    auto r2 = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"loud","off"}});
    TEST_ASSERT_TRUE(r2.ok);
    TEST_ASSERT_FALSE(r2.doc["loud"].as<bool>());
    auto r3 = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"loud","maybe"}});
    TEST_ASSERT_FALSE(r3.ok);
}

void test_query_override_enum() {
    auto ok = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"src","hdmi3"}});
    TEST_ASSERT_TRUE(ok.ok);
    TEST_ASSERT_EQUAL_STRING("hdmi3", ok.doc["src"].as<const char*>());
    auto bad = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"src","hdmi9"}});
    TEST_ASSERT_FALSE(bad.ok);
}

void test_query_override_stringlist() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"keys","A,B,C"}});
    TEST_ASSERT_TRUE(r.ok);
    auto a = r.doc["keys"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL(3, (int)a.size());
    TEST_ASSERT_EQUAL_STRING("A", a[0].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("C", a[2].as<const char*>());
}

void test_unknown_query_key_ignored() {
    auto r = coerceQueryArgs(JsonVariantConst(), mkParams(), {{"unknown","x"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.doc["unknown"].is<JsonVariantConst>());
}

void test_default_args_overlaid() {
    JsonDocument def;
    def["host"] = "fromBinding";
    def["n"]    = 99;
    auto r = coerceQueryArgs(def.as<JsonVariantConst>(), mkParams(), {{"n","5"}});
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_STRING("fromBinding", r.doc["host"].as<const char*>());
    TEST_ASSERT_EQUAL(5, r.doc["n"].as<int>());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_when_no_query);
    RUN_TEST(test_query_override_string);
    RUN_TEST(test_query_override_int_ok);
    RUN_TEST(test_query_override_int_fail);
    RUN_TEST(test_query_override_bool);
    RUN_TEST(test_query_override_enum);
    RUN_TEST(test_query_override_stringlist);
    RUN_TEST(test_unknown_query_key_ignored);
    RUN_TEST(test_default_args_overlaid);
    return UNITY_END();
}
```

- [ ] **Step 3: Run, expect FAIL (link error)**

```
pio test -e native -f test_http_query_coercion
```

- [ ] **Step 4: Implement `QueryCoerce.cpp`**

```cpp
#include "QueryCoerce.h"
#include <cstdlib>
#include <cstring>

namespace seqb {

namespace {
std::string lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c += 32;
    return out;
}

bool parseIntStrict(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;
    out = (int)v;
    return true;
}

bool enumContains(const std::string& csv, const std::string& v) {
    size_t i = 0;
    while (i <= csv.size()) {
        size_t j = csv.find(',', i);
        if (j == std::string::npos) j = csv.size();
        if (csv.compare(i, j - i, v) == 0) return true;
        i = j + 1;
    }
    return false;
}
}  // namespace

CoerceResult coerceQueryArgs(JsonVariantConst defaultArgs,
                             const std::vector<ParamDef>& params,
                             const std::map<std::string, std::string>& query) {
    CoerceResult r;
    JsonObject obj = r.doc.to<JsonObject>();

    if (!defaultArgs.isNull() && defaultArgs.is<JsonObjectConst>()) {
        for (JsonPairConst kv : defaultArgs.as<JsonObjectConst>()) {
            obj[kv.key()].set(kv.value());
        }
    }

    for (const auto& pd : params) {
        auto it = query.find(pd.key);
        if (it == query.end()) continue;
        const std::string& v = it->second;

        switch (pd.type) {
          case FieldType::String:
          case FieldType::MacAddress:
            obj[pd.key] = v;
            break;
          case FieldType::StringList: {
            JsonArray a = obj[pd.key].to<JsonArray>();
            size_t i = 0;
            while (i <= v.size()) {
                size_t j = v.find(',', i);
                if (j == std::string::npos) j = v.size();
                a.add(v.substr(i, j - i));
                i = j + 1;
            }
            break;
          }
          case FieldType::Int: {
            int n;
            if (!parseIntStrict(v, n)) {
                r.ok = false;
                r.error = "invalid param '" + pd.key + "': not int";
                return r;
            }
            obj[pd.key] = n;
            break;
          }
          case FieldType::Bool: {
            std::string l = lower(v);
            if (l=="1"||l=="true"||l=="yes"||l=="on")  obj[pd.key] = true;
            else if (l=="0"||l=="false"||l=="no"||l=="off") obj[pd.key] = false;
            else {
                r.ok = false;
                r.error = "invalid param '" + pd.key + "': not bool";
                return r;
            }
            break;
          }
          case FieldType::Enum: {
            if (!enumContains(pd.enumValues, v)) {
                r.ok = false;
                r.error = "invalid param '" + pd.key + "': not in enum";
                return r;
            }
            obj[pd.key] = v;
            break;
          }
          case FieldType::PredicateRef:
            // Not overridable via query — ignore silently.
            break;
        }
    }
    return r;
}

}  // namespace seqb
```

- [ ] **Step 5: Run native tests — PASS**

```
pio test -e native -f test_http_query_coercion
```

- [ ] **Step 6: Hook the helper into `HttpRouteTrigger.cpp`**

Replace the handler registration so it pulls the bound sequence's `ParamDef`s via `lookup`, builds the query map from the request, calls `coerceQueryArgs`, and invokes `cb` with the resolved doc — or returns 400 on error.

```cpp
#include "QueryCoerce.h"

static void registerHandler(Active& a) {
    if (!g_server) return;
    g_server->on(a.path.c_str(), HTTP_POST, [&a](AsyncWebServerRequest* req) {
        const Sequence* seq = a.lookup ? a.lookup(a.sequenceId) : nullptr;
        std::vector<ParamDef> empty;
        const auto& pdefs = seq ? seq->params : empty;

        std::map<std::string, std::string> q;
        for (size_t i = 0; i < req->params(); ++i) {
            auto* p = req->getParam(i);
            q.emplace(p->name().c_str(), p->value().c_str());
        }

        auto cr = coerceQueryArgs(a.defaultArgs.as<JsonVariantConst>(), pdefs, q);
        if (!cr.ok) {
            std::string body = std::string("{\"error\":\"") + cr.error + "\"}";
            req->send(400, "application/json", body.c_str());
            return;
        }
        a.cb(a.sequenceId, cr.doc.as<JsonVariantConst>());
        req->send(200, "text/plain", "queued");
    });
}
```

- [ ] **Step 7: Build firmware**

```
pio run
```

Expected: clean build.

- [ ] **Step 8: Full test suite**

```
pio test -e native
```

- [ ] **Step 9: Commit**

```
git add src/adapters/triggers/QueryCoerce.h src/adapters/triggers/QueryCoerce.cpp \
        src/adapters/triggers/HttpRouteTrigger.cpp \
        test/test_http_query_coercion/
git commit -m "feat(triggers): coerce HTTP query params to typed sequence args"
```

---

## Task 7: `main.cpp` queue carries args + `/api/run` accepts JSON body

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/ApiServer.cpp`

- [ ] **Step 1: Update queue + `enqueueRun` in `main.cpp`**

Replace the queue variables and `enqueueRun`:

```cpp
volatile bool sequence_pending_ = false;
String pending_seq_id_;
JsonDocument pending_args_;   // NEW — owned

void enqueueRun(const std::string& id, JsonVariantConst args = JsonVariantConst()) {
    if (sequence_pending_) return;
    pending_seq_id_ = id.c_str();
    pending_args_.clear();
    if (!args.isNull()) pending_args_.set(args);
    sequence_pending_ = true;
}
```

In `loop()` where it runs the pending sequence, replace `interp->runSequence(*seq, ctx)` with:

```cpp
auto r = interp->runSequence(*seq, ctx, pending_args_.as<JsonVariantConst>());
```

After the run completes, clear `pending_args_`:
```cpp
pending_args_.clear();
```

- [ ] **Step 2: Update lambdas/hooks to forward args**

```cpp
trigMgr = new seqb::TriggerManager(
    seqb::Registry::instance(),
    [](const std::string& sid, JsonVariantConst args) { enqueueRun(sid, args); },
    [](const std::string& id) -> const seqb::Sequence* { return seqStore->findById(id); });

static seqb::ApiHooks hooks{
    [](const std::string& sid, JsonVariantConst args) { enqueueRun(sid, args); },
    statusJson,
};
```

- [ ] **Step 3: Update `ApiHooks::enqueueRun` signature in `ApiServer.h`**

```cpp
struct ApiHooks {
    std::function<void(const std::string& sequenceId, JsonVariantConst args)> enqueueRun;
    std::function<std::string()> currentStatusJson;
};
```

- [ ] **Step 4: Update `/api/run` in `ApiServer.cpp` to accept body args**

Replace the existing `srv_.on("/api/run", ...)` block. We need a body-aware handler — use `AsyncCallbackJsonWebHandler` like the PUT routes:

```cpp
// /api/run?id=ABC  with optional JSON body { "args": {...} }
auto* runH = new AsyncCallbackJsonWebHandler(
    "/api/run", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        if (!req->hasParam("id")) {
            sendJson(req, R"({"error":"missing id"})", 400);
            return;
        }
        std::string id = req->getParam("id")->value().c_str();
        auto* s = seqs_.findById(id);
        if (!s) { sendJson(req, R"({"error":"unknown sequence"})", 404); return; }
        if (s->broken) { sendJson(req, R"({"error":"sequence is broken"})", 422); return; }
        JsonVariantConst args = json["args"].as<JsonVariantConst>();
        hooks_.enqueueRun(s->id, args);
        sendJson(req, R"({"status":"queued"})");
    });
runH->setMethod(HTTP_POST);
srv_.addHandler(runH);
```

For body-less POSTs (no Content-Type: application/json), the handler still triggers but `json` is null; `args` will be null too — fine, defaults are used.

- [ ] **Step 5: Build firmware**

```
pio run
```

Expected: clean build.

- [ ] **Step 6: Full native test suite**

```
pio test -e native
```

- [ ] **Step 7: Commit**

```
git add src/main.cpp src/ApiServer.h src/ApiServer.cpp
git commit -m "feat(api): /api/run takes optional JSON body args; queue carries args"
```

---

## Task 8: `SequenceStore` validates `call-sequence` args against callee `params`

**Why:** Surface dangling `call-sequence` args at load time (so editor shows `broken=true`).

**Files:**
- Modify: `src/core/SequenceStore.cpp`
- Modify: `test/test_store/test_store.cpp`

- [ ] **Step 1: Add tests in `test_store.cpp`**

```cpp
void test_caller_marked_broken_when_callee_arg_missing() {
    Registry::reset();
    static FakeBlock leaf("leaf","L","F");
    Registry::instance().registerBlock(&leaf);

    FakePersistence p;
    SequenceStore store(p);
    Sequence callee; callee.id="CB"; callee.name="cb";
    ParamDef pd; pd.key="needed"; pd.type=FieldType::String; pd.required=true;
    callee.params.push_back(std::move(pd));
    Node n; n.type="leaf"; callee.nodes.push_back(n);

    Sequence caller; caller.id="CA"; caller.name="ca";
    Node call; call.type="call-sequence";
    call.params["sequenceId"] = "CB";
    // NOTE: no "args" key -> required arg "needed" is missing
    caller.nodes.push_back(call);

    store.replaceAll({callee, caller});
    auto* loaded = store.findById("CA");
    TEST_ASSERT_NOT_NULL(loaded);
    TEST_ASSERT_TRUE(loaded->broken);
    TEST_ASSERT_TRUE(loaded->brokenReason.find("needed") != std::string::npos);
}
```

Add to `RUN_TEST` list.

- [ ] **Step 2: Run, expect FAIL**

```
pio test -e native -f test_store
```

- [ ] **Step 3: Implement check in `SequenceStore`**

In `SequenceStore.cpp`, after the existing decode-and-mark-broken logic in `replaceAll` and `load`, walk all sequences and for every `call-sequence` node check args against the callee's required params. Add a helper `validateCallSequenceArgs` and invoke it after the list is in place:

```cpp
namespace {
void walkNodes(std::vector<Node>& nodes,
               const std::function<void(Node&)>& visit) {
    for (auto& n : nodes) {
        visit(n);
        for (auto& kv : n.children) walkNodes(kv.second, visit);
    }
}

void validateCallSequenceArgs(std::vector<Sequence>& all) {
    auto findCallee = [&](const std::string& id) -> const Sequence* {
        for (const auto& s : all) if (s.id == id) return &s;
        return nullptr;
    };
    for (auto& caller : all) {
        if (caller.broken) continue;
        walkNodes(caller.nodes, [&](Node& n) {
            if (caller.broken) return;
            if (n.type != "call-sequence") return;
            const char* cid = n.params["sequenceId"].as<const char*>();
            if (!cid) {
                caller.broken = true;
                caller.brokenReason = "call-sequence: missing sequenceId";
                return;
            }
            const Sequence* callee = findCallee(cid);
            if (!callee) {
                caller.broken = true;
                caller.brokenReason = std::string("unknown sequence: ") + cid;
                return;
            }
            JsonVariantConst args = n.params["args"];
            for (const auto& pd : callee->params) {
                if (!pd.required) continue;
                if (!pd.defaultValue.isNull()) continue;
                if (args.isNull() || !args[pd.key.c_str()].is<JsonVariantConst>()) {
                    caller.broken = true;
                    caller.brokenReason = "call-sequence to " + std::string(cid)
                                        + " missing required arg '" + pd.key + "'";
                    return;
                }
            }
        });
    }
}
}  // namespace
```

Call `validateCallSequenceArgs(seqs_)` at the end of both `load()` and `replaceAll()`.

- [ ] **Step 4: Run, PASS**

```
pio test -e native -f test_store
```

- [ ] **Step 5: Full suite**

```
pio test -e native
```

- [ ] **Step 6: Commit**

```
git add src/core/SequenceStore.cpp test/test_store/test_store.cpp
git commit -m "feat(store): mark caller broken when call-sequence missing required arg"
```

---

## Task 9: Web editor — params pane

**Files:**
- Modify: `src/web/editor.html`

The editor reads the schema from `/api/schema` and renders fields per type. We add a parameters pane between the header and tree, and reuse the existing `renderField` (or whatever the local helper is) to render the `default` cell.

- [ ] **Step 1: Add HTML markup for the params pane**

Open `src/web/editor.html`, find the section that renders the sequence header (look for a `header`-like container before the tree). Below it, before the `<main>` or tree container, add:

```html
<section id="paramsPane" class="card">
  <h2>Parameters <button id="addParamBtn" class="ghost" title="Add parameter">+</button></h2>
  <table id="paramsTable">
    <thead><tr>
      <th>key</th><th>type</th><th>label</th><th>default</th>
      <th>required</th><th>enum (CSV)</th><th></th>
    </tr></thead>
    <tbody></tbody>
  </table>
</section>
```

Use existing card/section styling already in editor.html.

- [ ] **Step 2: JS — render params table from `seq.params`**

Add a `renderParams()` function:

```js
const PARAM_TYPES = ['string','int','bool','stringlist','mac','enum','predicate'];

function renderParams() {
  const tb = document.querySelector('#paramsTable tbody');
  tb.innerHTML = '';
  (seq.params || []).forEach((p, i) => {
    const tr = document.createElement('tr');
    tr.appendChild(td(textInput(p.key, v => { p.key = v; markDirty(); })));
    tr.appendChild(td(typeSelect(p.type, v => {
      p.type = v;
      // Reset default to a type-appropriate empty.
      if (v === 'int') p.default = 0;
      else if (v === 'bool') p.default = false;
      else if (v === 'stringlist') p.default = [];
      else p.default = '';
      renderParams();
      markDirty();
    })));
    tr.appendChild(td(textInput(p.label || '', v => { p.label = v; markDirty(); })));
    tr.appendChild(td(defaultEditor(p)));
    tr.appendChild(td(checkboxInput(p.required, v => { p.required = v; markDirty(); })));
    tr.appendChild(td(textInput(p.enumValues || '',
                                v => { p.enumValues = v; markDirty(); },
                                p.type !== 'enum')));
    const del = document.createElement('button');
    del.className = 'ghost'; del.textContent = '×';
    del.onclick = () => { seq.params.splice(i,1); renderParams(); markDirty(); };
    tr.appendChild(td(del));
    tb.appendChild(tr);
  });
}
function td(el) { const t = document.createElement('td'); t.appendChild(el); return t; }
function textInput(v, onChange, disabled=false) {
  const i = document.createElement('input');
  i.type='text'; i.value=v; i.disabled=!!disabled;
  i.oninput = () => onChange(i.value);
  return i;
}
function checkboxInput(v, onChange) {
  const i = document.createElement('input');
  i.type='checkbox'; i.checked=!!v;
  i.onchange = () => onChange(i.checked);
  return i;
}
function typeSelect(v, onChange) {
  const s = document.createElement('select');
  for (const t of PARAM_TYPES) {
    const o = document.createElement('option'); o.value=t; o.textContent=t;
    if (t===v) o.selected=true;
    s.appendChild(o);
  }
  s.onchange = () => onChange(s.value);
  return s;
}
function defaultEditor(p) {
  if (p.type === 'bool') return checkboxInput(p.default, v => { p.default=v; markDirty(); });
  if (p.type === 'int') {
    const i = document.createElement('input');
    i.type = 'number'; i.value = p.default ?? 0;
    i.oninput = () => { p.default = parseInt(i.value, 10) || 0; markDirty(); };
    return i;
  }
  return textInput(typeof p.default === 'string' ? p.default : (p.default ?? ''),
                   v => { p.default = v; markDirty(); });
}

document.getElementById('addParamBtn').onclick = () => {
  seq.params = seq.params || [];
  seq.params.push({key:'param'+(seq.params.length+1), type:'string', label:'', default:'', required:false});
  renderParams(); markDirty();
};
```

Call `renderParams()` from the existing `render()` function (alongside `renderTree()` / `renderInspector()`).

- [ ] **Step 3: Ensure `seq.params` is initialized when an old sequence loads**

Look for the place that loads a sequence into `seq` (probably near the `fetch('/api/sequences')` block). After loading:

```js
seq.params = seq.params || [];
```

- [ ] **Step 4: Run gen_web and build**

```
pio run
```

Expected: clean build (the pre-build step regenerates `editor.html.gz.h`).

- [ ] **Step 5: Smoke check (optional, manual)**

Open the editor at `/edit?id=…` after flashing — confirm the Parameters pane renders and add/remove/edit work.

- [ ] **Step 6: Commit**

```
git add src/web/editor.html src/web/editor.html.gz.h
git commit -m "feat(web): parameters pane in sequence editor"
```

(`editor.html.gz.h` is a generated artifact but is checked in per the existing project pattern — confirm via `git status` whether it's tracked. If `.gitignore` excludes it, omit from the commit.)

---

## Task 10: Web editor — fx toggle on non-string fields

**File:** `src/web/editor.html`

The existing inspector renders block-field inputs. We extend the inspector so non-string fields can be toggled to a "use parameter" select.

- [ ] **Step 1: Find the inspector field-rendering function (probably `renderField` or `renderInspector`)**

Look near the `renderInspector` definition. Each field rendering ends up reading/writing `selectedNode.params[field.key]`.

- [ ] **Step 2: Wrap non-string field renderers with the fx toggle**

```js
function fieldIsString(t) { return t==='string'||t==='stringlist'||t==='mac'; }

function paramOptionsForType(t) {
  return (seq.params || []).filter(p => p.type === t).map(p => p.key);
}

function renderParamSelect(currentName, type, onPick) {
  const s = document.createElement('select');
  s.appendChild(new Option('-- pick parameter --', ''));
  for (const k of paramOptionsForType(type)) {
    const o = new Option(k, k);
    if (k === currentName) o.selected = true;
    s.appendChild(o);
  }
  s.onchange = () => onPick(s.value);
  return s;
}

// Wrap the existing per-field render with fx toggle for non-string types.
function renderFieldWithFx(node, field) {
  const wrap = document.createElement('div'); wrap.className = 'field';
  const label = document.createElement('label');
  label.textContent = field.label || field.key;

  const fxBtn = document.createElement('button');
  fxBtn.className = 'fx-toggle ghost';
  fxBtn.textContent = 'fx';
  fxBtn.title = 'Use a parameter';
  const value = node.params[field.key];
  const isFx = value && typeof value === 'object' && '$param' in value;
  if (isFx) fxBtn.classList.add('on');

  if (!fieldIsString(field.type)) label.appendChild(fxBtn);
  wrap.appendChild(label);

  const slot = document.createElement('div');
  function rerender() {
    slot.innerHTML = '';
    const v = node.params[field.key];
    if (!fieldIsString(field.type) && v && typeof v === 'object' && '$param' in v) {
      slot.appendChild(renderParamSelect(v.$param, field.type, name => {
        if (!name) { node.params[field.key] = field.type==='int'?0:field.type==='bool'?false:''; markDirty(); rerender(); }
        else { node.params[field.key] = {'$param': name}; markDirty(); }
      }));
    } else {
      slot.appendChild(renderLiteralInput(node, field));  // existing function
    }
  }
  fxBtn.onclick = (e) => {
    e.preventDefault();
    const cur = node.params[field.key];
    const isFx2 = cur && typeof cur === 'object' && '$param' in cur;
    if (isFx2) {
      node.params[field.key] = field.type==='int'?0:field.type==='bool'?false:'';
      fxBtn.classList.remove('on');
    } else {
      node.params[field.key] = {'$param':''};
      fxBtn.classList.add('on');
    }
    markDirty();
    rerender();
  };
  rerender();
  wrap.appendChild(slot);
  return wrap;
}
```

Wire this in place of the current per-field renderer in the inspector loop. Where the existing code calls `renderField(...)`, route non-string types through `renderFieldWithFx`. Keep string fields on the existing path (since `${name}` interpolation works inline in their string value).

Tiny CSS for the toggle (next to existing styles):
```css
.fx-toggle.on { background:var(--accent); color:var(--paper); }
```

- [ ] **Step 3: Build, smoke**

```
pio run
```

- [ ] **Step 4: Commit**

```
git add src/web/editor.html src/web/editor.html.gz.h
git commit -m "feat(web): fx toggle on non-string inspector fields"
```

---

## Task 11: Web editor — `call-sequence` block schema + inspector

**File:** `src/web/editor.html`

The editor needs to know about `call-sequence` (it's hardcoded server-side; the schema endpoint doesn't return it). Add a client-side known-block entry alongside `if`/`repeat` (which already exist this way — locate the local declarations).

- [ ] **Step 1: Find where `if`/`repeat` schemas are declared client-side**

Search for `'if'` and `'repeat'` strings within the script block and locate the array/object that holds local control-flow schemas.

- [ ] **Step 2: Add a `call-sequence` entry**

```js
{
  type: 'call-sequence',
  label: 'Call sequence',
  category: 'Flow',
  fields: [
    {key:'sequenceId', type:'enum', label:'Sequence', required:true, dynamicEnum:'sequences'},
    {key:'args',       type:'object', label:'Args'}
  ],
  childSlots: []
}
```

`dynamicEnum:'sequences'` is a sentinel for a runtime-resolved enum; in the renderer treat it specially — fetch options from `allSeqs.map(s => ({value:s.id, text:s.name}))`. Filter out `seq.id` itself (no self-call).

- [ ] **Step 3: Render the `args` form when a call-sequence node is selected**

When the inspector renders a node of type `call-sequence` and `sequenceId` is set:

```js
function renderCallArgsForm(node) {
  const callee = allSeqs.find(s => s.id === node.params.sequenceId);
  if (!callee || !callee.params || !callee.params.length) return null;
  const wrap = document.createElement('div'); wrap.className='args-form';
  for (const p of callee.params) {
    // Each arg input gets the same fx-toggle as block fields.
    // node.params.args is an object mapping callee-param-name -> value-or-{$param}.
    if (!node.params.args) node.params.args = {};
    const fakeField = {key:p.key, type:p.type, label:p.label||p.key, default:p.default, enumValues:p.enumValues};
    // Reuse renderFieldWithFx but on a synthetic 'args' object:
    const subnode = { params: node.params.args };
    wrap.appendChild(renderFieldWithFx(subnode, fakeField));
  }
  return wrap;
}
```

In the inspector code, after rendering the standard fields for a `call-sequence` node, append `renderCallArgsForm(selectedNode)`.

- [ ] **Step 4: Build, smoke**

```
pio run
```

- [ ] **Step 5: Commit**

```
git add src/web/editor.html src/web/editor.html.gz.h
git commit -m "feat(web): call-sequence inspector with callee arg form"
```

---

## Task 12: Trigger binding form renders sequence args with defaults

**File:** `src/web/triggers.html`

When the user picks a `sequenceId` for a trigger binding, render an "Arguments" sub-form generated from that sequence's `params`, prefilled with each param's `default`. Persist into `binding.args`.

- [ ] **Step 1: Locate the binding form render in `src/web/triggers.html`**

Find the function that renders the per-binding row. Identify where `sequenceId` is selected.

- [ ] **Step 2: Add an args sub-form**

```js
function renderBindingArgs(binding) {
  const wrap = document.createElement('div'); wrap.className='args';
  const seq = allSeqs.find(s => s.id === binding.sequenceId);
  if (!seq || !seq.params || !seq.params.length) return wrap;
  binding.args = binding.args || {};
  for (const p of seq.params) {
    if (!(p.key in binding.args)) binding.args[p.key] = p.default ?? '';
    const row = document.createElement('div'); row.className='field';
    const lbl = document.createElement('label'); lbl.textContent = p.label || p.key;
    row.appendChild(lbl);
    // simple literal editor — no fx toggle here, since binding-time values
    // are concrete, not references.
    if (p.type === 'bool') {
      const i = document.createElement('input'); i.type='checkbox'; i.checked = !!binding.args[p.key];
      i.onchange = () => { binding.args[p.key] = i.checked; markDirty(); };
      row.appendChild(i);
    } else if (p.type === 'int') {
      const i = document.createElement('input'); i.type='number'; i.value = binding.args[p.key];
      i.oninput = () => { binding.args[p.key] = parseInt(i.value,10)||0; markDirty(); };
      row.appendChild(i);
    } else if (p.type === 'enum') {
      const s = document.createElement('select');
      for (const v of (p.enumValues||'').split(',')) {
        const o = new Option(v,v); if (v===binding.args[p.key]) o.selected=true; s.appendChild(o);
      }
      s.onchange = () => { binding.args[p.key] = s.value; markDirty(); };
      row.appendChild(s);
    } else {
      const i = document.createElement('input'); i.type='text';
      i.value = Array.isArray(binding.args[p.key]) ? binding.args[p.key].join(',') : binding.args[p.key];
      i.oninput = () => {
        binding.args[p.key] = (p.type==='stringlist') ? i.value.split(',').map(s=>s.trim()) : i.value;
        markDirty();
      };
      row.appendChild(i);
    }
    wrap.appendChild(row);
  }
  return wrap;
}
```

Call `renderBindingArgs(binding)` in the binding row render after the `sequenceId` select. Re-render when the select changes.

- [ ] **Step 3: Build**

```
pio run
```

- [ ] **Step 4: Commit**

```
git add src/web/triggers.html src/web/editor.html.gz.h
git commit -m "feat(web): trigger binding form renders sequence args with defaults"
```

(`editor.html.gz.h` may include all generated PROGMEM blobs; if separate generated file for triggers, adapt the path.)

---

## Task 13: Final integration — full test sweep + manual smoke

- [ ] **Step 1: Full native test suite**

```
pio test -e native
```

Expected: all green.

- [ ] **Step 2: Firmware build**

```
pio run
```

Expected: clean build. Verify firmware size hasn't ballooned (`.pio/build/esp32c3/firmware.bin` < ~70% of partition).

- [ ] **Step 3: Manual smoke (optional, on-device)**

If hardware accessible:

1. Flash. Open `/edit?id=<existing>` — confirm Parameters pane renders empty for default-seeded sequences.
2. Add a parameter, reference it via `${...}` in a Wait block's arbitrary string field, save.
3. From `/triggers`, edit the existing `/trigger` HTTP binding — confirm Arguments form renders.
4. `curl -XPOST 'http://<ip>/play?n=2'` against a parametrized HTTP-bound sequence; confirm the queued run uses `n=2`.
5. Create a `call-sequence` block invoking the parametrized sequence with literal args; run the parent and confirm.

- [ ] **Step 4: Update `CLAUDE.md`** with one short bullet under "Architecture" so the next contributor finds the moving parts:

```
- Sequences are parametrizable: `Sequence.params` are typed (FieldType); node fields use
  `${name}` (strings) or `{"$param":"name"}` (typed) substitution, resolved per-node by
  `resolveParams`. `call-sequence` is a control-flow block handled directly by Interpreter
  (with cycle/depth guards). `TriggerBinding.args` are the binding-time defaults; HTTP
  trigger overrides per-key via query string (coerced against `Sequence.params` types).
```

- [ ] **Step 5: Commit final docs touch**

```
git add CLAUDE.md
git commit -m "docs: note parametrized sequences in CLAUDE.md"
```

---

## Self-review notes (already applied)

- **Spec coverage:** every spec section maps to ≥1 task.
  - §2 data model → Task 1.
  - §3 referencing → Task 2 (helper) + Task 3 (wired in interpreter).
  - §4 call-sequence → Task 4. Caller-broken-on-bad-callee-arg → Task 8.
  - §5 trigger binding overrides → Task 5 + Task 7 (queue carries args) + Task 12 (UI).
  - §6 HTTP query overrides → Task 6 + `/api/run` body → Task 7.
  - §7 wire format → Task 1 (codec).
  - §8 editor changes → Tasks 9, 10, 11, 12.
  - §10 testing → tests live alongside their tasks.
  - §11 file layout → File Structure table at top.

- **Type consistency:** `FireCallback` signature `(const std::string&, JsonVariantConst)` used in Trigger.h, TriggerManager.{h,cpp}, BleMacTrigger, HttpRouteTrigger, ApiHooks, main.cpp.

- **No placeholders.** Every code step shows the actual code.
