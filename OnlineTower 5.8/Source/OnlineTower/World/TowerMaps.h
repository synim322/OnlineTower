#pragma once
#include "CoreMinimal.h"
class UWorld;
namespace TowerMaps {
inline constexpr const TCHAR* Menu=TEXT("/Game/Maps/Frontend/L_MainMenu");
inline constexpr const TCHAR* Hub=TEXT("/Game/Maps/Hub/L_Hub");
inline constexpr const TCHAR* Trials[]={TEXT("/Game/Maps/Trials/L_Dungeon"),TEXT("/Game/Maps/Trials/L_Duel"),TEXT("/Game/Maps/Trials/L_FloorFinal")};
// -2: legacy all-zones sandbox, -1: hub, 0..2: trials.
int32 GetZone(const UWorld* World);
FName Destination(int32 Trial);
}
