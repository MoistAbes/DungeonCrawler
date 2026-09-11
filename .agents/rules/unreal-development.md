# Unreal Development — DungeonCrawler

This rule defines how development work should be performed in Unreal Engine for the DungeonCrawler project.

Project-wide architectural principles are defined in `CONSTITUTION.md`.
The current project architecture is defined in `ARCHITECTURE.md`.

---

## 1. Unreal First

Use Unreal Engine's native architecture and lifecycle as the foundation of the project.

Prefer appropriate Unreal mechanisms such as:

- `AActor`
- `ACharacter`
- `UActorComponent`
- `UObject`
- Unreal Interfaces
- Delegates and Events
- Subsystems when appropriate
- Enhanced Input
- UMG
- Data Assets
- Data Tables
- Gameplay Tags where appropriate
- Unreal replication and RPCs

Do not translate Java/Spring architecture directly into Unreal.

An abstraction that is useful in backend development is not automatically useful in Unreal.

---

## 2. Understand Unreal Ownership and Lifecycle

Before changing or creating a system, understand:

- who owns the object,
- when it is constructed,
- when it becomes part of the world,
- when replication begins,
- when gameplay starts,
- when it can be safely accessed,
- when it is destroyed.

Respect Unreal lifecycle methods and ownership semantics.

Do not introduce custom lifecycle abstractions when Unreal already provides the required lifecycle.

When using an Actor Component, consider whether its behavior belongs to:

- constructor/default setup,
- `BeginPlay`,
- initialization,
- replication lifecycle,
- explicit gameplay events,
- destruction/cleanup.

---

## 3. Actor Components

Use `UActorComponent` when behavior is:

- reusable,
- logically independent,
- applicable to multiple actors,
- stateful gameplay functionality that should not belong to the actor itself.

Good candidates include:

- interaction,
- damage/durability,
- status effects,
- carry/manipulation,
- specialized character functionality.

Do not create a component merely to move a few lines of code out of an actor.

A component should have a clear responsibility and meaningful lifecycle/ownership.

---

## 4. Actors

Use an `AActor` when the object:

- exists independently in the world,
- has world presence,
- participates in collision/physics,
- has replication requirements,
- represents an environmental or gameplay entity.

Do not create an Actor when a component, UObject or simple data structure is the more appropriate Unreal abstraction.

---

## 5. UObject

Use `UObject` when behavior or state needs Unreal's object system but does not itself need world presence.

Typical reasons include:

- reusable non-world logic,
- data-oriented Unreal objects,
- objects requiring reflection/serialization,
- objects participating in Unreal ownership/lifecycle without being Actors.

Do not use `UObject` simply because it feels more "architectural".

---

## 6. Interfaces

Use Unreal Interfaces to represent capabilities.

Examples in this project include:

- `IInteractableInterface`
- `IGrabbableInterface`
- `IMaterialProviderInterface`
- `IMechanismReceiverInterface`
- `IStatProviderInterface`
- `ICarryAnchorProviderInterface`

Prefer an interface when a system needs to interact with multiple implementations through a shared capability.

Do not create an interface for every class or every method.

An interface should represent a meaningful contract.

---

## 7. Composition Over Deep Inheritance

Prefer composition when behavior is reusable across unrelated actor types.

For example:

```text
Actor
├── DamageableComponent
├── InteractionComponent
├── StatusEffectComponent
└── PhysicsCarryComponent
```

Use inheritance when there is a meaningful Unreal type relationship and shared behavior/state.

Avoid deep inheritance trees where base classes accumulate unrelated responsibilities.

Do not force composition when a simple inheritance relationship is clearly the better Unreal design.

---

## 8. Blueprint and C++ Boundary

Use C++ for core reusable gameplay behavior, especially when the system contains:

- complex state,
- authoritative multiplayer logic,
- reusable gameplay rules,
- non-trivial physics,
- performance-sensitive behavior,
- important contracts.

Use Blueprint for appropriate:

- actor composition,
- asset configuration,
- designer-facing tuning,
- presentation,
- simple event wiring,
- feature-specific orchestration.

Blueprint is not "bad architecture".

C++ is not automatically better for every piece of logic.

Choose the boundary based on responsibility, reuse, complexity and Unreal workflow.

---

## 9. Blueprint Graph Complexity

Do not allow Blueprint graphs to become accidental gameplay architectures.

If a Blueprint begins accumulating:

- complex state machines,
- extensive branching,
- repeated logic,
- networking rules,
- reusable algorithms,
- complicated physics logic,

evaluate whether the underlying logic belongs in a reusable C++ system.

Conversely, do not move simple composition or configuration into C++ solely to avoid Blueprint.

---

## 10. Networking and Server Authority

The project uses server-authoritative multiplayer.

Client input should represent intent.

The server should:

- validate requests,
- make authoritative gameplay decisions,
- mutate authoritative shared state,
- replicate relevant results.

For every new multiplayer interaction, identify:

```text
Client intent
    ↓
Server request
    ↓
Validation
    ↓
Authoritative gameplay
    ↓
Replication
    ↓
Client presentation
```

Do not trust client-provided gameplay outcomes.

---

## 11. RPCs

Use RPCs when communication across the network boundary is actually required.

Before adding an RPC, determine:

- who calls it,
- which machine executes it,
- whether validation is required,
- whether the request can be abused,
- whether the result should be replicated instead,
- whether the operation is appropriate for the owning Actor/Component.

Do not create RPCs merely because a function is related to multiplayer.

Keep authoritative gameplay on the server.

---

## 12. Replication

Replicate state that other machines genuinely need.

Do not replicate every property by default.

For each replicated property, understand:

- who owns it,
- who modifies it,
- who receives it,
- why other machines need it,
- what happens when it arrives.

Prefer replicating authoritative state/results over trusting replicated client intent.

Keep replication responsibility close to the system that owns the state.

---

## 13. Client Validation

Any client request that can affect shared gameplay must be treated as untrusted input.

Server validation should consider:

- actor ownership,
- distance/range,
- current gameplay state,
- permissions,
- valid targets,
- cooldowns/timing,
- physical/gameplay constraints,
- whether the requested transition is legal.

Validation belongs at the authoritative boundary.

Do not rely on the client UI or Blueprint to enforce rules that matter to multiplayer correctness.

---

## 14. State Machines

Use explicit state modeling when a system contains mutually exclusive gameplay states.

Prefer:

```text
enum/state
    ↓
explicit transitions
    ↓
legal state behavior
```

over multiple independent booleans whose combinations are difficult to reason about.

Existing example:

```text
UPhysicsCarryComponent

None
 ↓
RequestingGrab
 ↓
Carrying
 ↓
Releasing
 ↓
None
```

When a system starts accumulating interacting flags, evaluate whether those flags represent a missing state machine.

---

## 15. Events and Delegates

Prefer events/delegates when one system needs to notify another without creating unnecessary direct coupling.

Typical flow:

```text
Gameplay System
      │
      ▼
Delegate / Event
      │
 ┌────┴────┐
 ▼         ▼
UI       Gameplay reaction
```

Events should communicate meaningful state changes or occurrences.

Do not create events for every trivial internal operation.

---

## 16. Tick

Do not add `Tick` automatically.

Before using per-frame Tick, ask:

- Does this genuinely require per-frame evaluation?
- Can an event solve it?
- Can an overlap callback solve it?
- Can a timer solve it?
- Can a state transition solve it?
- Can the calculation be performed only when relevant?

Use Tick when continuous evaluation is genuinely required.

Avoid expensive work inside high-frequency Tick paths.

---

## 17. Timers

Prefer Unreal timers for delayed or periodic operations that do not require per-frame execution.

Typical use cases include:

- cooldowns,
- delayed state transitions,
- temporary effects,
- timed environmental behavior,
- periodic gameplay events.

A timer should be owned and cleaned up by the system responsible for the behavior.

---

## 18. Physics

Physics-related gameplay should respect Unreal's physics model and authority rules.

When implementing physics interactions, consider:

- server authority,
- collision state,
- simulation state,
- ownership,
- replication,
- physical constraints,
- performance,
- whether the operation should be continuous or event-driven.

Reuse existing project physics helpers where applicable, including:

- `UKineticForceLibrary`
- `UPhysicsCarryComponent`

Do not create another physics abstraction without first inspecting existing functionality.

---

## 19. Collision and Overlap

Use Unreal collision/overlap mechanisms according to the actual gameplay requirement.

Before adding manual collision polling, check whether the behavior can use:

- overlap events,
- hit events,
- collision channels,
- object types,
- existing interaction traces,
- existing project helpers.

Avoid repeated world queries when an event-driven solution is sufficient.

---

## 20. Input

The project uses Enhanced Input.

Input actions should represent player intent such as:

- Move,
- Look,
- Jump,
- Interact,
- Throw,
- Zoom.

Do not put authoritative gameplay rules inside input mappings.

Prefer:

```text
Input Action
    ↓
Player intent
    ↓
Gameplay system
    ↓
Server authority when required
```

Input configuration should remain replaceable without rewriting gameplay rules.

---

## 21. Data-Driven Design

Use data-driven configuration when values or rules need to be:

- reused,
- tuned frequently,
- designer-editable,
- shared across multiple instances,
- separated from executable logic.

Appropriate Unreal mechanisms may include:

- `USTRUCT`,
- Data Assets,
- Data Tables,
- Blueprint-exposed properties,
- Gameplay Tags.

Do not create a data-driven framework when a simple property is sufficient.

---

## 22. Gameplay Tags

Use Gameplay Tags when they provide a meaningful vocabulary for:

- gameplay states,
- categories,
- effects,
- capabilities,
- filtering,
- scalable classification.

Do not replace simple enums or straightforward state variables with Gameplay Tags without a real need for tag-based composition or querying.

---

## 23. Casting

`Cast<T>` is allowed when a concrete Unreal type is genuinely required.

Do not treat casting as inherently bad.

However, repeated casts across many concrete actor types are often a sign of excessive coupling.

Prefer:

- interfaces,
- components,
- events/delegates,
- explicit references,

when the relationship represents a capability or reusable contract.

Rule:

> A cast may be an implementation detail. It should not become the architecture.

---

## 24. Unreal Naming and Conventions

Follow Unreal naming conventions and existing project conventions.

Respect Unreal prefixes such as:

```text
A  → Actor
U  → UObject / Component
F  → Struct
E  → Enum
I  → Interface
```

Follow the naming patterns already established in the repository rather than inventing a parallel convention.

Do not rename existing classes or files merely for stylistic preference unless there is a concrete reason.

---

## 25. Headers and Dependencies

Keep dependencies intentional.

Avoid including large or unrelated headers when forward declarations are sufficient.

Do not introduce circular dependencies.

When adding a dependency, ask:

- Does this class actually need it?
- Could an interface or component remove the dependency?
- Is the dependency appropriate for the domain?
- Does it create an unnecessary coupling direction?

Do not create abstraction solely to avoid one legitimate Unreal dependency.

---

## 26. Build Configuration

When adding Unreal functionality, update module dependencies only when required.

Respect the existing `MyProject.Build.cs`.

Do not add engine modules speculatively.

When a new dependency is necessary, keep it explicit and minimal.

---

## 27. Logging

Use the project's existing Unreal logging categories where appropriate.

Prefer meaningful diagnostics over excessive logging.

Do not use logging as gameplay communication.

Avoid noisy logs inside:

- Tick,
- frequent overlap processing,
- high-frequency physics,
- frequently executed network paths,

unless actively diagnosing a problem.

---

## 28. Asset and Blueprint Verification

Source code alone cannot fully verify Unreal Editor state.

When a task involves:

- Blueprint graphs,
- Blueprint inheritance,
- component instances,
- level/world composition,
- asset references,
- editor-exposed values,
- runtime/editor configuration,

verify the relevant state through Unreal Editor/MCP when necessary.

Do not assume a Blueprint or world is correct merely because the C++ source compiles.

---

## 29. Unreal MCP

Use Unreal MCP as part of the development and verification workflow when it provides information unavailable from source inspection.

Typical reasons include:

- inspecting Blueprint state,
- inspecting actors in a level,
- verifying component configuration,
- checking asset relationships,
- validating world state,
- confirming editor-side changes.

Do not use MCP blindly for information that is already clear from source.

Use the simplest reliable verification method.

---

## 30. Implementation Workflow

For a non-trivial Unreal change:

```text
Understand
    ↓
Inspect existing code/assets
    ↓
Read relevant architecture/specification
    ↓
Plan
    ↓
Implement
    ↓
Compile/build
    ↓
Verify runtime/editor state
    ↓
Check acceptance criteria
    ↓
Update documentation if architecture changed
```

Do not jump directly from a feature request to implementation when the feature affects multiple systems.

---

## 31. Minimal Change Principle

Prefer the smallest implementation that correctly satisfies the requirement and fits the existing architecture.

Before introducing:

- a new component,
- interface,
- subsystem,
- manager,
- library,
- data layer,

verify that an existing mechanism cannot reasonably handle the requirement.

Do not optimize for the number of abstractions.

Optimize for clear ownership and correct behavior.

---

## 32. Preserve Existing Behavior

When modifying an existing system:

1. understand its current behavior,
2. identify what must remain unchanged,
3. make the smallest required modification,
4. verify existing functionality after the change.

Do not casually refactor unrelated code during feature work.

Separate architectural refactoring from feature implementation when possible.

---

## 33. Debugging

When debugging an Unreal issue:

1. reproduce or understand the failure,
2. identify the owning system,
3. inspect the relevant state/lifecycle,
4. check networking authority if multiplayer is involved,
5. inspect Blueprint/editor state when relevant,
6. make the smallest targeted fix,
7. verify the original failure is resolved,
8. check for regressions.

Do not immediately rewrite a subsystem because of a localized bug.

---

## 34. Performance

Prefer correct and maintainable implementation first.

When performance matters:

1. identify the actual expensive path,
2. measure or otherwise establish the cost,
3. optimize the relevant bottleneck,
4. verify behavior after optimization.

Pay particular attention to:

- Tick,
- physics queries,
- overlap processing,
- network traffic,
- replicated properties,
- large actor/component populations,
- expensive Blueprint graphs.

Avoid speculative optimization.

---

## 35. Editor State vs Source State

Treat the appropriate source as authoritative for the type of information being changed.

```text
C++ / Config
    → source-controlled implementation/configuration

Blueprint / World / Editor state
    → Unreal Editor / MCP

Feature behavior
    → feature specification

Current architecture
    → ARCHITECTURE.md

Architectural rules
    → CONSTITUTION.md
```

When sources disagree, investigate rather than silently overwriting one with another.

---

## 36. Final Unreal Rule

> Use Unreal the way Unreal is designed to be used.

Do not fight the engine by recreating external architectural paradigms inside it.

Use strong engineering principles where they improve the game:

- clear responsibilities,
- modularity,
- composition,
- meaningful interfaces,
- explicit ownership,
- controlled dependencies,
- server authority,
- testable/reasonable state,
- deliberate replication.

But always adapt those principles to Unreal's actual runtime model, editor workflow and multiplayer architecture.