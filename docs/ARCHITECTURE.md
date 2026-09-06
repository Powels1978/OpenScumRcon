# Architecture

## Request and authority flow

The native UE4SS module starts a Source RCON TCP listener. A client must authenticate
before a command is enqueued. `CommandAuthority::authenticated_rcon` travels with
the request through the queue to the game-thread dispatcher. Requests without that
authority are rejected before any native command call. Response futures remain
associated with their individual requests.

Unreal object discovery, construction and execution happen synchronously on the
game thread. Network threads do not access gameplay objects.

## Native GodMode

The target is resolved by its exact SteamID using live `ConZPlayerController`
instances with a valid connection and Prisoner pawn. No fallback to another player
is allowed. The dispatcher resolves the `SetGodMode_C` class and creates a regular
command instance with the target controller as Outer.

The implementation calls the version-checked native GodMode handler with a borrowed
`TArray<FString>` view containing one boolean argument. The SteamID is used to select
the recipient and is not forwarded as an extra native argument. Unreal owns the
command instance; the module neither modifies the class default object's Outer nor
fabricates an executor interface pointer.

RCON authority is separate from the target's chat permissions. The preparation
request reports SCUM's chat permission check for diagnosis; it is not a permission
gate for an authenticated RCON command. The dispatcher does not grant the recipient
admin rights or change shared chat cooldown timestamps.

Execution verifies the resulting GodMode flag and checks that Immortality was
preserved. Replies describe that verified state. Native SCUM response capture has
not yet been implemented.

## Version and observation guards

The tested native path validates the executable image metadata, function signatures,
object identity and vtable slots. An unsupported signature disables execution.
Offsets are implementation details of the tested build, not portable promises.

The optional GodMode observer is bounded to four calls and a 120-second window. It
records local diagnostic data and calls the original function without altering its
arguments or result. Other historical diagnostic commands are development tools;
their local output must not be committed or published.

## Validation and open work

Standalone tests cover request parsing, rejecting missing authority, authenticated
queue handoff and response association. Live tests verified GodMode on/off for
admin and non-admin targets, unchanged Immortality, disconnected-target rejection,
and rejection before native execution when authentication was missing or incorrect.
Herbie remained loaded during the tests; removal still needs a separate validation.

Two reflected paths did not perform the requested gameplay change:
`MiscStatics::Test_ProcessAdminCommand` is a Shipping stub, and the attempted
`PlayerRpcChannel::Chat_Server_ProcessAdminCommand` invocation had no effect.
A historical RPC acknowledgement must not be interpreted as successful execution.

Next work is the registry-based dispatcher, native response capture and an independent
`ListPlayers` query. Commands needing a server-wide executor without any connected
player require additional investigation. No blanket compatibility claim is made.
