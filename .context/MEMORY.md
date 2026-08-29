\# Project Memory: Rogue-like Dungeon Crawler (Co-op 1-4)

\- Always follow architectural guidelines defined in architecture.md.

\## Tech Stack

\- Unreal Engine 5.x C++ (Server-Authoritative Network Model)

\- JetBrains Rider

\- Enhanced Input System + Gameplay Tags



\## Domain Modules \& Components

\- \[ ] `APlayerCharacter` (Agregator komponentów, ruch WASD)

\- \[ ] `UElementalStateComponent` (Obsługa stanów: Wet, Flammable, Burning, Conductive)

\- \[ ] `UDestructibleMaterialComponent` (Drewno, kamień, podatności na typy obrażeń)

\- \[ ] `UPhysicsGrabberComponent` (Chwytanie i rzucanie obiektami fizycznymi)



\## Interfaces

\- \[ ] `IInteractable` (Metody: `Interact`, `Grab`, `Throw`)

\- \[ ] `IDamageable` (Metoda: `TakeDamage` przyjmująca `FDamageContext`)



\## Decisions \& Conventions Log

\- 2026-08-29: Replikacja sieciowa włączona od etapu 0 dla każdego bytu domenowego.

\- 2026-08-29: Brak logiki biznesowej w Blueprintach; Blueprinty wyłącznie jako prefab/widok.

