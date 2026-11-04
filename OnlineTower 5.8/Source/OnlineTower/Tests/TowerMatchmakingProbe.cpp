#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "TowerAccountSubsystem.h"
#include "TowerPartyComponent.h"
#include "TowerAbilities.h"
#include "TowerCharacter.h"
#include "TowerWorld.h"
#include "TowerHub.h"
#include "TowerFrontend.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace
{
// Uses production account/party RPCs and physical portal overlaps. No test RPCs.
struct FTowerMatchmakingProbe
{
 bool AuthStarted=false, Invited=false, Accepted=false, ClientPassed=false, QueueSeen=false, MatchAccepted=false, AcceptPhaseSeen=false;
 int32 Phase=0;
 double PhaseAt=0;
 TMap<FString,TArray<int32>> OriginalSkills;
 FTowerMatchmakingProbe() { FWorldDelegates::OnWorldPostActorTick.AddRaw(this,&FTowerMatchmakingProbe::Tick); }
 ~FTowerMatchmakingProbe() { FWorldDelegates::OnWorldPostActorTick.RemoveAll(this); }
 void Tick(UWorld* World,ELevelTick,float)
 {
  if(!FParse::Param(FCommandLine::Get(),TEXT("TowerMatchProbe")) || !World || World->WorldType!=EWorldType::Game) return;
  int32 Trial=0; FParse::Value(FCommandLine::Get(),TEXT("TowerProbeTrial="),Trial);
  const int32 Required=Trial==0?5:Trial==1?2:12;
  if(World->GetNetMode()==NM_Standalone)
  {
   auto* Menu=Cast<ATowerFrontendController>(World->GetFirstPlayerController());
   auto* Account=World->GetGameInstance()->GetSubsystem<UTowerAccountSubsystem>();
   if(Menu && Account && !AuthStarted)
   {
    FString User;
    if(!FParse::Value(FCommandLine::Get(),TEXT("TowerProbeUser="),User)||!User.StartsWith(TEXT("match_"))) return;
    AuthStarted=true; Menu->PlayOnline(); Account->Register(User,FGuid::NewGuid().ToString()+TEXT("Aa_9"));
    UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_PROBE MENU_AUTH_STARTED"));
   }
   return;
  }
  auto* GS=World->GetGameState(); if(!GS) return;
  const double Now=FPlatformTime::Seconds();
  if(World->GetNetMode()==NM_Client)
  {
   auto* PC=World->GetFirstPlayerController();
   auto* Account=World->GetGameInstance()->GetSubsystem<UTowerAccountSubsystem>();
   auto* Party=PC?PC->FindComponentByClass<UTowerPartyComponent>():nullptr;
   if(!Account || !Party) return;
   if(!AuthStarted)
   {
    FString User; if(!FParse::Value(FCommandLine::Get(),TEXT("TowerProbeUser="),User)||!User.StartsWith(TEXT("match_"))) return;
    AuthStarted=true; Account->Register(User,FGuid::NewGuid().ToString()+TEXT("Aa_9"));
   }
   auto* Self=PC->GetPlayerState<ATowerPlayerState>();
   auto* Hero=Cast<ATowerCharacter>(PC->GetPawn());
   if(!Party->IsAuthenticated()||!Self||!Hero) return;
   // Dungeon mixes a two-person party with three solo tickets. Duel mixes two solos.
   if(Trial==0 && !Invited && Self->AccountUsername.EndsWith(TEXT("_0")))
    for(APlayerState* PS:GS->PlayerArray)
     if(auto* Other=Cast<ATowerPlayerState>(PS);Other && Other->AccountUsername.EndsWith(TEXT("_1")) && !Other->AccountId.IsEmpty())
     { Party->InviteByName(Other->AccountUsername.ToUpper()); Invited=true; break; }
   if(!Accepted && !Party->GetInvites().IsEmpty())
   { Party->RespondInvite(Party->GetInvites()[0].InviterPlayerId,true); Accepted=true; }
   if(!QueueSeen && Self->QueuedTrial==Trial && GS->GetServerWorldTimeSeconds()-Self->QueueStartedAt>1)
   {
    QueueSeen=true;
    UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_PROBE QUEUE_CLIENT_PASS Trial=%d Population=%d"),Trial,Self->QueuePopulation);
    if(auto* Hub=Cast<ATowerHubController>(PC)) { Hub->SetMenuScreen(ETowerHubScreen::Pause); Hub->SetMenuScreen(ETowerHubScreen::Gameplay); }
   }
   const bool Spectator=Self->AccountUsername.EndsWith(*FString::Printf(TEXT("_%d"),Required));
   if(!Spectator && !MatchAccepted && Self->MatchAcceptEndsAt>0 && !Self->bMatchAccepted)
   { Party->AcceptMatchSearch(); MatchAccepted=true; UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_PROBE ACCEPT_SENT Trial=%d"),Trial); }
   int32 Matched=0;
   for(APlayerState* PS:GS->PlayerArray) if(auto* P=Cast<ATowerPlayerState>(PS);P && P->MatchId.IsValid()) ++Matched;
   if(!ClientPassed && Matched==Required && Self->SkillIds.Num()==4 &&
      (Spectator?(!Self->MatchId.IsValid() && Hero->TrialIndex==-1):(Self->MatchId.IsValid() && Hero->TrialIndex==Trial && Self->QueuedTrial==-1)))
   { ClientPassed=true; UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_PROBE CLIENT_PASS Trial=%d Spectator=%d"),Trial,Spectator?1:0); }
   return;
  }
  if(World->GetNetMode()!=NM_DedicatedServer || Phase==99 || GS->PlayerArray.Num()!=Required+1) return;
  TArray<ATowerCharacter*> Heroes;
  for(APlayerState* PS:GS->PlayerArray)
  {
   auto* P=Cast<ATowerPlayerState>(PS); auto* H=P?Cast<ATowerCharacter>(P->GetPawn()):nullptr;
   if(!P || P->AccountId.IsEmpty() || !H || P->SkillIds.Num()!=4) return;
   Heroes.Add(H);
  }
  Heroes.Sort([](const ATowerCharacter& A,const ATowerCharacter& B){return A.GetPlayerState<ATowerPlayerState>()->AccountUsername < B.GetPlayerState<ATowerPlayerState>()->AccountUsername;});
  ATowerPortal* Portal=nullptr;
  for(TActorIterator<ATowerPortal> It(World);It;++It) if(It->DestinationTrial==Trial) { Portal=*It;break; }
  if(!Portal) return;
  auto Move=[&](int32 Index)
  {
   Heroes[Index]->NextPortalTime=0;
   Heroes[Index]->GetCharacterMovement()->StopMovementImmediately();
   Heroes[Index]->TeleportTo(Portal->GetActorLocation()+FVector(0,(Index%3-1)*75,0),FRotator::ZeroRotator,false,true);
  };
  if(Phase==0)
  {
   if(Trial==0)
   {
    auto* A=Heroes[0]->GetPlayerState<ATowerPlayerState>();auto* B=Heroes[1]->GetPlayerState<ATowerPlayerState>();
    if(!A->PartyId.IsValid() || A->PartyId!=B->PartyId) return;
   }
   for(auto* H:Heroes) {auto* P=H->GetPlayerState<ATowerPlayerState>(); OriginalSkills.Add(P->AccountId,P->SkillIds);}
   Move(0); Phase=1; PhaseAt=Now;
  }
  else if(Phase==1 && Now-PhaseAt>3)
  {
   if(Heroes[0]->TrialIndex!=-1) { Fail(TEXT("started before roster ready"));return; }
   if(Trial==0 && Heroes[0]->GetPlayerState<ATowerPlayerState>()->QueuedTrial!=-1) {Fail(TEXT("partial party queued"));return;}
   for(int32 I=1;I<Required-1;++I) Move(I);
   Phase=2; PhaseAt=Now;
  }
  else if(Phase==2 && Now-PhaseAt>5)
  {
   if(Heroes[0]->GetPlayerState<ATowerPlayerState>()->QueuedTrial!=Trial || Heroes[0]->TrialIndex!=-1)
   {Fail(TEXT("queue snapshot or premature match"));return;}
   UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_PROBE WAIT_PASS Trial=%d"),Trial);
   Move(Required-1);Phase=3;PhaseAt=Now;
  }
  else if(Phase==3)
  {
   // Roster must enter the 10s accept phase; teleport only after every client AcceptMatchSearch.
   auto* Lead=Heroes[0]->GetPlayerState<ATowerPlayerState>();
   if(Now-PhaseAt>15) {Fail(TEXT("accept phase timed out"));return;}
   if(Lead->MatchAcceptEndsAt>0) AcceptPhaseSeen=true;
   if(Lead->MatchId.IsValid())
   {
    if(!AcceptPhaseSeen) {Fail(TEXT("match started before accept"));return;}
    UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_PROBE ACCEPT_PASS Trial=%d"),Trial);
    Phase=4; PhaseAt=Now; return;
   }
   if(!AcceptPhaseSeen) return;
   for(int32 I=0;I<Required;++I)
    if(!Heroes[I]->GetPlayerState<ATowerPlayerState>()->bMatchAccepted) return;
  }
  else if(Phase==4)
  {
   const FGuid Id=Heroes[0]->GetPlayerState<ATowerPlayerState>()->MatchId;
   if(!Id.IsValid()) { if(Now-PhaseAt>8) Fail(TEXT("match did not start after accept")); return; }
   for(int32 I=0;I<Required;++I)
   {
    auto* P=Heroes[I]->GetPlayerState<ATowerPlayerState>();
    if(P->MatchId!=Id || Heroes[I]->TrialIndex!=Trial || P->QueuedTrial!=-1) return;
    if(OriginalSkills.FindChecked(P->AccountId)!=P->SkillIds) {Fail(TEXT("loadout changed"));return;}
   }
   auto* Spectator=Heroes[Required];
   if(Spectator->TrialIndex!=-1 || Spectator->GetPlayerState<ATowerPlayerState>()->MatchId.IsValid()) {Fail(TEXT("spectator moved"));return;}
   auto* Victim=Heroes[1]->GetPlayerState<ATowerPlayerState>();const float Before=Victim->Health();
   UGameplayStatics::ApplyDamage(Heroes[1],10,Heroes[0]->GetController(),Heroes[0],UDamageType::StaticClass());
   const bool DamageOK=Trial==1 ? Victim->Health()<Before : FMath::IsNearlyEqual(Victim->Health(),Before);
   const float After=Victim->Health();
   UGameplayStatics::ApplyDamage(Heroes[1],10,Spectator->GetController(),Spectator,UDamageType::StaticClass());
   if(!DamageOK || !FMath::IsNearlyEqual(Victim->Health(),After)) {Fail(TEXT("damage policy"));return;}
   UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_PROBE SERVER_PASS Trial=%d Players=%d SpectatorInHub=1 DamagePolicy=1 LoadoutPreserved=1 AcceptPhase=1"),Trial,Required);
   Phase=99;
  }
 }
 void Fail(const TCHAR* Why) {Phase=99;UE_LOG(LogTemp,Error,TEXT("TOWER_MATCH_PROBE FAIL %s"),Why);}
} GMatchmakingProbe;
}
#endif
