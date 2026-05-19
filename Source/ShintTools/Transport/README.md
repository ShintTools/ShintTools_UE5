# Transport — pure HTTP layer

**Sprint 1** of the UE5 plugin refactor. The intent of this folder is to host
the network plumbing that today lives inside the 2000-line `FShintCoreClient`.

## What lives here today

- `FShintHttpClient` — pure transport. Method enum, result struct, send +
  dispatch callback, JSON serialise helper, timeout policy. No knowledge of
  any specific endpoint, no parsing.

## What does NOT live here yet

- Endpoint-specific request builders. They still live in `Core/ShintCoreClient.cpp`
  and will be migrated to `Api/<Domain>/` folders in Sprint 2+.
- DTOs. Still in `Core/ShintCoreClient.h`. Move to `Models/` in Sprint 3.

## Sprint 1 scope

This Sprint **only extracts the transport**. `FShintCoreClient` continues to
work unchanged because its existing `FShintRequestResult` + `SendRequest`
remain in place. New endpoints (and migrated endpoints in later sprints) can
take a `FShintHttpClient&` reference instead of depending on the whole
core client.

## Why this comes first

- Adding mTLS / cert pinning (security Capa B) is a single-file change here
  in the future, instead of touching every endpoint method.
- Tests can stub `FShintHttpClient` without touching reflection, Slate, or
  the 90+ DTOs in the core client header.
- The refactor is reversible: if Sprint 2 stalls, the transport simply sits
  unused — no behaviour change.

## Sprint 2 (next)

Migrate one endpoint as proof: pick `/validate/code` (smallest payload, no
state machine). Create `Api/Validate/ValidateCodeApi.{h,cpp}` that takes a
`TSharedRef<FShintHttpClient>` and exposes `Send(payload, OnDone)`. Wire
`FShintCoreClient::RequestValidateCode` to delegate.
