# Changelog

## 2026-09-06 — Native GodMode and authenticated RCON authority

- Added native `SetGodMode true|false <SteamID>` for a connected target with a live pawn.
- Added `!godmode_state` and construction-only `!godmode_prepare` diagnostics.
- Carried authenticated RCON authority through the queue; rejected unauthenticated
  requests before game-thread execution. Target chat permissions remain independent.
- Added strict target parsing, validated object construction and vtable checks,
  native build guards, and verification of GodMode with unchanged Immortality.
- Added a bounded observer for development diagnostics.
- Preserved existing observer tools; disabled the obsolete interface-bypass experiment.
- Made diagnostic log paths relative to the server working directory and expanded
  exclusions for deployment configuration and runtime data.
- Replaced environment-specific research documents with general public documentation.
  Raw research, captures and the local development history remain private.

Validation: Windows x64 Shipping build, 16 parser checks and authority/queue tests.
Live tests confirmed on/off for admin and non-admin recipients, invalid/disconnected
request rejection and authentication failure without a native call. Herbie remained
loaded; a test without it is still pending. Portable log paths were rebuilt after
these live tests and have not been redeployed as part of publication.

## Earlier development

Implemented the Source RCON listener, game-thread command queue, object-discovery
diagnostics and PE cross-reference tooling. Reflected command paths were found
ineffective. Early native experiments failed; the current GodMode path uses normal
object construction and the correctly identified native handler. Raw investigation
logs and environment-specific notes are excluded from the public tree.
