\# Project Memory — DungeonCrawler



> Persistent cross-session context only.

>

> Detailed architecture belongs in `ARCHITECTURE.md`.

> Feature behavior belongs in `.context/specs/`.

> Project rules belong in `CONSTITUTION.md`.



\---



\## 1. Project Context



\* Unreal Engine 5.8 C++ multiplayer dungeon crawler.

\* Current networking model is server-authoritative co-op.

\* The project is developed with AI assistance and Unreal MCP.



\---



\## 2. Persistent Architectural Decisions



\* Prefer idiomatic Unreal Engine architecture over blindly applying Java/Spring patterns.

\* Prefer composition, Actor Components, Unreal Interfaces, Events/Delegates, Data Assets and other Unreal-native mechanisms where appropriate.

\* Core gameplay rules and reusable systems should primarily live in C++.

\* Blueprints are primarily used for composition, presentation, asset configuration and simple event wiring.

\* Client input represents intent; the server owns authoritative gameplay state.

\* Reuse existing systems before creating parallel implementations.

\* Do not introduce abstractions without a concrete architectural or gameplay need.



\---



\## 3. Important Existing Systems



The following systems already exist and should be reused or extended when applicable:



\* `UPhysicsCarryComponent` — physics carrying/manipulation.

\* `UInteractionComponent` — interaction detection and interaction flow.

\* `UDamageableComponent` — shared damage/durability behavior.

\* `UStatusEffectComponent` — status-effect state.

\* `AStatusZoneBase` and derived status-zone actors — environmental status zones.

\* `UKineticForceLibrary` — shared kinetic/physics force operations.

\* `UStatusZoneLibrary` — status-zone/effect delivery operations.



Detailed contracts and implementation belong in `ARCHITECTURE.md` and feature specifications.



\---



\## 4. AI Development Context



The developer has strong Java/Spring experience but is learning Unreal Engine and C++.



When making an Unreal-specific architectural decision:



\* prefer Unreal-native solutions,

\* explain non-obvious Unreal concepts briefly,

\* do not assume backend architecture should map directly to Unreal,

\* avoid unnecessary `Service`, `Manager`, `Factory`, `Repository` or similar abstractions.



\---



\## 5. Documentation Rules



Keep this file small.



Add information only when it is:



\* persistent,

\* non-obvious,

\* relevant across multiple future tasks,

\* not better suited to Constitution, Architecture or a feature specification.



Do \*\*not\*\* store:



\* changelogs,

\* completed task lists,

\* detailed implementation history,

\* temporary TODOs,

\* detailed class/file inventories,

\* feature specifications,

\* information that can be obtained directly from the codebase.



When information becomes obsolete, remove it rather than preserving historical context.



