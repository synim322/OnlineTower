#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
class UWorld;
namespace TowerMaps {
inline constexpr const TCHAR* Menu=TEXT("/Game/Maps/Frontend/L_MainMenu");
inline constexpr const TCHAR* Hub=TEXT("/Game/Maps/Hub/L_Hub");
inline constexpr const TCHAR* Trials[]={TEXT("/Game/Maps/Trials/L_Dungeon"),TEXT("/Game/Maps/Trials/L_Duel"),TEXT("/Game/Maps/Trials/L_FloorFinal")};
// -2: legacy all-zones sandbox, -1: hub, 0..2: trials.
int32 GetZone(const UWorld* World);
FName Destination(int32 Trial);
/** Server-owned transition; keep the session and its current PIE/host port. */
inline bool TravelToHubListen(UWorld* World)
{
	if (!World || World->GetNetMode()==NM_Client || !World->GetAuthGameMode()) return false;
	// A second player's login can arrive while the first request is loading the hub.
	if (World->IsInSeamlessTravel() || !World->NextURL.IsEmpty()) return true;
	FURL Url = World->URL;
	Url.Map = Hub;
	Url.Portal.Empty();
	// PIE can have a frontend override in its URL. Explicitly select the hub rules.
	Url.AddOption(TEXT("game=/Script/OnlineTower.TowerHubGameMode"));
	if (!Url.HasOption(TEXT("listen"))) Url.AddOption(TEXT("listen"));
	return World->ServerTravel(Url.ToString(), true);
}
}
