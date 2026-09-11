\# DungeonCrawler Constitution



\*\*Version:\*\* 1.0

\*\*Status:\*\* Active

\*\*Project:\*\* DungeonCrawler

\*\*Engine:\*\* Unreal Engine 5.8



\---



\## 1. Purpose



This document defines the engineering principles and architectural rules that govern development of DungeonCrawler.



The purpose of this Constitution is to ensure that the project remains:



\* maintainable,

\* understandable,

\* modular,

\* multiplayer-safe,

\* idiomatic to Unreal Engine,

\* suitable for AI-assisted development,

\* resistant to unnecessary architectural complexity.



These rules apply to human and AI-generated code, Blueprint changes, gameplay systems, networking, and architectural decisions.



When a new feature conflicts with an existing rule, the conflict MUST be identified explicitly before implementation.



\---



\# 2. Core Philosophy



\## Principle 1 — Unreal First



\*\*Unreal Engine conventions take precedence over patterns imported from Java, Spring, REST APIs, or other backend technologies.\*\*



The project may use general software engineering principles such as SOLID, SRP, composition, interfaces, encapsulation, and separation of concerns.



However, these principles MUST be adapted to Unreal rather than mechanically reproduced from backend architecture.



AI MUST NOT introduce a backend-style abstraction merely because it is familiar from Java/Spring.



Before introducing concepts such as:



\* Service

\* Manager

\* Repository

\* Factory

\* Provider

\* Controller

\* Dependency Injection layer

\* Registry



AI SHOULD first determine whether an Unreal-native solution is more appropriate, such as:



\* Actor Components,

\* Actors,

\* UObject-based systems,

\* Unreal Interfaces,

\* Delegates,

\* Subsystems,

\* Data Assets,

\* Data Tables,

\* Gameplay Tags,

\* replicated properties,

\* RPCs,

\* existing engine systems.



\---



\# 3. Responsibility and Modularity



\## Principle 2 — Single Responsibility



\*\*Every class, component, subsystem, or gameplay system MUST have a clear primary responsibility.\*\*



A class SHOULD have one primary reason to change.



Gameplay responsibilities SHOULD be separated into dedicated systems where practical.



For example, a player character SHOULD NOT become responsible for every gameplay feature.



Prefer:



```text

PlayerCharacter

├── InteractionComponent

├── PhysicsCarryComponent

├── DamageableComponent

├── StatusEffectComponent

└── Movement-related functionality

```



over a single class containing unrelated gameplay systems.



SRP MUST NOT be interpreted as "one class per function."



Small, artificial abstractions that provide no meaningful separation SHOULD NOT be created merely to satisfy SRP.



\---



\## Principle 3 — Composition Before Inheritance



\*\*Gameplay capabilities SHOULD generally be implemented through composition rather than deep inheritance hierarchies.\*\*



Actor Components SHOULD be preferred when a capability can exist independently and may be reused by multiple actors.



Examples:



\* Damage

\* Interaction

\* Carrying

\* Status effects

\* Inventory

\* Health

\* Equipment

\* Abilities



Inheritance IS appropriate when objects genuinely share:



\* Unreal lifecycle behavior,

\* common state,

\* common implementation,

\* or meaningful polymorphic behavior.



The project MUST NOT ban inheritance.



The goal is to avoid deep and fragile gameplay inheritance hierarchies.



\---



\# 4. Contracts and Communication



\## Principle 4 — Interfaces Represent Capabilities



\*\*Interfaces SHOULD be used to define stable contracts between systems that should not depend on concrete implementations.\*\*



For example:



```text

IInteractable

IGrabbable

IDamageable

ICarryAnchorProvider

```



A gameplay system SHOULD depend on the capability it requires rather than the concrete class implementing that capability.



Prefer:



```text

Player

&#x20;  ↓

IInteractable

&#x20;  ↓

Door / Switch / Chest / Lever

```



over:



```text

Player

&#x20;  ↓

Cast<ADoor>()

Cast<ASwitch>()

Cast<AChest>()

```



Interfaces MUST NOT be introduced solely for the sake of having an interface.



\---



\## Principle 5 — Minimize Coupling



Systems SHOULD know as little as reasonably possible about concrete implementations of other systems.



Prefer communication through:



1\. interfaces,

2\. delegates/events,

3\. well-defined component APIs,

4\. replicated state,

5\. explicit references where appropriate.



Direct references to concrete classes are acceptable when the relationship is structurally meaningful and does not create harmful coupling.



The goal is not zero coupling.



The goal is \*\*intentional coupling\*\*.



\---



\## Principle 6 — Event-Driven Communication



\*\*Events and delegates SHOULD be preferred when one system needs to notify another system without owning or controlling it.\*\*



Examples:



```text

OnHealthChanged

OnDurabilityChanged

OnDestroyed

OnInteractionStarted

OnInteractionCompleted

OnStatusEffectApplied

```



A system SHOULD NOT directly manipulate unrelated systems when an event/contract provides a cleaner boundary.



Events MUST NOT be used simply to make straightforward synchronous logic more complicated.



\---



\# 5. C++ and Blueprint Responsibilities



\## Principle 7 — C++ Owns Gameplay Rules



Core gameplay logic SHOULD live in C++.



C++ SHOULD own:



\* gameplay rules,

\* authoritative state,

\* networking,

\* reusable gameplay systems,

\* complex calculations,

\* state machines,

\* validation,

\* performance-sensitive logic,

\* reusable components.



Blueprints SHOULD primarily own:



\* actor composition,

\* visual presentation,

\* asset configuration,

\* designer-facing parameters,

\* simple event wiring,

\* animation/FX integration,

\* level-specific behavior.



Blueprints MAY contain gameplay logic when the behavior is simple, local, and intentionally designer-facing.



AI MUST NOT move complex gameplay logic into Blueprint merely because Blueprint is easier to edit.



AI MUST NOT move everything into C++ merely because C++ is considered "more professional."



The boundary MUST be chosen according to responsibility and maintainability.



\---



\# 6. Networking and Multiplayer



\## Principle 8 — Server Authority



\*\*The server is authoritative over multiplayer gameplay state.\*\*



Clients MAY:



\* provide input,

\* request actions,

\* predict appropriate local behavior where justified,

\* display replicated state.



Clients MUST NOT be treated as authoritative sources of gameplay truth.



Examples of authoritative state include:



\* damage,

\* health,

\* durability,

\* item ownership,

\* object pickup,

\* object throwing,

\* status effects,

\* gameplay interactions,

\* world state.



Client input represents \*\*intent\*\*, not authority.



\---



\## Principle 9 — Explicit Replication



Every multiplayer gameplay system MUST have an intentional replication strategy.



For replicated state, AI MUST be able to answer:



1\. Who owns the state?

2\. Who is allowed to modify it?

3\. How is it replicated?

4\. What happens when the client is out of sync?

5\. What happens when the client sends an invalid request?

6\. Is client prediction required?



Replication MUST NOT be added mechanically.



If a property or behavior does not need replication, it SHOULD remain local.



\---



\## Principle 10 — Validate Client Requests



Server RPCs MUST treat client-provided gameplay requests as untrusted intent.



The server MUST validate requests when the action affects authoritative gameplay state.



Examples:



```text

Client requests grab

&#x20;       ↓

Server validates

&#x20;       ↓

Server performs grab

&#x20;       ↓

Replicated state updates

```



The client MUST NOT be allowed to establish authoritative gameplay state merely by sending an RPC.



\---



\# 7. Unreal Architecture



\## Principle 11 — Respect Unreal Lifecycles



AI MUST respect Unreal Engine object and actor lifecycles.



Systems MUST be placed in the Unreal type that best matches their lifecycle and responsibility.



Before introducing a new global system, AI SHOULD evaluate whether the behavior belongs in:



\* Actor,

\* Actor Component,

\* UObject,

\* GameMode,

\* GameState,

\* PlayerController,

\* PlayerState,

\* GameInstance,

\* World/Engine Subsystem,

\* existing Unreal framework functionality.



Global state MUST NOT be introduced casually.



\---



\## Principle 12 — Avoid God Objects



No single class SHOULD become responsible for unrelated gameplay systems.



The following are warning signs:



\* PlayerCharacter knows about every gameplay system.

\* GameMode controls unrelated world behavior.

\* GameInstance becomes a global service container.

\* A Manager class becomes responsible for multiple unrelated domains.

\* A Component becomes a generic dumping ground for gameplay logic.



When a class grows across multiple unrelated responsibilities, AI SHOULD propose decomposition before continuing.



\---



\# 8. Casting Rules



\## Principle 13 — No Cast-Driven Domain Logic



\*\*`Cast<T>` MUST NOT be used as the primary mechanism for making gameplay architecture decisions.\*\*



Bad pattern:



```cpp

if (ADoor\* Door = Cast<ADoor>(Actor))

{

&#x20;   ...

}

else if (ASwitch\* Switch = Cast<ASwitch>(Actor))

{

&#x20;   ...

}

```



Prefer contracts:



```text

IInteractable

IGrabbable

IDamageable

```



However, Unreal Engine framework-bound casts MAY be used when required by the engine API.



Examples may include obtaining:



\* PlayerController from a Pawn,

\* EnhancedInputComponent from an input setup callback,

\* other Unreal framework types at an engine boundary.



Such casts SHOULD:



\* remain localized,

\* not contain domain decisions,

\* not be used when an appropriate interface/contract is available.



Therefore, the rule is:



> \*\*No cast-driven domain architecture, not "zero Cast<T> anywhere in the project."\*\*



\---



\# 9. Data and Configuration



\## Principle 14 — Data-Driven Where Appropriate



Gameplay values that are expected to change during balancing SHOULD be separated from hardcoded gameplay logic.



Prefer appropriate Unreal data mechanisms such as:



\* Data Assets,

\* Data Tables,

\* Structs,

\* Gameplay Tags,

\* configurable properties.



Examples:



```text

Damage

Cooldown

Movement Speed

Status Duration

Carry Force

Throw Strength

Zone Radius

```



AI MUST NOT create a data-driven system for every numeric constant.



Simple implementation constants MAY remain in code when they are truly implementation details.



\---



\# 10. Reuse Before Duplication



\## Principle 15 — Inspect Existing Systems First



Before creating a new gameplay system, AI MUST inspect the existing project architecture.



AI SHOULD look for:



\* existing Components,

\* Interfaces,

\* Enums,

\* Structs,

\* base classes,

\* utility systems,

\* existing Blueprint assets,

\* existing gameplay behavior.



If an existing system already provides most of the required functionality, it SHOULD be extended rather than duplicated.



Duplicate systems MUST NOT be introduced merely because they are easier to implement independently.



\---



\# 11. Abstraction Rules



\## Principle 16 — No Premature Abstraction



\*\*Abstraction MUST solve a real problem.\*\*



AI MUST NOT introduce abstractions solely because:



\* "SOLID says so,"

\* "Java usually does this,"

\* "this might be useful someday,"

\* "we may need another implementation later."



Before introducing an abstraction, AI SHOULD identify at least one concrete benefit:



\* multiple real implementations,

\* reduced coupling,

\* reusable behavior,

\* independently testable logic,

\* clear architectural boundary,

\* required Unreal integration.



YAGNI applies.



\---



\## Principle 17 — Prefer Simple Solutions



When two solutions satisfy the requirements, prefer the simpler solution.



The project SHOULD optimize for:



```text

clarity

>

unnecessary abstraction

```



and:



```text

working gameplay

>

architectural perfection

```



Architecture exists to support the game, not the other way around.



\---



\# 12. Game Development Constraints



\## Principle 18 — Gameplay Performance Matters



Gameplay systems MUST consider runtime cost.



AI SHOULD pay particular attention to:



\* Tick usage,

\* timers,

\* collision queries,

\* overlap checks,

\* physics operations,

\* network replication,

\* spawning/destruction,

\* large numbers of actors,

\* repeated allocations,

\* expensive Blueprint execution.



A Tick function MUST have a clear reason to exist.



If an event, timer, delegate, or state change can replace continuous Tick processing, that alternative SHOULD be considered.



Optimization MUST NOT be premature.



However, obviously expensive patterns SHOULD NOT be introduced without justification.



\---



\## Principle 19 — State Machines for Complex Stateful Behavior



When a gameplay system has multiple mutually exclusive states and transitions, an explicit state machine SHOULD be preferred over scattered boolean flags.



For example:



```text

None

&#x20; ↓

RequestingGrab

&#x20; ↓

Carrying

&#x20; ↓

Releasing

```



A system SHOULD NOT accumulate contradictory state such as:



```text

bIsGrabbing

bIsCarrying

bIsReleasing

bIsThrowing

bHasObject

bIsPendingGrab

```



when a clearly defined state machine would better represent the behavior.



Simple two-state behavior does not require a formal state machine.



\---



\# 13. AI Development Rules



\## Principle 20 — Understand Before Modifying



Before making a non-trivial change, AI MUST inspect:



1\. relevant architecture documentation,

2\. relevant existing code,

3\. related Components/Interfaces,

4\. relevant Blueprint assets when applicable,

5\. networking implications,

6\. existing specifications.



AI MUST NOT blindly create new files or systems without understanding the existing architecture.



\---



\## Principle 21 — Specification Before Complex Implementation



Non-trivial gameplay features SHOULD follow this workflow:



```text

Specification

&#x20;     ↓

Architecture / Design

&#x20;     ↓

Implementation Plan

&#x20;     ↓

Tasks

&#x20;     ↓

Implementation

&#x20;     ↓

Verification

```



A feature SHOULD NOT be implemented directly from a vague request when it introduces significant gameplay, networking, physics, UI, or architectural behavior.



Small fixes MAY skip formal specification when the intent and impact are obvious.



\---



\## Principle 22 — AI Must Not Silently Change Architecture



AI MUST NOT silently:



\* introduce a new architectural pattern,

\* replace an existing system,

\* create a new global manager,

\* move responsibilities between C++ and Blueprint,

\* change networking authority,

\* remove existing abstractions,



merely to make implementation easier.



If the implementation requires an architectural deviation, AI MUST explain:



1\. what is changing,

2\. why it is necessary,

3\. what alternatives were considered,

4\. what consequences it has.



\---



\## Principle 23 — Unreal MCP Is Part of the Development Loop



When a task affects Unreal Editor state, Blueprint assets, placed actors, components, world configuration, or gameplay behavior that cannot be reliably verified from source code alone, AI SHOULD use Unreal MCP to inspect or verify the actual Unreal project state.



Source code inspection MUST NOT be treated as proof that the corresponding Unreal asset or world state is correct.



When relevant:



```text

Code

&#x20;+

Blueprint

&#x20;+

Editor/World State

&#x20;=

Feature

```



All relevant parts SHOULD be verified.



\---



\# 14. Verification



\## Principle 24 — Done Means Verified



A feature is NOT considered complete merely because:



\* code was written,

\* compilation succeeded,

\* an asset was created,

\* or the AI reports success.



Completion SHOULD be based on explicit acceptance criteria.



Verification MAY include:



\* compilation,

\* automated tests,

\* Unreal Editor inspection,

\* Blueprint inspection,

\* PIE testing,

\* multiplayer testing,

\* replication verification,

\* physics verification,

\* manual gameplay testing.



For multiplayer features, both server and client behavior SHOULD be considered.



\---



\## Principle 25 — Acceptance Criteria Are the Source of Truth



Every non-trivial feature SHOULD have explicit acceptance criteria.



Acceptance criteria SHOULD describe observable behavior.



Prefer:



```text

Given a player is carrying a crate,

when the player releases the carry input,

then the server releases the crate

and all clients observe the resulting state.

```



over:



```text

Implement CarryRelease().

```



The first describes behavior.



The second describes an implementation detail.



\---



\# 15. Documentation and Project Memory



\## Principle 26 — Documentation Describes Intent



Project documentation SHOULD explain:



\* why a system exists,

\* what responsibility it owns,

\* important architectural decisions,

\* constraints,

\* invariants,

\* interactions with other systems.



Documentation SHOULD NOT become a copy of the source code.



\---



\## Principle 27 — Keep Project Memory Small



Project memory MUST remain concise enough for an AI agent to consume effectively.



Long historical information SHOULD be moved into appropriate specifications, decision records, or documentation.



`MEMORY.md` SHOULD primarily contain:



\* current architectural context,

\* important persistent decisions,

\* current project state,

\* constraints that AI must remember.



It SHOULD NOT become a complete changelog.



\---



\# 16. Domain Boundaries



The project SHOULD maintain clear domain boundaries.



Current primary domains include:



```text

Dungeon

Environment

Player

UI

Shared

Networking

Logging

```



New gameplay functionality SHOULD be placed in the domain that owns the responsibility.



Cross-domain communication SHOULD use explicit contracts rather than hidden dependencies.



If a new feature does not clearly belong to an existing domain, AI SHOULD identify the architectural decision before implementation.



\---



\# 17. Change Management



\## Principle 28 — Small, Reversible Changes



Changes SHOULD be kept as small and focused as practical.



A single task SHOULD preferably have:



\* one clear goal,

\* limited architectural scope,

\* explicit verification,

\* an understandable diff.



Large refactors SHOULD NOT be mixed with unrelated gameplay features unless necessary.



\---



\## Principle 29 — Preserve Working Gameplay



Existing working behavior MUST be preserved unless changing it is an explicit part of the requirement.



AI SHOULD avoid unrelated refactoring during feature implementation.



If an existing architectural problem blocks implementation, AI SHOULD report it explicitly rather than silently rewriting unrelated code.



\---



\# 18. Decision Priority



When principles conflict, use the following priority order:



```text

1\. Unreal Engine correctness

2\. Multiplayer correctness and server authority

3\. Gameplay correctness

4\. Project architecture and maintainability

5\. Performance

6\. Reusability

7\. Abstraction elegance

```



A simpler architecture that correctly implements the game is preferable to a theoretically elegant architecture that increases risk.



\---



\# 19. Practical Decision Framework



When deciding how to implement a new feature, AI SHOULD ask:



\### 1. What owns this behavior?



```text

Actor?

Component?

Subsystem?

PlayerController?

GameState?

Other?

```



\### 2. Is this a capability?



If yes, consider an Actor Component.



\### 3. Does another system need a stable contract?



If yes, consider an Unreal Interface.



\### 4. Does something need to be notified?



If yes, consider a Delegate/Event.



\### 5. Is this shared/global state?



If yes, consider whether a Subsystem or existing Unreal lifecycle object is actually appropriate.



\### 6. Is this configuration?



If yes, consider a Data Asset, Data Table, Struct, or configurable property.



\### 7. Is this multiplayer state?



If yes, define authority and replication before implementation.



\### 8. Is this complex stateful behavior?



If yes, consider an explicit state machine.



\### 9. Am I creating an abstraction because I need it?



If no, do not create it.



\### 10. Can I reuse something that already exists?



Always check first.



\---



\# 20. Anti-Patterns



The following patterns SHOULD be treated as architectural warning signs:



\### Backend Architecture Leakage



```text

Actor

&#x20;↓

Controller

&#x20;↓

Service

&#x20;↓

Repository

&#x20;↓

Manager

```



when Unreal-native architecture would be simpler.



\### God Object



```text

PlayerCharacter

&#x20;├── Combat

&#x20;├── Inventory

&#x20;├── Interaction

&#x20;├── Status

&#x20;├── UI

&#x20;├── Networking

&#x20;├── Physics

&#x20;└── World management

```



\### Cast-Driven Architecture



```cpp

Cast<ADoor>()

Cast<ASwitch>()

Cast<AChest>()

Cast<ACrate>()

```



used to determine gameplay behavior instead of capability contracts.



\### Global Everything



```text

GlobalManager

GlobalState

GlobalService

GlobalRegistry

GlobalSubsystem

```



without a concrete need.



\### Premature Framework Building



Creating an internal framework before there are multiple real gameplay systems requiring it.



\### Blueprint Dumping Ground



Putting large amounts of complex gameplay logic into Blueprint because it is faster to prototype.



\### C++ Dogmatism



Moving simple designer-facing configuration and presentation logic into C++ solely because "real code belongs in C++."



\---



\# 21. Definition of Architectural Quality



A high-quality DungeonCrawler system should generally be:



```text

Understandable

&#x20;    +

Modular

&#x20;    +

Unreal-native

&#x20;    +

Server-authoritative

&#x20;    +

Testable

&#x20;    +

Reusable where justified

&#x20;    +

Simple

```



Not:



```text

Abstract

&#x20;    +

Generic

&#x20;    +

Enterprise-like

&#x20;    +

Over-engineered

```



The project is a game, not a Spring application.



\---



\# 22. AI Architectural Rule



When uncertain, AI MUST prefer the following process:



```text

Understand the gameplay requirement

&#x20;           ↓

Inspect existing project architecture

&#x20;           ↓

Identify the Unreal-native solution

&#x20;           ↓

Check multiplayer implications

&#x20;           ↓

Choose the simplest maintainable design

&#x20;           ↓

Implement

&#x20;           ↓

Verify in Unreal

&#x20;           ↓

Document important decisions

```



AI MUST NOT optimize for architectural novelty.



AI MUST optimize for \*\*correct, understandable, maintainable gameplay\*\*.



\---



\# 23. Constitution Evolution



This Constitution is versioned.



Rules MAY evolve as the team gains experience with Unreal Engine and the project's architecture matures.



A rule SHOULD be changed when:



\* practical Unreal experience demonstrates that it is incorrect,

\* the project repeatedly encounters a problem not covered by the Constitution,

\* a rule creates unnecessary complexity,

\* Unreal-native best practices change,

\* the game's requirements change significantly.



The Constitution MUST NOT be changed merely to justify a convenient implementation.



Architectural changes SHOULD be accompanied by a short decision record explaining why the previous rule was insufficient.



\---



\# 24. Final Principle



> \*\*Build the game first. Build the architecture that the game actually needs.\*\*



DungeonCrawler should benefit from strong software engineering practices without becoming a backend application disguised as a game.



Use the strengths of Java/Spring thinking:



\* clean responsibilities,

\* contracts,

\* modularity,

\* separation of concerns,

\* explicit boundaries,

\* maintainability.



Combine them with the strengths of Unreal:



\* composition through Actors and Components,

\* Unreal lifecycle,

\* Interfaces,

\* Delegates,

\* replication,

\* server authority,

\* Blueprint composition,

\* data-driven design,

\* physics and gameplay systems.



The objective is not to make Unreal behave like Java.



The objective is to use \*\*good engineering principles in an idiomatic Unreal architecture\*\*.



