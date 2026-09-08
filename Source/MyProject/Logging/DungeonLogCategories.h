#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Dungeon Crawler - Dedykowane kategorie logowania silnikowego.
 * Umożliwiają selektywne filtrowanie, zmianę poziomu szczegółowości (Verbosity)
 * z poziomu konsoli w runtime oraz czyste separowanie telemetrii sieciowej i fizycznej.
 */

// Logi systemu interakcji (chwytanie, upuszczanie, rzucanie, przełączniki)
MYPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogDungeonInteraction, Log, All);

// Logi fizyki kinetycznej (pchanie brył, uderzenia, odrzuty, tłumienie mikroruchów)
MYPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogDungeonPhysics, Log, All);

// Logi sieciowe, RPC, desynchronizacje i walidacje autorytatywne
MYPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogDungeonNetwork, Log, All);

// Logi mechanizmów lochu, pułapek, płyt naciskowych i tłoków
MYPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogDungeonMechanisms, Log, All);

// Logi chemii żywiołów, statusów i reakcji elementarnych
MYPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogDungeonElements, Log, All);
