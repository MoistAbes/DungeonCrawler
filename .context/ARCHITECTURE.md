\# Architecture — DungeonCrawler



> Current-state description of the project's Unreal Engine architecture.

>

> This document describes \*\*how the project is structured today\*\* and the architectural boundaries that should be preserved when extending it.

>

> Project-wide architectural rules and priorities are defined in `CONSTITUTION.md`.

> Persistent cross-session AI context belongs in `MEMORY.md`.

> Expected behavior of individual features belongs in `.context/specs/`.



\---



\## 1. Project Overview



DungeonCrawler is an Unreal Engine 5.8 C++ multiplayer dungeon crawler.



The project uses:



\* C++ for core gameplay systems and reusable runtime logic,

\* Blueprints for actor composition, asset configuration, presentation and simple feature-specific wiring,

\* Unreal Interfaces for capability-based communication,

\* Actor Components for reusable gameplay behavior,

\* Unreal Delegates/Events for event-driven communication,

\* Unreal replication and RPCs for multiplayer gameplay,

\* Unreal MCP for inspecting and verifying editor-side state that cannot be reliably represented by source files alone.



The project currently uses a \*\*single Unreal runtime module\*\*:



```text

MyProject

```



The folders under `Source/MyProject/` represent architectural domains, not separate Unreal modules.



\---



\# 2. Source Structure



The main runtime source tree is organized into the following domains:



```text

Source/MyProject/

├── Dungeon/

├── Environment/

├── Logging/

├── Networking/

├── Player/

├── Shared/

├── UI/

├── MyProject.Build.cs

├── MyProject.cpp

└── MyProject.h

```



\## 2.1 Dungeon



```text

Dungeon/

├── Mechanisms/

├── Props/

└── Structure/

```



Contains dungeon-specific interactive gameplay such as:



\* mechanisms,

\* traps,

\* interactive props,

\* dungeon structural actors.



Examples include:



\* `AMechanismTrapBase`

\* `APistonTrap`

\* `AInteractivePropBase`

\* `AVolatileProp`

\* `ADungeonStructureBase`



Dungeon systems can use shared interfaces/components where appropriate instead of duplicating generic gameplay behavior.



\---



\## 2.2 Environment



```text

Environment/

├── Elements/

├── Kinetic/

└── Zones/

&#x20;   └── Shapes/

```



Contains environmental gameplay systems.



Important existing systems include:



\* status zones,

\* environmental effects,

\* kinetic/physics-related functionality.



Important classes/libraries include:



\* `AStatusZoneBase`

\* `ASurfaceSplashZone`

\* `AVolumetricStatusZone`

\* `UStatusZoneLibrary`

\* `UKineticForceLibrary`



Environment systems are expected to remain reusable where practical and should not become tightly coupled to individual dungeon actors.



\---



\## 2.3 Logging



Contains project-specific Unreal logging categories.



Existing categories include:



\* `LogDungeonInteraction`

\* `LogDungeonPhysics`

\* `LogDungeonNetwork`

\* `LogDungeonMechanisms`

\* `LogDungeonElements`



Logging should provide useful diagnostic information without becoming part of gameplay architecture.



\---



\## 2.4 Networking



Contains reusable networking-related helpers and infrastructure.



An existing example is:



\* `NetworkFunctionLibrary`



The networking domain should contain reusable networking functionality rather than feature-specific gameplay rules whenever possible.



\---



\## 2.5 Player



Contains the player-controlled gameplay domain.



The primary player actor is:



\* `APlayerCharacter`



Player functionality is largely composed from reusable components rather than implemented as one monolithic character class.



\---



\## 2.6 Shared



```text

Shared/

├── Components/

├── Enums/

└── Interfaces/

```



Contains reusable gameplay building blocks shared across multiple domains.



Important components include:



\* `UDamageableComponent`

\* `UInteractionComponent`

\* `UPhysicsCarryComponent`

\* `UStatusEffectComponent`



Important interfaces include:



\* `ICarryAnchorProviderInterface`

\* `IGrabbableInterface`

\* `IInteractableInterface`

\* `IMaterialProviderInterface`

\* `IMechanismReceiverInterface`

\* `IStatProviderInterface`



Shared code should only be placed here when it represents genuinely reusable functionality.



\---



\## 2.7 UI



Contains Unreal UMG-based UI functionality.



Examples include:



\* `PlayerHUD`

\* `PlayerHUDWidget`

\* `StatusIconWidget`

\* `StatBarWidget`



UI code should consume gameplay state rather than becoming the owner of gameplay rules.



\---



\# 3. Player Architecture



\## 3.1 Player Character



`APlayerCharacter` derives from `ACharacter`.



The character uses composition through multiple components, including functionality for:



\* interaction,

\* physics carrying,

\* damage/durability,

\* knockback,

\* status effects,

\* camera behavior,

\* carry/hold anchoring.



The player character also implements capability interfaces including:



\* `IMaterialProviderInterface`

\* `ICarryAnchorProviderInterface`



The character therefore acts primarily as the player-facing composition root for player functionality rather than owning every gameplay responsibility itself.



\---



\## 3.2 Player Components



Important player-related components include:



\* `InteractionComponent`

\* `PhysicsCarryComponent`

\* `DamageableComponent`

\* `KnockbackComponent`

\* `StatusEffectComponent`

\* `PlayerCameraComponent`

\* `HoldAnchorComponent`

\* Unreal camera/spring-arm components where applicable.



Components should own narrowly scoped behavior.



For example:



```text

PlayerCharacter

├── Interaction

├── Physics Carry

├── Damage / Durability

├── Knockback

├── Status Effects

├── Camera

└── Carry Anchor

```



A new player feature should generally be evaluated as a candidate for a component before adding substantial logic directly to `APlayerCharacter`.



This is not an absolute rule: behavior that is inherently part of character lifecycle, movement or character-level state may remain on the character.



\---



\# 4. Shared Gameplay Components



\## 4.1 Damageable Component



`UDamageableComponent` provides reusable damage/durability behavior.



It owns state such as:



\* current durability,

\* maximum durability.



It exposes operations and events related to damage and destruction.



The component also supports replicated gameplay state where required.



Damageable behavior should remain independent from specific actors whenever possible.



Actors that need custom consequences of damage should react to component events rather than duplicating damage logic.



\---



\## 4.2 Interaction Component



`UInteractionComponent` provides reusable interaction functionality.



It is intended to separate interaction detection/flow from the concrete actor that owns the component.



Interactive actors can expose interaction capabilities through:



```text

IInteractableInterface

```



This allows the interaction system to work with different actor implementations without requiring the interaction component to know every concrete actor class.



\---



\## 4.3 Physics Carry Component



`UPhysicsCarryComponent` owns the player-facing physics carry/manipulation flow.



Its current state model includes:



```text

None

RequestingGrab

Carrying

Releasing

```



The component contains networking operations including:



```text

Server\_RequestGrab

Server\_RequestForwardThrow

Server\_RequestDropOrSwing

Client\_GrabDenied

```



The component is responsible for the carry interaction state and its networked execution.



Concrete grabbable actors communicate through capabilities such as:



```text

IGrabbableInterface

```



and related anchor/carry interfaces.



This prevents the player character from needing specialized knowledge of every object that can be carried.



\---



\## 4.4 Status Effect Component



`UStatusEffectComponent` provides reusable status-effect state/behavior for actors that can receive status effects.



It is used as a shared gameplay component rather than embedding status state separately inside every affected actor.



Environmental systems such as Status Zones can interact with this capability without requiring knowledge of every concrete target actor.



\---



\# 5. Status Zone Architecture



Status Zones are environmental gameplay actors responsible for applying effects to actors inside defined areas.



The base class is:



```text

AStatusZoneBase

```



Current shape-specific implementations include:



```text

ASurfaceSplashZone

AVolumetricStatusZone

```



with the shape implementations located under:



```text

Environment/Zones/Shapes/

```



The base zone architecture handles responsibilities such as:



\* zone lifecycle,

\* duration,

\* overlap/target evaluation,

\* effect application,

\* environmental interactions,

\* relevant replicated state.



Shape-specific classes should primarily provide geometry/shape-specific behavior rather than duplicate the complete zone system.



Shared operations are also exposed through:



```text

UStatusZoneLibrary

```



\---



\# 6. Interfaces and Capability-Based Communication



The project uses Unreal Interfaces to represent capabilities.



Examples include:



```text

IGrabbableInterface

IInteractableInterface

IMechanismReceiverInterface

IMaterialProviderInterface

IStatProviderInterface

ICarryAnchorProviderInterface

```



The purpose of an interface is to allow a system to depend on a capability rather than a concrete actor implementation.



For example:



```text

Interaction System

&#x20;       │

&#x20;       ▼

IInteractableInterface

&#x20;       │

&#x20;  ┌────┴────┐

&#x20;  ▼         ▼

Switch     Door

```



The interaction system does not need separate knowledge of every concrete interactive actor.



Interfaces should not be introduced automatically.



A capability should become an interface when:



\* multiple implementations are expected,

\* a system genuinely needs to depend on a capability,

\* decoupling provides a meaningful architectural benefit.



A one-off behavior does not automatically require an interface.



\---



\# 7. Cast<T> Policy



`Cast<T>` is \*\*not globally forbidden\*\* in the project.



Unreal gameplay code sometimes legitimately needs to determine whether an object is a particular concrete Unreal type.



Casts are acceptable when they represent a justified concrete-type requirement, especially at Unreal/framework boundaries or inside functionality that explicitly operates on a known concrete type.



The architectural problem occurs when casts become the primary way systems discover and couple themselves to each other.



Prefer:



```text

Interface

Component

Delegate/Event

Explicit reference

```



when the dependency represents a reusable capability or architectural contract.



Avoid patterns such as:



```text

System A

&#x20;├── Cast<ActorTypeA>()

&#x20;├── Cast<ActorTypeB>()

&#x20;├── Cast<ActorTypeC>()

&#x20;└── special-case logic for every new actor

```



because this creates growing concrete-type coupling.



A practical rule is:



> \*\*Do not ban casts. Do not use casts as the architecture.\*\*



Existing code such as `UStatusZoneLibrary` may legitimately use a concrete cast when the operation specifically requires `AStatusZoneBase`.



\---



\# 8. Domain Communication



The preferred communication mechanisms depend on the relationship between systems.



\## 8.1 Direct References



Use direct references when one object genuinely owns or explicitly depends on another object.



Example:



```text

PlayerCharacter

&#x20;   │

&#x20;   └── owns/references Components

```



Direct references are preferable to artificial abstraction when the relationship is clear and stable.



\---



\## 8.2 Interfaces



Use interfaces for capability-based interaction.



Example:



```text

InteractionComponent

&#x20;       │

&#x20;       ▼

IInteractableInterface

&#x20;       │

&#x20;       ▼

Concrete Actor

```



\---



\## 8.3 Components



Use Actor Components when behavior should be reusable across multiple actor types.



Example:



```text

Actor A ──┐

Actor B ──┼──> UDamageableComponent

Actor C ──┘

```



\---



\## 8.4 Delegates and Events



Use events/delegates when a system needs to notify other systems without directly owning them.



Example:



```text

DamageableComponent

&#x20;       │

&#x20;       └── OnDestroyed

&#x20;               │

&#x20;       ┌───────┴───────┐

&#x20;       ▼               ▼

&#x20;      VFX             Gameplay

```



This avoids unnecessary direct dependencies.



\---



\# 9. Networking Architecture



The multiplayer model is \*\*server authoritative\*\*.



The client provides intent.



The server validates and owns authoritative gameplay state.



Conceptually:



```text

Client

&#x20; │

&#x20; │ input / intent

&#x20; ▼

Server

&#x20; │

&#x20; ├── validation

&#x20; ├── gameplay rules

&#x20; ├── state mutation

&#x20; └── replication

&#x20;      │

&#x20;      ▼

&#x20;   Clients

```



Examples include server RPCs in systems such as `UPhysicsCarryComponent`.



\## 9.1 Client Responsibilities



Clients may:



\* gather player input,

\* initiate requests,

\* provide interaction intent,

\* display replicated gameplay state,

\* perform appropriate local presentation.



Clients should not be treated as authoritative sources for shared gameplay state.



\---



\## 9.2 Server Responsibilities



The server owns:



\* authoritative gameplay decisions,

\* validation of client requests,

\* authoritative state mutation,

\* multiplayer interactions,

\* replication of relevant state.



A new multiplayer gameplay feature should explicitly define:



1\. what the client requests,

2\. what the server validates,

3\. what state the server owns,

4\. what state is replicated,

5\. what clients use for presentation.



\---



\# 10. Replication



Replication should be intentional.



Not every property needs replication.



For each replicated state, the implementation should have a gameplay reason for replication.



Relevant systems already use replicated state where required, including:



\* player gameplay components,

\* physics carry state,

\* status zones.



Replication logic should remain close to the system that owns the state.



Do not create a generic replication layer merely to avoid using Unreal's native replication mechanisms.



\---



\# 11. Blueprint Architecture



Blueprints are an important part of the project.



They are primarily used for:



\* actor composition,

\* asset configuration,

\* designer-facing values,

\* presentation,

\* simple event wiring,

\* feature-specific wiring that does not justify additional C++ complexity.



Blueprints are \*\*not restricted to presentation only\*\*.



At the same time, core reusable gameplay rules should generally live in C++ when they require:



\* authoritative multiplayer behavior,

\* reusable gameplay logic,

\* complex state,

\* non-trivial physics,

\* performance-sensitive execution,

\* strong code-level contracts.



A Blueprint should not become a second implementation of an existing C++ gameplay system.



Preferred relationship:



```text

C++ gameplay system

&#x20;       ▲

&#x20;       │

Blueprint composition/configuration

&#x20;       │

&#x20;       ▼

Unreal assets / presentation

```



Blueprints may orchestrate existing C++ functionality without taking ownership of the underlying domain rules.



\---



\# 12. Actor Composition



The project favors composition over deep inheritance for reusable gameplay behavior.



A typical actor may be composed from:



```text

Actor

├── Gameplay Component

├── Interaction Component

├── Damageable Component

├── Physics Component

└── Presentation / Blueprint configuration

```



Inheritance remains appropriate when the Unreal type hierarchy itself represents a meaningful relationship.



For example:



```text

AActor

&#x20; └── AInteractivePropBase

&#x20;       └── AVolatileProp

```



or:



```text

AActor

&#x20; └── AMechanismTrapBase

&#x20;       └── APistonTrap

```



The goal is not to eliminate inheritance.



The goal is to avoid deep inheritance trees where unrelated responsibilities accumulate in base classes.



\---



\# 13. Dungeon Structure and Props



Dungeon-specific actors can use shared capabilities and components.



Examples:



```text

ADungeonStructureBase

AInteractivePropBase

AVolatileProp

AMechanismTrapBase

APistonTrap

```



A base class should contain behavior genuinely shared by its descendants.



Generic gameplay functionality such as:



\* damage,

\* interaction,

\* status effects,

\* carrying,



should use shared components/interfaces where appropriate instead of being independently reimplemented by each dungeon actor.



\---



\# 14. Data and Configuration



The project contains shared configuration structures such as:



\* `FCarrySocketConfig`

\* `FZoneEffectConfig`



Configuration should be separated from hard-coded gameplay behavior when the same behavior needs meaningful tuning or reuse.



Use Unreal-native data-driven mechanisms where appropriate, including:



\* `USTRUCT` configuration,

\* Data Assets,

\* Data Tables,

\* Blueprint-exposed properties.



Do not introduce a data abstraction solely for the sake of abstraction.



The appropriate mechanism depends on:



\* whether designers need to edit it,

\* whether values are shared,

\* whether values are runtime state or static configuration,

\* whether network replication is required.



\---



\# 15. Input Architecture



The project uses Unreal Enhanced Input.



Current input assets include actions for functionality such as:



\* Move,

\* Look,

\* Jump,

\* Interact,

\* Throw,

\* Zoom.



Input should represent \*\*player intent\*\*.



Gameplay systems should not become tightly coupled to raw input mappings when the input can instead be translated into a gameplay operation.



For multiplayer functionality:



```text

Input

&#x20; ↓

Intent

&#x20; ↓

Gameplay System

&#x20; ↓

Server Authority

```



This keeps input configuration separate from authoritative gameplay rules.



\---



\# 16. UI Architecture



The UI uses Unreal UMG.



Examples include:



```text

PlayerHUD

PlayerHUDWidget

StatusIconWidget

StatBarWidget

```



The UI should primarily:



\* display gameplay state,

\* react to gameplay events,

\* trigger presentation changes.



UI should not become the authoritative owner of gameplay state.



For example, a health bar should reflect health owned by a gameplay system/component rather than storing a second authoritative health value inside the widget.



Preferred flow:



```text

Gameplay State

&#x20;     │

&#x20;     ▼

Event / Observable State

&#x20;     │

&#x20;     ▼

UI Widget

&#x20;     │

&#x20;     ▼

Presentation

```



\---



\# 17. Libraries and Shared Helpers



The project contains reusable Unreal helper libraries, including:



\* `UKineticForceLibrary`

\* `UStatusZoneLibrary`

\* `NetworkFunctionLibrary`



These should remain focused on reusable operations.



A library should not become a hidden global service containing unrelated gameplay state.



When a helper starts accumulating state, lifecycle responsibilities or ownership semantics, it should be evaluated as a possible component, actor, subsystem or dedicated gameplay object instead.



\---



\# 18. Logging Architecture



Project logging uses Unreal's logging system and project-specific categories.



Logging should be used for:



\* debugging gameplay behavior,

\* diagnosing networking issues,

\* tracing complex interactions,

\* investigating physics problems,

\* reporting meaningful failure conditions.



Logging should not be used as a substitute for proper state communication.



Avoid excessive logging inside high-frequency execution paths unless there is a clear diagnostic purpose.



\---



\# 19. Architectural Boundaries



The following boundaries are currently important.



\### Gameplay ↔ Presentation



Gameplay state belongs to gameplay systems.



Presentation consumes that state.



\### Client ↔ Server



Client input is intent.



The server owns authoritative multiplayer gameplay.



\### Shared ↔ Feature-Specific



Shared code must represent genuinely reusable functionality.



Feature-specific logic should remain in its feature domain.



\### Capability ↔ Implementation



Interfaces represent capabilities.



Concrete actor classes provide implementations.



\### Configuration ↔ Runtime State



Configuration describes how a system should behave.



Runtime state belongs to the object/system executing that behavior.



\### C++ ↔ Blueprint



C++ provides reusable/core gameplay behavior.



Blueprint provides composition, configuration, presentation and simple orchestration where appropriate.



\---



\# 20. Architectural Constraints



The following constraints currently guide implementation.



\## 20.1 Do Not Reimplement Existing Systems



Before introducing a new:



\* interaction system,

\* damage system,

\* carry system,

\* status-effect system,

\* kinetic-force helper,

\* networking helper,



inspect the existing implementation first.



Extend or reuse it when possible.



\---



\## 20.2 Do Not Introduce Premature Abstractions



Do not automatically create:



```text

Service

Manager

Factory

Repository

Provider

Controller

Coordinator

```



or similar abstraction layers.



An abstraction should solve a concrete problem such as:



\* multiple implementations,

\* dependency isolation,

\* lifecycle ownership,

\* reusable behavior,

\* testability,

\* clear architectural boundary.



\---



\## 20.3 Avoid God Objects



Watch especially for growth of:



\* `APlayerCharacter`,

\* base actor classes,

\* generic managers,

\* Blueprint graphs.



When a class starts accumulating unrelated responsibilities, evaluate whether the behavior belongs in:



\* an Actor Component,

\* an Interface,

\* a dedicated Actor,

\* a Subsystem,

\* a library,

\* or another domain-specific object.



\---



\## 20.4 Tick Is Not Free



`Tick` should have a clear reason to exist.



Prefer where appropriate:



\* delegates,

\* overlap events,

\* timers,

\* state transitions,

\* explicit gameplay events.



Tick remains valid when the behavior genuinely requires continuous per-frame evaluation.



\---



\# 21. State Machines



Complex mutually exclusive behavior should use explicit state modeling rather than scattered booleans.



`UPhysicsCarryComponent` is an existing example:



```text

None

&#x20; ↓

RequestingGrab

&#x20; ↓

Carrying

&#x20; ↓

Releasing

&#x20; ↓

None

```



When a system begins accumulating multiple interacting flags, evaluate whether those flags actually represent a state machine.



\---



\# 22. Performance Considerations



Gameplay architecture should remain conscious of runtime cost.



Pay particular attention to:



\* per-frame Tick logic,

\* physics queries,

\* overlap processing,

\* network RPC frequency,

\* replicated state,

\* large numbers of actors/components,

\* expensive Blueprint execution.



Performance optimizations should be based on actual needs rather than speculative micro-optimization.



The preferred order is:



```text

Correct behavior

→

Correct architecture

→

Measure

→

Optimize where necessary

```



\---



\# 23. Specifications and Architecture



Architecture describes \*\*how the project is structured\*\*.



Feature specifications describe \*\*what a feature should do\*\*.



A feature specification should not silently redefine global architecture.



For a non-trivial feature, the expected relationship is:



```text

CONSTITUTION

&#x20;    ↓

ARCHITECTURE

&#x20;    ↓

FEATURE SPECIFICATION

&#x20;    ↓

IMPLEMENTATION PLAN

&#x20;    ↓

TASKS

&#x20;    ↓

C++ / Blueprint / Content

&#x20;    ↓

VERIFICATION

```



If a feature appears to require a change to the existing architecture, the architectural impact should be made explicit before implementation.



\---



\# 24. Unreal MCP and Editor State



Not all Unreal project state is reliably visible from source code.



Important examples include:



\* Blueprint graph configuration,

\* Blueprint inheritance,

\* actor/component instances,

\* level/world composition,

\* editor-exposed property values,

\* asset references,

\* runtime/editor configuration.



Unreal MCP should therefore be used when source inspection alone cannot verify the required state.



The repository is the source of truth for source-controlled code/configuration.



Unreal Editor/MCP is the source of truth for editor-side state that cannot be reliably represented by those source files.



\---



\# 25. Current Architectural Risks



These are areas to monitor, not automatic problems.



\## Player Character Growth



`APlayerCharacter` already coordinates multiple components.



Future player features should avoid moving unrelated systems directly into the character.



\## Damageable Component Growth



`UDamageableComponent` should not become a generic container for every possible player/actor resource.



If durability, health, armor, shield, stamina and similar systems develop substantially different rules, they should be evaluated as separate responsibilities.



\## Status Zone Complexity



`AStatusZoneBase` and related environmental systems contain significant gameplay behavior.



Future extensions should preserve the separation between:



\* zone lifecycle,

\* geometry,

\* effect configuration,

\* effect application,

\* environmental interactions.



\## Shared Folder Growth



`Shared/` should not become a dumping ground.



A class belongs there because multiple domains genuinely depend on it, not because it is convenient to place it there.



\## Blueprint Complexity



Blueprints can legitimately contain composition and simple feature wiring.



However, increasingly complex gameplay rules should be evaluated for migration into reusable C++ systems when that improves:



\* reuse,

\* multiplayer correctness,

\* maintainability,

\* state management,

\* performance.



\---



\# 26. How This Document Should Be Maintained



`ARCHITECTURE.md` describes the \*\*current architecture\*\*, not the desired future architecture.



Update it when a meaningful architectural change occurs, such as:



\* a new reusable gameplay system,

\* a new architectural domain,

\* a new major component,

\* a significant networking pattern,

\* a new cross-domain dependency,

\* a changed responsibility boundary,

\* a meaningful change to Blueprint/C++ responsibilities.



Do not update it for:



\* ordinary bug fixes,

\* individual variable changes,

\* temporary experiments,

\* completed TODOs,

\* minor implementation details.



When implementation and documentation disagree:



1\. determine which reflects the intended architecture,

2\. verify the actual implementation,

3\. correct the architecture or implementation as appropriate,

4\. update this document so it describes the resulting real architecture.



\---



\# 27. Architectural Source of Truth



The project uses the following hierarchy:



\### `CONSTITUTION.md`



Defines architectural principles, priorities and non-negotiable rules.



\### `ARCHITECTURE.md`



Defines the current structural architecture of the repository.



\### `.context/specs/`



Defines expected behavior of individual features.



\### C++ / Blueprint / Content



Defines the actual implementation.



\### Unreal MCP



Provides verification and visibility into Unreal Editor state that cannot be reliably determined from source files alone.



When these sources disagree, do not silently choose one.



Investigate the discrepancy and make the intended state explicit.



\---



\# 28. Core Architectural Direction



The project follows a simple principle:



> \*\*Use Unreal-native architecture to build the game, while keeping responsibilities explicit and systems modular.\*\*



In practice this means:



```text

Composition over unnecessary inheritance

Capabilities over concrete-type coupling

Events over unnecessary direct dependencies

C++ for reusable/core gameplay rules

Blueprints for composition, configuration and presentation

Server authority for multiplayer gameplay

Explicit replication

Data-driven configuration where useful

Existing systems before duplicate systems

Simple solutions before abstraction

Specifications before complex implementation

Verification before declaring a feature complete

```



The architecture should serve the game.



It should evolve when the game creates a real need for it, not because an abstract architecture pattern suggests that it should.



