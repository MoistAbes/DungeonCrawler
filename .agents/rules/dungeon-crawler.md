\# DungeonCrawler — Project Rule



This is the workspace rule for the DungeonCrawler Unreal Engine project.



\## 1. Project Context



DungeonCrawler is an Unreal Engine 5.8 C++ multiplayer dungeon crawler.



The project uses:



\* C++ for core and reusable gameplay logic,

\* Blueprints for composition, configuration, presentation and appropriate feature-specific wiring,

\* Unreal-native components, interfaces, delegates/events and data-driven mechanisms,

\* server-authoritative multiplayer,

\* Unreal MCP when editor/world/Blueprint state cannot be reliably verified from source code.



\---



\## 2. Project Documentation



The project has three primary context documents:



\* `@../../.context/CONSTITUTION.md`

\* `@../../.context/ARCHITECTURE.md`

\* `@../../.context/MEMORY.md`



Treat them as different sources of information:



\### Constitution



`CONSTITUTION.md` defines project-wide architectural principles, priorities and constraints.



Use it when deciding \*\*how the project should be built\*\*.



\### Architecture



`ARCHITECTURE.md` describes the \*\*current structural architecture\*\* of the project.



Use it when deciding \*\*where new functionality belongs and how existing systems are connected\*\*.



Do not assume the architecture document is a desired future architecture. It describes the current project and must stay synchronized with the actual implementation.



\### Memory



`MEMORY.md` contains only persistent cross-session context that is useful to the AI.



Do not treat Memory as a changelog, task list or replacement for the Architecture document.



\---



\## 3. Context Loading



Do not blindly load every project document for every task.



Use the minimum context required for the current task.



Always respect the Constitution when making architectural decisions.



Read Architecture when the task involves:



\* modifying existing gameplay architecture,

\* creating or modifying C++ systems,

\* changing responsibilities between systems,

\* introducing a new reusable component/interface,

\* changing networking structure,

\* changing Blueprint/C++ boundaries,

\* modifying shared systems.



Read the relevant feature specification when one exists.



Do not load unrelated feature specifications.



If documentation and implementation appear to disagree, investigate the discrepancy instead of silently choosing one.



\---



\## 4. Before Making Changes



Before implementing a non-trivial change:



1\. Understand the user's requested behavior.

2\. Inspect the existing implementation.

3\. Identify systems that already solve part of the problem.

4\. Check the relevant architecture and specification.

5\. Determine the smallest appropriate change.

6\. Consider multiplayer/server-authority implications.

7\. Only then modify the project.



Do not create a parallel implementation of an existing system without a concrete reason.



\---



\## 5. Unreal-First Architecture



Prefer idiomatic Unreal Engine architecture over directly translating Java/Spring patterns into Unreal.



Prefer existing Unreal mechanisms such as:



\* Actor Components,

\* Actors,

\* Unreal Interfaces,

\* Delegates and Events,

\* Subsystems when appropriate,

\* Data Assets,

\* Data Tables,

\* Gameplay Tags where appropriate,

\* Unreal replication and RPCs.



Do not introduce generic backend-style layers such as:



\* `Service`,

\* `Manager`,

\* `Repository`,

\* `Factory`,

\* `Provider`,

\* `Coordinator`



unless they solve a concrete problem in the actual project.



Composition is generally preferred over unnecessary inheritance.



Inheritance remains valid when the Unreal type hierarchy represents a meaningful relationship.



\---



\## 6. Existing Systems Come First



Before creating a new system, inspect existing functionality.



Particularly consider existing:



\* gameplay components,

\* interfaces,

\* status-zone systems,

\* interaction systems,

\* carry/physics systems,

\* damage systems,

\* networking helpers,

\* shared libraries.



Extend or reuse an existing system when it already owns the relevant responsibility.



Avoid duplicate systems with overlapping responsibilities.



\---



\## 7. C++ and Blueprint Responsibilities



Core reusable gameplay rules should generally live in C++ when they involve:



\* authoritative multiplayer behavior,

\* complex state,

\* reusable gameplay logic,

\* non-trivial physics,

\* performance-sensitive behavior,

\* strong code-level contracts.



Blueprints may be used for:



\* actor composition,

\* asset configuration,

\* designer-facing values,

\* presentation,

\* simple event wiring,

\* feature-specific orchestration.



Do not move gameplay rules into Blueprint merely because Blueprint is convenient.



Do not move simple composition/configuration into C++ merely because C++ is available.



Choose the appropriate Unreal-native boundary.



\---



\## 8. Multiplayer



The project is server authoritative.



Treat client input as intent.



The server owns authoritative shared gameplay state.



For multiplayer features, explicitly consider:



\* client request/intent,

\* server validation,

\* authoritative state mutation,

\* replication,

\* client presentation.



Never trust a client simply because the request originated from local player input.



\---



\## 9. Cast<T> Policy



`Cast<T>` is not globally forbidden.



A concrete cast is acceptable when the code genuinely needs a specific Unreal type.



However:



> Do not use casts as the architecture.



Prefer interfaces, components, events, delegates or explicit references when the dependency represents a reusable capability or architectural contract.



Avoid systems that accumulate casts to many concrete actor types as their primary communication mechanism.



\---



\## 10. Verification



Do not equate "code written" with "feature complete".



For non-trivial changes, verify the relevant acceptance criteria.



Use the appropriate verification source:



\* source code for C++ behavior,

\* project configuration for configuration changes,

\* Blueprint/editor state for Blueprint/content changes,

\* Unreal MCP when editor/world state cannot be reliably determined from source,

\* build/compile checks when appropriate,

\* runtime testing when behavior cannot be established statically.



If something cannot be verified, state that explicitly.



\---



\## 11. Documentation Discipline



Keep project documentation separated by responsibility.



Do not put:



\* temporary TODOs,

\* changelogs,

\* implementation history,

\* detailed class inventories,

\* feature specifications



into `MEMORY.md`.



Do not turn `ARCHITECTURE.md` into a task list.



Do not duplicate the Constitution inside other documentation files.



When a meaningful architectural change is made, update `ARCHITECTURE.md` so it reflects the resulting current state.



When persistent AI context changes, update `MEMORY.md`.



\---



\## 12. Decision Priority



When architectural choices conflict, use this priority:



1\. Unreal correctness

2\. Multiplayer correctness and server authority

3\. Gameplay correctness

4\. Maintainability

5\. Performance

6\. Reuse

7\. Abstraction elegance



Do not sacrifice gameplay correctness or Unreal-native behavior merely to preserve an abstract architecture pattern.



\---



\## 13. Developer Context



The developer has strong Java/Spring experience but is learning Unreal Engine and C++.



When an Unreal-specific architectural choice is non-obvious:



\* prefer the Unreal-native solution,

\* briefly explain the relevant Unreal concept,

\* do not assume Java/Spring architecture maps directly to Unreal.



The goal is not to reproduce a backend architecture inside Unreal.



The goal is to build a maintainable Unreal game using appropriate engineering principles.



\---



\## 14. When Uncertain



If the correct architectural decision is unclear:



1\. inspect the existing code,

2\. inspect relevant project documentation,

3\. inspect related Blueprints/content when applicable,

4\. use Unreal MCP when source inspection is insufficient,

5\. prefer the smallest change consistent with the existing architecture,

6\. explain the uncertainty before introducing a significant new abstraction.



Do not silently invent architectural conventions.



\---



\## 15. Core Principle



> Build the game first; build the architecture the game actually needs.



The project architecture should evolve from real gameplay requirements and concrete engineering problems.



Avoid both extremes:



\* uncontrolled ad-hoc implementation,

\* over-engineered architecture created without a real need.



