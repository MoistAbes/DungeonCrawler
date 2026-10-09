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

\* `UDeathComponent` — server-authoritative death life-cycle, ragdoll physics activation (prototype capsule / skeletal mesh), carried prop release, and movement disengagement with Zero-Bandwidth networking.

\* `UKnockbackComponent` — reusable pawn/character knockback handling with mass/resistance scaling, stun tracking, and dual-mode execution (CMC `LaunchCharacter` for kinematic alive pawns vs direct `AddImpulse` on simulating physics capsules/skeletons for ragdoll corpses).

\* `UStatusEffectComponent` — status-effect state with Zero-Bandwidth networking, dynamic carrier/fuel duration syncing, and modular helpers (`ComputeAdjustedDuration`, `DisplaceOtherLiquids`, `UpsertStatus`).

\* `AStatusZoneBase` and `AVolumetricStatusZone` — 3D volumetric fields (clouds, energy spheres) acting as immutable continuous source emitters, projecting status onto overlapping actors and underlying surface cells throughout their lifetime.

\* `UKineticForceLibrary` — shared kinetic/physics force operations and centralized kinetic impact/punch-through (`HandleKineticImpactAndPunchThrough` with breaker/victim resolution, physical integrity retention allowing ragdoll corpses to shatter glass walls, and polymorphic `CanBePunchedThrough()` suppression on explosive `AVolatileProp` actors).

\* `UStatusZoneLibrary` — status-zone/effect delivery operations (radial burst, point impact, volumetric spawn).

\* `UDungeonSurfaceSubsystem` — 3D sparse surface cell grid orchestrator for elemental propagation on walls/floors, coordinating static and dynamic grids via dedicated managers (`FStaticSurfaceGridManager`, `FDynamicSurfaceGridManager`), propagation pipelines (`FSurfaceGridPropagationUtils`), damage aggregation (`FSurfaceGridDamageUtils`), and spatial utilities. Employs continuous zone refresh buffering (`MinRemainingToSkip = 1.0f`) and conditional conduction BFS dirtying (`NewlyAddedCount > 0`) to prevent redundant tick workload.

\* `FStaticSurfaceGridManager` & `FDynamicSurfaceGridManager` — decoupled management of static world surface cells and localized dynamic grids on movable actors (e.g. gates, doors, mechanisms) using rigid transforms. Dynamic cell counts are strictly bounded to physical object voxels.

\* `FSurfaceGridPropagationUtils` — elemental cellular-automata spread, electrical conduction networks, and contact synchronization between static surfaces and dynamic actors. Enforces the Dynamic Conduction Invariant: conduction across movable actors propagates strictly along already active/existing cells, never creating cells out of thin air.

\* `FSurfaceGridDamageUtils` — environmental structural damage aggregation and DoT delivery to `UDamageableComponent`.

\* `SurfaceGridGeometryUtils` — spatial sampling and physical topology engine (`ProbeSurfaceAt`, `FindSpreadCandidates` with 4-variant candidate resolution for 90° transitions via `GetCornerCandidateCoords`, canonical voxel alignment via `FromWorldLocation`, and structural separation `CornerActor != SourceActor`).

\* `SurfaceGridProjectionUtils` — surface projection, 3D burst scans, edge drop-off line traces, and line-of-sight verification utilities.

\* `SurfaceActorInteractionUtils` — bidirectional floor/actor elemental transfer calculations delegating priority ordering (`SortByIngressPriority`) and transfer traits (`CanStatusTransferToFloor`) to `UElementalReactionRules`.

\* `USurfaceCellTransitionUtils` — cell-level transition resolver (`CalculateCellTransition`), applying reaction rules to `FSurfaceCellData` and managing burning visual states.

\* `UElementalReactionRules` — centralized Single Source of Truth (`CalculateElementalTransition`) for elemental reactions, material traits (`CanMaterialReceiveStatus` vs `CanMaterialSustainStatus`), liquid mutual exclusivity, independent status tiers (`GetDamagePerSecond(Tier)`), dynamic carrier/fuel duration syncing, directional floor transfer constraints (e.g. `Electrified.bCanTransferFromActorToFloor = false`), and propagation.

* **Level Design & Unreal MCP Map Editing** — The map (`Map_Dungeon_01.umap`) is 100% editable programmatically via Unreal MCP. Do not scan C++ engine headers for level tools. Always use the project CLI bridge: `& "E:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" Tools/MCP/unreal_mcp.py status` (and `run-script`, `save-level`). Full instructions and templates are in [`Docs/UNREAL_MCP_MAP_EDITING_GUIDE.md`](file:///E:/UE_PROJECTS/MyProject/Docs/UNREAL_MCP_MAP_EDITING_GUIDE.md).

* **Volatile Props & Item Tier Architecture** — Planned data-driven architecture separating Item Tier (explosion radius, instant damage, knockback, inventory tooltips) from Status Tier (chemical duration, DoT, tick intervals). Full specification in [`.context/specs/VOLATILE_PROP_TIER_SYSTEM.md`](file:///E:/UE_PROJECTS/MyProject/.context/specs/VOLATILE_PROP_TIER_SYSTEM.md).

* **Environmental & Dungeon Interaction Loop** — Planned vertical slice architecture covering open-ended movement modifiers (friction/speed/sensory), mechanism receivers (dungeon gates/doors responding to pressure plates/switches), dual-interaction loot chests (peaceful open vs brute-force smash), and optional ice/chilled balance design. Full specification in [`.context/specs/ENVIRONMENTAL_INTERACTION_LOOP.md`](file:///e:/UE_PROJECTS/MyProject/.context/specs/ENVIRONMENTAL_INTERACTION_LOOP.md).

* **Item, Container & Inventory Architecture** — Core Immersive Sim architecture separating Heavy World Props (held via `PhysicsCarryComponent`) from Inventory Items (Flyweight pattern via `UItemDefinition` Data Assets and `FInventorySlot`). Covers physical pickups (`APickupItemProp`), dual-interaction chests (`ADungeonChestProp`), and server-authoritative inventory replication (`FFastArraySerializer`). Full specification in [`.context/specs/ITEM_AND_INVENTORY_SYSTEM.md`](file:///e:/UE_PROJECTS/MyProject/.context/specs/ITEM_AND_INVENTORY_SYSTEM.md).

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



