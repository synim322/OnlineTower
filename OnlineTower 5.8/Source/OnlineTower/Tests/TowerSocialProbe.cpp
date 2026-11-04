#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "TowerAccountSubsystem.h"
#include "TowerSocialSubsystem.h"
#include "TowerPartyComponent.h"
#include "TowerAbilities.h"
#include "TowerCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace
{
// Explicit development-only integration driver. Actions use normal account,
// social HTTP APIs and player-owned party RPCs, never diagnostic server RPCs.
struct FTowerSocialProbe
{
 bool AuthStarted=false, InitialHubVerified=false, ClientPassed=false;
 bool ServerHadInitialPlayer=false, ServerPassed=false;
 bool FriendRequested=false, FriendAccepted=false, PartyAccepted=false;
 double NextAction=0, NextInvite=0;
 FString OriginalAccountId, PeerAccountId;
 FTowerSocialProbe() { FWorldDelegates::OnWorldPostActorTick.AddRaw(this,&FTowerSocialProbe::Tick); }
 ~FTowerSocialProbe() { FWorldDelegates::OnWorldPostActorTick.RemoveAll(this); }

 void Tick(UWorld* World,ELevelTick,float)
 {
  if(!FParse::Param(FCommandLine::Get(),TEXT("TowerSocialProbe")) || !World || World->WorldType!=EWorldType::Game) return;
  auto* GS=World->GetGameState(); if(!GS) return;
  if(World->GetNetMode()==NM_DedicatedServer)
  {
   FString ServerId; FParse::Value(FCommandLine::Get(),TEXT("TowerServerId="),ServerId);
   TArray<ATowerPlayerState*> Players;
   for(APlayerState* Base:GS->PlayerArray)
    if(auto* PS=Cast<ATowerPlayerState>(Base);PS && !PS->IsInactive() && !PS->AccountId.IsEmpty()) Players.Add(PS);
   if(!ServerHadInitialPlayer && Players.Num()==1)
   {
    const bool Expected=ServerId==TEXT("hub_a") ? Players[0]->AccountUsername.EndsWith(TEXT("_a")) : Players[0]->AccountUsername.EndsWith(TEXT("_b"));
    if(Expected) { ServerHadInitialPlayer=true; UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_PROBE INITIAL_SERVER_PASS Hub=%s"),*ServerId); }
   }
   if(!ServerHadInitialPlayer || ServerPassed) return;
   if(ServerId==TEXT("hub_b") && GS->PlayerArray.IsEmpty())
   {
    ServerPassed=true;
    UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_PROBE SOURCE_EMPTY_PASS"));
   }
   else if(ServerId==TEXT("hub_a") && Players.Num()==2 && Players[0]->PartyId.IsValid() && Players[0]->PartyId==Players[1]->PartyId && Players[0]->AccountId!=Players[1]->AccountId)
   {
    bool HubOnly=true;
    for(auto* PS:Players)
    {
     const auto* Hero=Cast<ATowerCharacter>(PS->GetPawn());
     HubOnly &= Hero && Hero->TrialIndex==-1 && PS->QueuedTrial==-1 && !PS->MatchId.IsValid();
    }
    if(HubOnly) { ServerPassed=true; UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_PROBE DESTINATION_SERVER_PASS Players=2 PartyMembers=2")); }
   }
   return;
  }
  if(World->GetNetMode()!=NM_Client || ClientPassed) return;
  auto* PC=World->GetFirstPlayerController();
  auto* Account=World->GetGameInstance()->GetSubsystem<UTowerAccountSubsystem>();
  auto* Social=World->GetGameInstance()->GetSubsystem<UTowerSocialSubsystem>();
  auto* Party=PC?PC->FindComponentByClass<UTowerPartyComponent>():nullptr;
  if(!PC || !Account || !Social || !Party) return;
  FString User,Peer,Role;
  if(!FParse::Value(FCommandLine::Get(),TEXT("TowerProbeUser="),User) || !User.StartsWith(TEXT("social_")) ||
     !FParse::Value(FCommandLine::Get(),TEXT("TowerProbePeer="),Peer) ||
     !FParse::Value(FCommandLine::Get(),TEXT("TowerSocialRole="),Role)) return;
  const bool Leader=Role==TEXT("a");
  if(!AuthStarted)
  {
   AuthStarted=true;
   Account->Register(User,FGuid::NewGuid().ToString()+TEXT("Aa_9"));
  }
  auto* Self=PC->GetPlayerState<ATowerPlayerState>();
  if(!Self || !Party->IsAuthenticated()) return;
  const double Now=FPlatformTime::Seconds();
  if(!InitialHubVerified)
  {
   const FString Expected=Leader?TEXT("hub_a"):TEXT("hub_b");
   if(Party->GetSocialServerId()!=Expected) return;
   InitialHubVerified=true; OriginalAccountId=Self->AccountId;
   UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_PROBE INITIAL_CLIENT_PASS Role=%s Hub=%s"),*Role,*Expected);
  }
  if(Self->AccountId!=OriginalAccountId)
  { UE_LOG(LogTemp,Error,TEXT("TOWER_SOCIAL_PROBE FAIL Account identity changed during travel")); ClientPassed=true; return; }

  // Names are discovered through the backend; no shared server PlayerArray lookup.
  if(Leader && PeerAccountId.IsEmpty() && !Social->IsBusy() && Now>=NextAction)
  {
   for(const auto& Result:Social->GetSearchResults()) if(Result.Username.Equals(Peer,ESearchCase::IgnoreCase)) { PeerAccountId=Result.Id;break; }
   if(PeerAccountId.IsEmpty()) { Social->SearchUsers(Peer.ToUpper());NextAction=Now+3; }
  }
  if(Leader && !PeerAccountId.IsEmpty() && !FriendRequested && !Social->IsBusy() && Now>=NextAction)
  {
   Social->RequestFriend(PeerAccountId);FriendRequested=true;NextAction=Now+3;
   UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_PROBE FRIEND_REQUEST_SENT"));
  }
  if(!Leader && !FriendAccepted && !Social->IsBusy())
   for(const auto& Request:Social->GetIncomingRequests()) if(Request.Username.Equals(Peer,ESearchCase::IgnoreCase))
   { PeerAccountId=Request.UserId; Social->RespondFriend(Request.Id,true);FriendAccepted=true;break; }
  const FTowerSocialUser* Friend=Social->GetFriends().FindByPredicate([&](const FTowerSocialUser& Entry){return Entry.Username.Equals(Peer,ESearchCase::IgnoreCase);});
  if(Friend && PeerAccountId.IsEmpty()) PeerAccountId=Friend->Id;
  if(Leader && Friend && Friend->bOnline && Friend->ServerId==TEXT("hub_b") && Friend->Status==TEXT("hub") && Now>=NextInvite)
  {
   Party->InviteFriend(Friend->Id);NextInvite=Now+10;
   UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_PROBE CROSS_HUB_INVITE_SENT"));
  }
  if(!Leader && !PartyAccepted && !Social->IsBusy())
   for(const auto& Invite:Social->GetPartyInvites()) if(Invite.Username.Equals(Peer,ESearchCase::IgnoreCase) && Invite.ServerId==TEXT("hub_a"))
   {
    Social->RespondPartyInvite(Invite.Id,true);PartyAccepted=true;
    UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_PROBE CROSS_HUB_INVITE_ACCEPTED"));break;
   }
  if(Party->GetSocialServerId()!=TEXT("hub_a") || Party->GetPartyMembers().Num()!=2 || !Self->PartyId.IsValid() || !Friend || Friend->Id!=PeerAccountId) return;
  bool FoundPeer=false;
  for(auto* Member:Party->GetPartyMembers()) if(Member!=Self && Member->AccountId==PeerAccountId && Member->PartyId==Self->PartyId) FoundPeer=true;
  if(FoundPeer && (Leader || PartyAccepted))
  {
   ClientPassed=true;
   UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_PROBE CLIENT_PASS Role=%s Friends=1 Party=2 Hub=hub_a IdentityPreserved=1"),*Role);
  }
 }
} GSocialProbe;
}
#endif
