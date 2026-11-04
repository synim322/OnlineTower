#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "TowerAccountSubsystem.h"
#include "TowerPartyComponent.h"
#include "TowerAbilities.h"
#include "TowerCharacter.h"
#include "TowerWorld.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace
{
// Development-only orchestration: accounts and party actions use the production
// client API/RPCs; the server only positions pawns to create real portal overlaps.
struct FTowerPartyLimitsProbe
{
 double NextAction=0, PhaseAt=0;
 int32 NextInvite=1, Phase=0;
 bool Accepted=false, Left=false, CancelSent=false, FullRejected=false;
 bool ClientPassed=false, QueueSeen=false;
 FString Password=FGuid::NewGuid().ToString()+TEXT("Aa_9");
 FTowerPartyLimitsProbe() { FWorldDelegates::OnWorldPostActorTick.AddRaw(this,&FTowerPartyLimitsProbe::Tick); }
 ~FTowerPartyLimitsProbe() { FWorldDelegates::OnWorldPostActorTick.RemoveAll(this); }
 void Fail(const TCHAR* Why)
 {
  Phase=99;
  UE_LOG(LogTemp,Error,TEXT("TOWER_LIMITS_PROBE FAIL %s"),Why);
 }
 void Tick(UWorld* World,ELevelTick,float)
 {
  if(!FParse::Param(FCommandLine::Get(),TEXT("TowerPartyLimitsProbe")) || !World || World->WorldType!=EWorldType::Game) return;
  auto* GS=World->GetGameState(); if(!GS) return;
  const double Now=FPlatformTime::Seconds();
  if(World->GetNetMode()==NM_Client)
  {
   auto* PC=World->GetFirstPlayerController();
   auto* Account=World->GetGameInstance()->GetSubsystem<UTowerAccountSubsystem>();
   auto* Party=PC?PC->FindComponentByClass<UTowerPartyComponent>():nullptr;
   if(!Account || !Party) return;
   FString User; int32 Index=-1;
   if(!FParse::Value(FCommandLine::Get(),TEXT("TowerProbeUser="),User) || !User.StartsWith(TEXT("limits_")) ||
      !FParse::Value(FCommandLine::Get(),TEXT("TowerProbeIndex="),Index)) return;
   if(!Account->IsLoggedIn() && !Account->IsBusy() && Now>=NextAction)
   { NextAction=Now+4; Account->Register(User,Password); }
   auto* Self=PC->GetPlayerState<ATowerPlayerState>(); auto* Hero=Cast<ATowerCharacter>(PC->GetPawn());
   if(!Self || !Hero || !Party->IsAuthenticated()) return;
   if(!ClientPassed)
   { ClientPassed=true; UE_LOG(LogTemp,Display,TEXT("TOWER_LIMITS_PROBE AUTH_CLIENT_PASS Index=%d"),Index); }
   if(Index==0 && NextInvite<=6 && Now>=NextAction)
   {
    const int32 Count=FMath::Max(1,Party->GetPartyMembers().Num());
    if(Count==NextInvite)
     for(APlayerState* Base:GS->PlayerArray)
      if(auto* Target=Cast<ATowerPlayerState>(Base); Target && !Target->AccountId.IsEmpty() &&
         Target->AccountUsername.EndsWith(*FString::Printf(TEXT("_%d"),NextInvite)))
      {
       Party->InviteByName(Target->AccountUsername.ToUpper());
       ++NextInvite; NextAction=Now+1; break;
      }
   }
   if(!Accepted && !Party->GetInvites().IsEmpty() && Now>=NextAction)
   { Party->RespondInvite(Party->GetInvites()[0].InviterPlayerId,true); Accepted=true; NextAction=Now+1; }
   if(Index==0 && NextInvite==7 && !FullRejected && Party->GetStatus().Contains(TEXT("6 игроков")))
   { FullRejected=true; UE_LOG(LogTemp,Display,TEXT("TOWER_LIMITS_PROBE SEVENTH_REJECT_CLIENT_PASS")); }
   // Only the probe server moves these three clients to this isolated marker.
   if(Index>=3 && Index<=5 && !Left && Hero->GetActorLocation().X<TowerHubOrigin().X-1600 && Now>=NextAction)
   { Party->LeaveParty(); Left=true; NextAction=Now+1; }
   if(Index==1 && Self->QueuedTrial==0)
   {
    QueueSeen=true;
    if(!CancelSent && Self->ServerTime()-Self->QueueStartedAt>1.5f && Now>=NextAction)
    { Party->CancelMatchSearch(); CancelSent=true; NextAction=Now+1; }
   }
   if(Index==1 && QueueSeen && CancelSent && Self->QueuedTrial==-1 && Phase!=99)
   { Phase=99; UE_LOG(LogTemp,Display,TEXT("TOWER_LIMITS_PROBE CANCEL_CLIENT_PASS")); }
   return;
  }
  if(World->GetNetMode()!=NM_DedicatedServer || Phase==99 || GS->PlayerArray.Num()!=7) return;
  TArray<ATowerCharacter*> Heroes;
  for(APlayerState* Base:GS->PlayerArray)
  {
   auto* PS=Cast<ATowerPlayerState>(Base); auto* Hero=PS?Cast<ATowerCharacter>(PS->GetPawn()):nullptr;
   if(!PS || PS->AccountId.IsEmpty() || !Hero) return;
   Heroes.Add(Hero);
  }
  Heroes.Sort([](const ATowerCharacter& A,const ATowerCharacter& B)
  {return A.GetPlayerState<ATowerPlayerState>()->AccountUsername<B.GetPlayerState<ATowerPlayerState>()->AccountUsername;});
  auto State=[&](int32 I){return Heroes[I]->GetPlayerState<ATowerPlayerState>();};
  auto Members=[&]()
  {
   int32 Count=0; const FGuid Id=State(0)->PartyId;
   for(auto* Hero:Heroes) if(Id.IsValid() && Hero->GetPlayerState<ATowerPlayerState>()->PartyId==Id) ++Count;
   return Count;
  };
  auto Move=[&](int32 I,FVector Position)
  {
   Heroes[I]->NextPortalTime=0;
   Heroes[I]->GetCharacterMovement()->StopMovementImmediately();
   Heroes[I]->GetCharacterMovement()->DisableMovement();
   Heroes[I]->TeleportTo(Position,FRotator::ZeroRotator,false,true);
   Heroes[I]->ForceNetUpdate();
  };
  auto Enter=[&](int32 Trial,int32 Count)
  {
   for(TActorIterator<ATowerPortal> It(World);It;++It) if(It->DestinationTrial==Trial)
   {
    for(int32 I=0;I<Count;++I) Move(I,It->GetActorLocation()+FVector(0,(I%3-1)*70,(I/3)*100-50));
    return true;
   }
   return false;
  };
  auto NoneQueued=[&]()
  {
   for(int32 I=0;I<Heroes.Num();++I) if(State(I)->QueuedTrial!=-1 || State(I)->MatchId.IsValid() || Heroes[I]->TrialIndex!=-1) return false;
   return true;
  };
  if(Phase==0)
  {
   if(Members()!=6) return;
   Phase=1;PhaseAt=Now;
  }
  else if(Phase==1 && Now-PhaseAt>5)
  {
   if(Members()!=6 || State(6)->PartyId.IsValid()) {Fail(TEXT("party exceeded six"));return;}
   UE_LOG(LogTemp,Display,TEXT("TOWER_LIMITS_PROBE MAX6_SERVER_PASS"));
   if(!Enter(0,6)) {Fail(TEXT("dungeon portal missing"));return;}
   Phase=2;PhaseAt=Now;
  }
  else if(Phase==2 && Now-PhaseAt>4)
  {
   if(!NoneQueued()) {Fail(TEXT("six-player party admitted to dungeon"));return;}
   UE_LOG(LogTemp,Display,TEXT("TOWER_LIMITS_PROBE DUNGEON_REJECT_PASS"));
   for(int32 I=0;I<6;++I) Move(I,TowerHubOrigin()+FVector(I>=3?-1900:-600,(I%3-1)*160,120));
   Phase=3;PhaseAt=Now;
  }
  else if(Phase==3 && Members()==3 && Now-PhaseAt>3)
  {
   if(!Enter(1,3)) {Fail(TEXT("duel portal missing"));return;}
   Phase=4;PhaseAt=Now;
  }
  else if(Phase==4 && Now-PhaseAt>4)
  {
   if(!NoneQueued()) {Fail(TEXT("three-player party admitted to duel"));return;}
   UE_LOG(LogTemp,Display,TEXT("TOWER_LIMITS_PROBE DUEL_REJECT_PASS"));
   for(int32 I=0;I<3;++I) Move(I,TowerHubOrigin()+FVector(-600,(I-1)*160,120));
   Phase=5;PhaseAt=Now;
  }
  else if(Phase==5 && Now-PhaseAt>2)
  {
   if(!Enter(0,3)) {Fail(TEXT("dungeon portal missing"));return;}
   Phase=6;PhaseAt=Now;
  }
  else if(Phase==6)
  {
   if(State(0)->QueuedTrial==0 && State(1)->QueuedTrial==0 && State(2)->QueuedTrial==0)
   {Phase=7;PhaseAt=Now;}
  }
  else if(Phase==7 && NoneQueued())
  {Phase=8;PhaseAt=Now;}
  else if(Phase==8)
  {
   if(!NoneQueued()) {Fail(TEXT("cancelled party immediately requeued inside portal"));return;}
   if(Now-PhaseAt>4)
   {
    UE_LOG(LogTemp,Display,TEXT("TOWER_LIMITS_PROBE SERVER_PASS Max6=1 SeventhRejected=1 Dungeon6Rejected=1 Duel3Rejected=1 WholePartyCancel=1 NoAutoRequeue=1"));
    Phase=99;
   }
  }
 }
};
FTowerPartyLimitsProbe PartyLimitsProbe;
}
#endif
