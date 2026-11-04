#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "TowerAccountSubsystem.h"
#include "TowerPartyComponent.h"
#include "TowerCharacter.h"
#include "TowerWorld.h"
#include "TowerFrontend.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Layout/Children.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
int32 CountOnlinePanels(const TSharedRef<SWidget>& Widget)
{
 int32 Count=Widget->GetTag()==TEXT("TowerOnlinePanel") ? 1 : 0;
 FChildren* Children=Widget->GetChildren();
 for (int32 I=0; I<Children->Num(); ++I) Count+=CountOnlinePanels(Children->GetChildAt(I));
 return Count;
}
// Drives the real public account/entry path; deliberately starts in the networked menu.
struct FTowerFrontendNetworkProbe
{
 bool bRegistered=false, bRequested=false, bPassed=false, bServerPassed=false, bPanelOpened=false;
 FString Identity;
 float MenuReadyAge=0.f;
 FTowerFrontendNetworkProbe() { FWorldDelegates::OnWorldPostActorTick.AddRaw(this,&FTowerFrontendNetworkProbe::Tick); }
 ~FTowerFrontendNetworkProbe() { FWorldDelegates::OnWorldPostActorTick.RemoveAll(this); }
 void Tick(UWorld* World,ELevelTick,float DeltaSeconds)
 {
  if (!FParse::Param(FCommandLine::Get(),TEXT("TowerFrontendNetworkProbe")) || !World || World->WorldType!=EWorldType::Game) return;
  auto* GS=World->GetGameState();
  auto* PC=World->GetGameInstance()->GetFirstLocalPlayerController();
  auto* Account=World->GetGameInstance()->GetSubsystem<UTowerAccountSubsystem>();
  if (!GS || !PC || !Account) return;
  const bool bHub=World->GetMapName().Contains(TEXT("L_Hub"));
  auto* Viewport=World->GetGameViewport();
  if (!bHub && !bPanelOpened && Viewport && Viewport->GetGameViewportWidget().IsValid())
  {
   TowerMenuUI::ShowOnline(PC);
   bPanelOpened=CountOnlinePanels(Viewport->GetGameViewportWidget().ToSharedRef())==1;
   if (bPanelOpened) UE_LOG(LogTemp,Display,TEXT("TOWER_ENTRY_PROBE PANEL_OPEN"));
  }
  FString Role; FParse::Value(FCommandLine::Get(),TEXT("TowerEntryRole="),Role);
  // Client b logs in only after travel, reproducing the second player's normal delayed login.
  if (!bRegistered && (Role!=TEXT("b") || bHub))
  {
   FString User; FParse::Value(FCommandLine::Get(),TEXT("TowerProbeUser="),User);
   if (!User.StartsWith(TEXT("entry_"))) return;
   bRegistered=true; Account->Register(User,FGuid::NewGuid().ToString()+TEXT("Aa_9"));
  }
  if (Account->IsLoggedIn() && Identity.IsEmpty()) Identity=Account->GetUserId();
  if (!bHub && GS->PlayerArray.Num()==3) MenuReadyAge+=DeltaSeconds;
  if (!bHub && Account->IsLoggedIn() && GS->PlayerArray.Num()==3 && MenuReadyAge>5.f
      && FParse::Param(FCommandLine::Get(),TEXT("TowerEntryInitiator")) && !bRequested)
  {
   bRequested=true;
   UE_LOG(LogTemp,Display,TEXT("TOWER_ENTRY_PROBE REQUEST Role=%s"),*Role);
   if (auto* Frontend=Cast<ATowerFrontendController>(PC)) Frontend->PlayOnline();
  }
  if (!bHub) return;
  if (Role==TEXT("b") && Account->IsLoggedIn() && !bRequested)
  { bRequested=true; Account->ConnectLocal(); }
  int32 Ready=0;
  TSet<FString> Ids;
  for (APlayerState* State:GS->PlayerArray)
  {
   auto* Player=Cast<ATowerPlayerState>(State);
   if (Player && !Player->AccountId.IsEmpty() && Player->SkillIds.Num()==4 && Cast<ATowerCharacter>(Player->GetPawn()))
   { ++Ready; Ids.Add(Player->AccountId); }
  }
  auto* Party=PC->FindComponentByClass<UTowerPartyComponent>();
  if (!bPassed && Ready==3 && Ids.Num()==3 && Party && Party->IsAuthenticated()
      && Account->GetUserId()==Identity && !Account->IsConnecting() && PC->GetPawn()
      && bPanelOpened && Viewport && Viewport->GetGameViewportWidget().IsValid()
      && CountOnlinePanels(Viewport->GetGameViewportWidget().ToSharedRef())==0)
  {
   bPassed=true;
   UE_LOG(LogTemp,Display,TEXT("TOWER_ENTRY_PROBE CLIENT_PASS Role=%s Players=3 IdentityPreserved=1 Pawns=3 Skills=4 Panels=0"),*Role);
  }
  if (!bServerPassed && World->GetNetMode()==NM_ListenServer && Ready==3 && Ids.Num()==3)
  {
   bServerPassed=true;
   UE_LOG(LogTemp,Display,TEXT("TOWER_ENTRY_PROBE SERVER_PASS Players=3 Port=%d"),World->URL.Port);
  }
 }
} GFrontendNetworkProbe;
}
#endif
