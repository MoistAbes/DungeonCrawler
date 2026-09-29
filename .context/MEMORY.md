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

* Never implement workaround code or temporary fallbacks solely to avoid restarting the Unreal Editor. Restarting the editor takes seconds; temporary crutches introduce dead code, mask real bugs, and pollute the codebase.



\---



\## 3. Important Existing Systems



The following systems already exist and should be reused or extended when applicable:



\* `UPhysicsCarryComponent` — physics carrying/manipulation.

\* `UInteractionComponent` — interaction detection and interaction flow.

\* `UDamageableComponent` — shared damage/durability behavior.

\* `UKnockbackComponent` — reusable pawn/character knockback handling with mass/resistance scaling and stun tracking.

\* `UStatusEffectComponent` — status-effect state with Zero-Bandwidth networking, dynamic carrier/fuel duration syncing, and modular helpers (`ComputeAdjustedDuration`, `DisplaceOtherLiquids`, `UpsertStatus`).

\* `AStatusZoneBase` and derived status-zone actors — environmental status zones with automatic `UDungeonSurfaceSubsystem` registration and point geometry evaluation.

\* `UKineticForceLibrary` — shared kinetic/physics force operations and centralized kinetic impact/punch-through (`HandleKineticImpactAndPunchThrough` with breaker/victim resolution and flat horizontal penetration).

\* `UStatusZoneLibrary` — status-zone/effect delivery operations (radial burst, point impact, volumetric spawn).

\* `UDungeonSurfaceSubsystem` — 3D sparse surface cell grid for elemental propagation on walls/floors, solid fuel combustion orchestration, and structural damage aggregation. Delegates spatial burst projections to `SurfaceGridProjectionUtils` and floor/actor transfers to `SurfaceActorInteractionUtils`.

\* `SurfaceGridGeometryUtils` — spatial sampling and physical topology engine (`ProbeSurfaceAt`, `FindSpreadCandidates` with `Coplanar`/`Corner` hierarchy and structural separation `CornerActor != SourceActor`).

\* `SurfaceGridProjectionUtils` — surface projection, 3D burst scans, edge drop-off line traces, and line-of-sight verification utilities.

\* `SurfaceActorInteractionUtils` — bidirectional floor/actor elemental transfer calculations (Phase A Actor->Floor and Phase B Floor->Actor).

\* `USurfaceCellTransitionUtils` — cell-level transition resolver (`CalculateCellTransition`), applying reaction rules to `FSurfaceCellData` and managing burning visual states.

\* `UElementalReactionRules` — centralized Single Source of Truth (`CalculateElementalTransition`) for elemental reactions, material traits (`CanMaterialReceiveStatus` vs `CanMaterialSustainStatus`), liquid mutual exclusivity, independent status tiers (`GetDamagePerSecond(Tier)`), dynamic carrier/fuel duration syncing, and propagation.

* **Level Design & Unreal MCP Map Editing** — The map (`Map_Dungeon_01.umap`) is 100% editable programmatically via Unreal MCP. Do not scan C++ engine headers for level tools. Always use the project CLI bridge: `& "E:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" Tools/MCP/unreal_mcp.py status` (and `run-script`, `save-level`). Full instructions and templates are in [`Docs/UNREAL_MCP_MAP_EDITING_GUIDE.md`](file:///E:/UE_PROJECTS/MyProject/Docs/UNREAL_MCP_MAP_EDITING_GUIDE.md).

* **Volatile Props & Item Tier Architecture** — Planned data-driven architecture separating Item Tier (explosion radius, instant damage, knockback, inventory tooltips) from Status Tier (chemical duration, DoT, tick intervals). Full specification in [`.context/specs/VOLATILE_PROP_TIER_SYSTEM.md`](file:///E:/UE_PROJECTS/MyProject/.context/specs/VOLATILE_PROP_TIER_SYSTEM.md).

* **Environmental & Dungeon Interaction Loop** — Planned vertical slice architecture covering open-ended movement modifiers (friction/speed/sensory), mechanism receivers (dungeon gates/doors responding to pressure plates/switches), dual-interaction loot chests (peaceful open vs brute-force smash), and optional ice/chilled balance design. Full specification in [`.context/specs/ENVIRONMENTAL_INTERACTION_LOOP.md`](file:///e:/UE_PROJECTS/MyProject/.context/specs/ENVIRONMENTAL_INTERACTION_LOOP.md).

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



