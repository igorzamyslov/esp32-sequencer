# Parametrized & Reusable Sequences — Design

Date: 2026-05-05
Status: Draft → review

## 1. Motivation

Today every sequence is a hardcoded recipe. Switching the TV from HDMI3 to HDMI2 requires editing the saved sequence in the web UI; running the same "wake host + select source" pattern for two different host/source combinations means duplicating the whole sequence.

Goal: sequences become reusable templates.

- **Parameters with defaults**: a sequence declares typed parameters; node fields can reference them.
- **Sequences calling sequences**: a `call-sequence` block invokes another sequence with explicit args.
- **Trigger-bound defaults**: when binding a sequence to a trigger, the user fills in the params; that becomes the trigger's fixed payload.
- **HTTP query-param overrides**: `POST /play?source=2` overrides the binding's defaults at fire time.

Builds on the existing sequence-builder spec (`2026-05-04-sequence-builder-design.md`) — additive only.

## 2. Core data model

Two additive fields. Old stored data loads unchanged.

```cpp
struct ParamDef {
    std::string key;            // e.g. "hostMac"
    FieldType   type;           // reuse existing FieldType enum
    std::string label;
    JsonDocument defaultValue;  // typed: string/int/bool stored natively
    std::string enumValues;     // CSV, only when type == Enum
    bool        required = false;
};

struct Sequence {
    std::string id, name;
    uint32_t    cooldownMs = 60000;
    std::vector<ParamDef> params;   // NEW — empty for old/un-parametrized sequences
    std::vector<Node> nodes;
    bool        broken = false;
    std::string brokenReason;
};

struct TriggerBinding {
    std::string id, type;
    JsonDocument params;         // trigger's own fields (path, mac, …)
    std::string sequenceId;
    JsonDocument args;           // NEW — callee-param-name → value
    bool enabled = true;
};
```

`FieldType` is the existing enum (`Bool`, `Int`, `String`, `StringList`, `MacAddress`, `Enum`, `PredicateRef`); `ParamDef` is structurally similar to `FieldDef` but owns its strings (heap-allocated, since it comes from user data, not C-string literals).

## 3. Parameter referencing — hybrid

Decided per-field at substitution time, based on the field's declared `FieldType` in the block's schema.

### 3.1 String fields (`String`, `StringList`, `MacAddress`)

`${name}` placeholder syntax inside the literal value. Multiple placeholders and surrounding literal text concatenate naturally.

Examples:

- `mac: "${hostMac}"` — pure substitution.
- `path: "/api/${room}/light"` — interpolation in a longer string.
- `keys: "KEY_SOURCE,${navKey},KEY_ENTER"` — StringList with one slot parametrized.

For `StringList`, substitution applies to the literal CSV string before it is split.

### 3.2 Non-string fields (`Int`, `Bool`, `Enum`, `PredicateRef`)

Field value is either a literal or the JSON object `{"$param": "name"}`. The editor inspector shows an "fx" toggle next to the input; toggled on, the input is replaced by a dropdown of currently-declared param names whose type matches.

```jsonc
{ "type":"wait", "params": { "ms": {"$param": "delay"} } }
```

### 3.3 Resolution

The interpreter calls `resolveParams(JsonVariantConst raw, const ParamScope&) -> JsonDocument` for each node's `params` (and recursively into `if`'s `predicate.params`) before invoking the block. Blocks see fully-resolved params and need no changes.

Errors during resolution abort the run with a `RunResult::failed` carrying `unknown param: <name>` or `param type mismatch: <name> expected int got string`.

`ParamScope` is the current call-frame's `std::map<std::string, JsonDocument>`. Default frame at sequence start = the binding's `args` merged over the sequence's `ParamDef` defaults.

## 4. `call-sequence` block

New core block — control-flow, like `if` and `repeat`, so the interpreter handles it directly rather than the registry.

```
type: "call-sequence"
fields:
  sequenceId   Enum   (populated client-side from /api/sequences)
  args         (object) callee-param-name → value-or-${ref}
```

Runtime steps:
1. Resolve `args` against the current scope (so `${...}` inside args refers to caller's params).
2. Look up callee `Sequence` by id; if missing or `broken`, fail.
3. Push callee id onto `RunCtx.callStack`. If already on the stack → fail `cycle: <id>`.
4. Push new scope: callee's `ParamDef` defaults overlaid with resolved args.
5. `runSlot(callee.nodes, ctx)` with the new scope active.
6. Pop scope and call stack.

Hard depth cap: 8. Defensive only — cycle detection is the primary guard.

Args missing required callee params → fail at run time (not at edit time, since args may be `${...}` references resolvable only at run time).

The editor surfaces dangling args (callee param removed) as a sequence-level warning by extending `SequenceStore::reload`: after decode, walk all `call-sequence` nodes and check args keys against the callee's current `ParamDef` list. Missing required keys → mark caller `broken`.

## 5. Trigger binding overrides

`TriggerBinding.args` (new) is the binding's "default fire payload." Stored in NVS as part of the existing triggers JSON blob.

The editor's binding form, after the user picks a `sequenceId`, fetches that sequence and renders an input row per `ParamDef` (using existing field renderers) pre-filled with the sequence's default. The user adjusts and saves.

Fire path:

```cpp
// before
using FireCallback = std::function<void(const std::string& sequenceId)>;
// after
using FireCallback = std::function<void(const std::string& sequenceId,
                                         JsonVariantConst args)>;
```

The 1-deep queue in `main.cpp` carries the args (as an owned `JsonDocument`) alongside the id; `Interpreter::runSequence` accepts the args and seeds the initial scope with them.

Lifetime: the callback receives a view (`JsonVariantConst`); consumers that store it (i.e. `enqueueRun`) copy into their own `JsonDocument`. The producer (HTTP/BLE trigger) is free to drop its source buffer after the callback returns.

## 6. HTTP query-param overrides

`HttpRouteTrigger` needs to know the sequence's `ParamDef`s at fire time to coerce query strings. Today the trigger interface receives only `JsonVariantConst params` (its own fields). We extend `Trigger::bind` to also receive a `SequenceLookup`:

```cpp
using SequenceLookup = std::function<const Sequence*(const std::string&)>;

class Trigger {
public:
    virtual void bind(const std::string& bindingId,
                      JsonVariantConst params,
                      const std::string& sequenceId,
                      JsonVariantConst defaultArgs,   // NEW
                      SequenceLookup lookup,           // NEW
                      FireCallback onFire) = 0;
    // ...
};
```

`BleMacTrigger` ignores the new args; only HTTP uses them.

### 6.1 Coercion

For each query param `k=v`:
1. Find `ParamDef` for `k` on the sequence. If absent → ignore silently.
2. Coerce `v` to `ParamDef.type`:
   - `String`, `MacAddress`: verbatim. (`MacAddress` not validated at coerce time — block code already validates.)
   - `StringList`: split on comma.
   - `Int`: `strtol` with full-string consumption check; failure → `400 invalid param '<k>': not int`.
   - `Bool`: lower-cased match; `1`/`true`/`yes`/`on` → true; `0`/`false`/`no`/`off` → false; else 400.
   - `Enum`: must equal one of the CSV `enumValues`; else 400.
   - `PredicateRef`: not overridable via query params; ignored silently.
3. Build a `JsonDocument` starting from the binding's `defaultArgs`; overlay coerced query overrides; pass to `cb`.

Unknown query keys are ignored silently. Missing required-without-default params → fire-time run failure (same path as direct `POST /api/sequences/:id/run`).

### 6.2 `POST /api/sequences/:id/run`

Already exists. Body is now an optional JSON object `{ "args": {...} }`. Validation rules same as the query-coerced path but no string coercion needed (JSON is already typed). Body absent or args absent → run with sequence defaults only.

## 7. Wire format

Additive only. Old data deserializes; new fields default-empty.

```jsonc
// /api/sequences
[{
  "id": "ab12cd34", "name": "wake to source", "cooldownMs": 60000,
  "params": [
    {"key":"hostMac","type":"mac","label":"Host MAC","default":"AA:BB:CC:DD:EE:FF","required":true},
    {"key":"steps","type":"int","label":"Right presses","default":3}
  ],
  "nodes": [
    {"type":"wol","params":{"mac":"${hostMac}"},"children":{}},
    {"type":"samsung-keys","params":{"keys":"KEY_SOURCE","settle_ms":900},"children":{}},
    {"type":"repeat","params":{"count":{"$param":"steps"}},"children":{
       "body":[{"type":"samsung-keys","params":{"keys":"KEY_RIGHT","settle_ms":350},"children":{}}]
    }},
    {"type":"samsung-keys","params":{"keys":"KEY_ENTER","settle_ms":350},"children":{}}
  ]
}]

// /api/triggers
[{
  "id":"7788aabb", "type":"http-route",
  "params":{"path":"/play"},
  "sequenceId":"ab12cd34",
  "args":{"hostMac":"AA:BB:CC:DD:EE:FF","steps":3},
  "enabled":true
}]
```

## 8. Editor changes

### 8.1 Sequence editor — Parameters pane

New collapsible section between the sequence header and the node list. Each row:
- key (text)
- type (select: existing FieldType enum)
- label (text)
- default (rendered using existing field renderer for the chosen type)
- required (checkbox)
- enumValues (text, only when type == Enum)
- delete-row button

Add-row button at bottom.

### 8.2 Inspector — "fx" toggle on non-string fields

Each non-string input gets a small "fx" toggle button. Toggled on:
- input replaced by a `<select>` listing the current sequence's `ParamDef`s with matching type
- field value in the JSON becomes `{"$param":"<chosen>"}`
- toggling off restores literal input + clears the value

String fields don't get the toggle — `${name}` interpolation works inline.

### 8.3 Trigger binding form

When the user picks a sequenceId, the form re-renders to include an "Arguments" subsection generated from that sequence's `ParamDef`s, pre-filled with the sequence's defaults. Same renderers as the parameters pane.

If the sequence has no params, the subsection is hidden.

### 8.4 `call-sequence` inspector

Two-stage rendering:
- sequenceId dropdown (populated from `/api/sequences`).
- After selection: an args form generated from the callee's `ParamDef`s. Each arg input carries the same "fx" toggle as block fields, so an arg can itself be `${caller-param}`.

### 8.5 Palette grouping

`call-sequence` lives in the existing "Flow" category alongside `if` and `repeat`.

## 9. Backwards compatibility

- Old stored sequences without `params` field → loaded with `params=[]`. Existing nodes have no `${...}` and no `{"$param":...}` so nothing resolves; behavior unchanged.
- Old triggers without `args` field → loaded with `args={}`. Sequences without params don't read it; behavior unchanged.
- Default-seed sequences (`DefaultSequences.cpp`) are not parametrized; no change needed in v1. We may parametrize them in a follow-up.

## 10. Testing (native)

New test files. Existing tests stay green.

- `test_resolve_params/`:
  - `${name}` single substitution in `String`.
  - Multi-`${}` and surrounding literals concatenate correctly.
  - `${name}` in `StringList` substitutes before split.
  - `{"$param":"x"}` for `Int`/`Bool`/`Enum` substitutes typed value.
  - Type mismatch → failed with descriptive error.
  - Missing param → failed with `unknown param: <name>`.
  - Nested `if.predicate.params` resolves.
- `test_call_sequence/`:
  - Args resolved against caller scope before pushing callee scope.
  - Callee defaults overlaid by args correctly.
  - Cycle A→B→A fails with `cycle: A`.
  - Depth cap (8) fails self-recursive call before stack grows further.
  - Calling a `broken` callee fails.
  - Caller marked `broken` when callee param is renamed away.
- `test_http_query_coercion/` (native, no Arduino deps):
  - `Int` success and failure.
  - `Bool` covers all accepted forms; reject unknown.
  - `Enum` accepts only declared values.
  - Unknown query key ignored.
  - `StringList` split on comma.
  - Defaults overlaid by query.
- `test_codec/` extended:
  - `params` and `args` round-trip for sequences and triggers.

## 11. File layout impact

```
src/core/
  ParamScope.h           NEW — scope type + push/pop helpers
  ResolveParams.{h,cpp}  NEW — substitution helper
  Sequence.h             EDIT — add params, args
  Interpreter.{h,cpp}    EDIT — call-sequence, scope stack, resolve before block.run
  Trigger.h              EDIT — bind() signature gains defaultArgs + lookup
  TriggerManager.{h,cpp} EDIT — pass lookup + defaultArgs to bind()
  SequenceCodec.{h,cpp}  EDIT — encode/decode params + args
  SequenceStore.{h,cpp}  EDIT — caller-broken-on-missing-callee-arg check
src/adapters/triggers/
  HttpRouteTrigger.{h,cpp} EDIT — query-param coercion, FireCallback signature
  BleMacTrigger.{h,cpp}    EDIT — bind() signature update only (ignores new args)
src/main.cpp             EDIT — queue carries args; FireCallback signature update
src/web/                 EDIT — params pane, fx toggle, binding-form args, call-sequence inspector
src/web/editor.html.gz.h GENERATED
test/
  test_resolve_params/
  test_call_sequence/
  test_http_query_coercion/
  test_codec/  (extended)
```

## 12. Non-goals (explicit YAGNI)

- No expressions or arithmetic on params (no `${a + 1}`).
- No conditional defaults; no per-binding "lock" of individual params (any declared param is overridable via query string).
- No `PredicateRef` overrides via query string.
- No schema migration of stored data — additive only.
- No nested-args-in-args UI past one level (the args form for `call-sequence` shows fx-toggled inputs that resolve against caller scope; deeper composition is up to the author via intermediate sequences).
- No edit-time validation that `${name}` references resolve — surfaced at run time with a clear error.
- No multi-arg query syntax beyond `?k=v&k2=v2`. No JSON-in-query.

## 13. Open questions

None blocking. Deferred:

- Whether to also parametrize the default-seed sequences in `DefaultSequences.cpp`. Would let a user bind a single "wake host" template to multiple triggers with different MACs out of the box. Trivial follow-up once the runtime is in place.
- Whether `BleMacTrigger` should accept query-equivalent overrides via a different channel (e.g. encoded in BLE manufacturer data). Out of scope for v1.
