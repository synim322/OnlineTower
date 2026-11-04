#include "TowerMatchmaking.h"
#include "TowerAbilities.h"
#include "TowerCharacter.h"
#include "TowerPartyComponent.h"
#include "TowerWorld.h"
#include "TowerMaps.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
 APlayerController* Controller(ATowerPlayerState* PS) { return PS ? Cast<APlayerController>(PS->GetOwner()) : nullptr; }
 UTowerPartyComponent* Party(ATowerPlayerState* PS) { auto* PC=Controller(PS); return PC ? PC->FindComponentByClass<UTowerPartyComponent>() : nullptr; }
 void Notice(ATowerPlayerState* PS,const FString& Text) { if (auto* P=Party(PS)) P->ClientNotice(Text); }
}

ATowerQueuedDungeonEnvironment::ATowerQueuedDungeonEnvironment() { bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); BuildTowerEnvironment(this,0,false); }
ATowerQueuedDuelEnvironment::ATowerQueuedDuelEnvironment() { bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); BuildTowerEnvironment(this,1,false); }
ATowerQueuedFinalEnvironment::ATowerQueuedFinalEnvironment() { bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); BuildTowerEnvironment(this,2,false); }

void UTowerMatchmakingSubsystem::ClearQueue(ATowerPlayerState* PS) const
{
 if (!PS) return;
 PS->QueuedTrial=-1; PS->QueueStartedAt=0; PS->QueuePopulation=0;
 PS->MatchAcceptEndsAt=0; PS->bMatchAccepted=false; PS->ForceNetUpdate();
}

void UTowerMatchmakingSubsystem::EnsureZones()
{
 if (bZonesReady) return;
 const int32 Zone=TowerMaps::GetZone(GetWorld());
 if (Zone!=-1 && Zone!=-2) return;
 if (Zone==-2) { bZonesReady=true; return; } // Legacy sandbox already contains all three environments and returns.
 TArray<AActor*> Spawned;
 bool bComplete=true;
 UClass* Classes[]={ATowerQueuedDungeonEnvironment::StaticClass(),ATowerQueuedDuelEnvironment::StaticClass(),ATowerQueuedFinalEnvironment::StaticClass()};
 for (int32 Trial=0;Trial<3;++Trial)
 {
  const FVector Center=TowerTrialLocation(Trial);
  auto* Environment=GetWorld()->SpawnActor<AActor>(Classes[Trial],Center-FVector(0,0,120),FRotator::ZeroRotator);
  if (!Environment) { bComplete=false; break; }
  Spawned.Add(Environment);
  const FTransform Transform(FRotator::ZeroRotator,Center+FVector(-950,0,85));
  auto* Portal=GetWorld()->SpawnActorDeferred<ATowerPortal>(ATowerPortal::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if (!Portal) { bComplete=false; break; }
  Spawned.Add(Portal); Portal->DestinationTrial=-1; Portal->Destination=TowerHubOrigin()+FVector(-500,0,120); Portal->FinishSpawning(Transform);
  // Final is capacity-only: do not add combat mechanics or targets there.
  if (Trial!=2) for (int32 I=0;I<3;++I)
   if (auto* Dummy=GetWorld()->SpawnActor<ATowerPracticeDummy>(Center+FVector(200,(I-1)*250,-45),FRotator::ZeroRotator)) Spawned.Add(Dummy);
 }
 bZonesReady=bComplete;
 if (!bComplete) for (auto* Actor:Spawned) if (IsValid(Actor)) Actor->Destroy();
}

void UTowerMatchmakingSubsystem::RequestQueue(APlayerController* PC,int32 Trial)
{
 if (!PC || !PC->HasAuthority() || GetWorld()->GetNetMode()==NM_Client || Capacity(Trial)==0) return;
 auto* PS=PC->GetPlayerState<ATowerPlayerState>();
 auto* Hero=Cast<ATowerCharacter>(PC->GetPawn());
 if (!PS || !Hero || PS->MatchId.IsValid() || Hero->TrialIndex!=-1) return;
 if (PS->AccountId.IsEmpty()) { Notice(PS,TEXT("Для поиска матча войдите в аккаунт (Esc → аккаунт).")); return; }
 if (PS->QueuedTrial>=0) return;
 EnsureZones();
 if (!bZonesReady) { Notice(PS,TEXT("Поиск доступен из общего хаба.")); return; }
 ATowerPortal* Portal=nullptr;
 for (TActorIterator<ATowerPortal> It(GetWorld());It;++It)
  if (It->DestinationTrial==Trial && It->IsOverlappingActor(Hero)) { Portal=*It; break; }
 if (!Portal || Hero->MatchQueueBlockedPortal.Get()==Portal) return;
 FTicket Ticket; Ticket.Trial=Trial; Ticket.PartyId=PS->PartyId; Ticket.Portal=Portal;
 TArray<ATowerPlayerState*> Members;
 if (auto* P=Party(PS)) Members=P->GetPartyMembers();
 if (Members.IsEmpty()) Members.Add(PS);
 if (Members.Num()>Capacity(Trial))
 {
  Notice(PS,FString::Printf(TEXT("Группа: %d. В испытании максимум %d. Поиск не начат — уменьшите группу."),Members.Num(),Capacity(Trial)));
  return;
 }
 for (auto* Member:Members)
 {
  auto* Pawn=Cast<ATowerCharacter>(Member->GetPawn());
  if (!Pawn || Member->AccountId.IsEmpty() || Member->Health()<=0 || Member->MatchId.IsValid() || Member->QueuedTrial>=0 || Pawn->TrialIndex!=-1 ||
      !Portal->IsOverlappingActor(Pawn) || Pawn->NextPortalTime>GetWorld()->GetTimeSeconds() || Pawn->MatchQueueBlockedPortal.Get()==Portal)
  {
   if (Members.Num()>1)
    Notice(PS,TEXT("Ожидание группы: все участники пати должны стоять в этом портале."));
   else
    Notice(PS,TEXT("Не удалось начать поиск. Выйдите и зайдите в портал снова."));
   return;
  }
  Ticket.Members.Add(Member); Ticket.Accounts.Add(Member->AccountId);
 }
 for (auto* Member:Members)
 {
  Member->QueuedTrial=Trial; Member->QueueStartedAt=GetWorld()->GetTimeSeconds();
  Member->QueuePopulation=Members.Num(); Member->MatchAcceptEndsAt=0; Member->bMatchAccepted=false; Member->ForceNetUpdate();
  Notice(Member,TEXT("Поиск матча начат. Можно отойти от портала; отмена и таймер — в меню поиска."));
 }
 Tickets.Add(MoveTemp(Ticket));
 UE_LOG(LogTemp,Display,TEXT("TOWER_QUEUE_JOIN Trial=%d Members=%d"),Trial,Members.Num());
}

bool UTowerMatchmakingSubsystem::ValidTicket(const FTicket& Ticket) const
{
 const auto* GS=GetWorld()->GetGameState();
 if (!GS || Ticket.Members.IsEmpty()) return false;
 for (int32 I=0;I<Ticket.Members.Num();++I)
 {
  auto* PS=Ticket.Members[I].Get();
  auto* Hero=PS ? Cast<ATowerCharacter>(PS->GetPawn()) : nullptr;
  if (!PS || !Controller(PS) || PS->IsInactive() || !GS->PlayerArray.Contains(PS) || PS->AccountId.IsEmpty() || PS->AccountId!=Ticket.Accounts[I] ||
      PS->PartyId!=Ticket.PartyId || PS->QueuedTrial!=Ticket.Trial || PS->MatchId.IsValid() || !Hero || Hero->TrialIndex!=-1 || PS->Health()<=0) return false;
  if (Ticket.PartyId.IsValid())
  {
   auto* P=Party(PS);
   if (!P || P->GetPartyMembers().Num()!=Ticket.Members.Num()) return false;
  }
 }
 return true;
}

bool UTowerMatchmakingSubsystem::ValidPending(const FPendingMatch& Match) const
{
 const auto* GS=GetWorld()->GetGameState();
 if (!GS || Match.Members.Num()!=Match.Accounts.Num() || Match.Members.IsEmpty()) return false;
 for (int32 I=0;I<Match.Members.Num();++I)
 {
  auto* PS=Match.Members[I].Get();
  auto* Hero=PS ? Cast<ATowerCharacter>(PS->GetPawn()) : nullptr;
  if (!PS || !Controller(PS) || PS->IsInactive() || !GS->PlayerArray.Contains(PS) || PS->AccountId.IsEmpty() || PS->AccountId!=Match.Accounts[I] ||
      PS->QueuedTrial!=Match.Trial || PS->MatchId.IsValid() || !Hero || Hero->TrialIndex!=-1 || PS->Health()<=0) return false;
 }
 return true;
}

void UTowerMatchmakingSubsystem::RemoveTicket(int32 Index,const FString& Reason)
{
 if (!Tickets.IsValidIndex(Index)) return;
 const auto Ticket=Tickets[Index]; Tickets.RemoveAt(Index);
 for (const auto& Weak:Ticket.Members) if (auto* PS=Weak.Get())
 {
  ClearQueue(PS);
  if (auto* Hero=Cast<ATowerCharacter>(PS->GetPawn())) Hero->MatchQueueBlockedPortal=Ticket.Portal;
  if (!Reason.IsEmpty()) Notice(PS,Reason);
 }
}

void UTowerMatchmakingSubsystem::CancelPendingContaining(ATowerPlayerState* PS,const FString& Reason)
{
 for (int32 M=Pending.Num()-1;M>=0;--M)
 {
  const int32 MemberIndex=Pending[M].Members.IndexOfByPredicate([PS](const TWeakObjectPtr<ATowerPlayerState>& Weak){ return Weak.Get()==PS; });
  if (MemberIndex==INDEX_NONE) continue;
  DropPendingPlayer(M,MemberIndex,Reason,true);
  return;
 }
}

void UTowerMatchmakingSubsystem::DropPendingPlayer(int32 MatchIndex,int32 MemberIndex,const FString& Reason,bool bRequeueAccepted)
{
 if (!Pending.IsValidIndex(MatchIndex) || !Pending[MatchIndex].Members.IsValidIndex(MemberIndex)) return;
 FPendingMatch Match=Pending[MatchIndex];
 Pending.RemoveAt(MatchIndex);
 auto* Leaving=Match.Members[MemberIndex].Get();
 if (Leaving)
 {
  ClearQueue(Leaving);
  if (Match.Portals.IsValidIndex(MemberIndex))
   if (auto* Hero=Cast<ATowerCharacter>(Leaving->GetPawn())) Hero->MatchQueueBlockedPortal=Match.Portals[MemberIndex];
  Notice(Leaving,Reason.IsEmpty() ? TEXT("Вы вышли из поиска матча.") : Reason);
 }
 if (!bRequeueAccepted) return;
 // Remaining players who already accepted stay searching; others also return to the open queue.
 TMap<FGuid,FTicket> ByParty;
 for (int32 I=0;I<Match.Members.Num();++I)
 {
  if (I==MemberIndex) continue;
  auto* PS=Match.Members[I].Get();
  if (!PS || PS->AccountId!=Match.Accounts[I] || PS->QueuedTrial!=Match.Trial) continue;
  PS->MatchAcceptEndsAt=0; PS->bMatchAccepted=false; PS->ForceNetUpdate();
  Notice(PS,TEXT("Кто-то не принял матч. Поиск продолжается."));
  const FGuid TicketKey=Match.PartyIds.IsValidIndex(I) ? Match.PartyIds[I] : FGuid();
  if (!TicketKey.IsValid())
  {
   FTicket Solo; Solo.Trial=Match.Trial; Solo.PartyId=PS->PartyId; Solo.Portal=Match.Portals.IsValidIndex(I) ? Match.Portals[I] : nullptr;
   Solo.Members.Add(PS); Solo.Accounts.Add(PS->AccountId); Tickets.Add(MoveTemp(Solo));
   continue;
  }
  FTicket& Ticket=ByParty.FindOrAdd(TicketKey);
  Ticket.Trial=Match.Trial; Ticket.PartyId=TicketKey;
  if (Match.Portals.IsValidIndex(I) && !Ticket.Portal.IsValid()) Ticket.Portal=Match.Portals[I];
  Ticket.Members.Add(PS); Ticket.Accounts.Add(PS->AccountId);
 }
 for (auto& Pair:ByParty) Tickets.Add(MoveTemp(Pair.Value));
 UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_ACCEPT_FAIL Trial=%d Left=%s"),Match.Trial,Leaving?*Leaving->AccountUsername:TEXT("?"));
}

void UTowerMatchmakingSubsystem::CancelQueue(APlayerController* PC)
{
 if (!PC || !PC->HasAuthority()) return;
 auto* PS=PC->GetPlayerState<ATowerPlayerState>();
 if (!PS) return;
 CancelPendingContaining(PS,TEXT("Поиск отменён. Выйдите из портала перед повторным входом."));
 for (int32 I=Tickets.Num()-1;I>=0;--I)
  if (Tickets[I].Members.Contains(PS)) RemoveTicket(I,TEXT("Поиск отменён для всей группы. Выйдите из портала перед повторным входом."));
}

void UTowerMatchmakingSubsystem::AcceptMatch(APlayerController* PC)
{
 if (!PC || !PC->HasAuthority()) return;
 auto* PS=PC->GetPlayerState<ATowerPlayerState>();
 if (!PS || PS->QueuedTrial<0 || PS->MatchAcceptEndsAt<=0 || PS->bMatchAccepted) return;
 for (auto& Match:Pending)
 {
  for (int32 I=0;I<Match.Members.Num();++I)
  {
   if (Match.Members[I].Get()!=PS) continue;
   Match.Accepted[I]=true; PS->bMatchAccepted=true; PS->ForceNetUpdate();
   Notice(PS,TEXT("Матч принят. Ожидание остальных игроков…"));
   UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_ACCEPT Player=%s Trial=%d"),*PS->AccountUsername,Match.Trial);
   return;
  }
 }
}

void UTowerMatchmakingSubsystem::ReturnToHub(APlayerController* PC)
{
 if (!PC || !PC->HasAuthority()) return;
 CancelQueue(PC);
 auto* PS=PC->GetPlayerState<ATowerPlayerState>(); auto* Hero=Cast<ATowerCharacter>(PC->GetPawn());
 if (!PS || !Hero) return;
 FVector Arrival=TowerHubOrigin()+FVector(-500,(PS->GetPlayerId()%5)*150-300,120);
 if (!GetWorld()->FindTeleportSpot(Hero,Arrival,FRotator::ZeroRotator)) return;
 Hero->GetCharacterMovement()->StopMovementImmediately();
 if (!Hero->TeleportTo(Arrival,FRotator::ZeroRotator,false,true)) return;
 Hero->TrialIndex=-1; Hero->NextPortalTime=GetWorld()->GetTimeSeconds()+2; Hero->ForceNetUpdate();
 PS->MatchId.Invalidate(); PS->ForceNetUpdate();
 Notice(PS,TEXT("Вы вернулись в хаб."));
}

void UTowerMatchmakingSubsystem::Tick(float DeltaTime)
{
 if (!GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->GetNetMode()==NM_Client || GetWorld()->GetNetMode()==NM_Standalone) return;
 Elapsed+=DeltaTime; if (Elapsed<.25f) return; Elapsed=0;
 for (int32 I=Tickets.Num()-1;I>=0;--I) if (!ValidTicket(Tickets[I])) RemoveTicket(I,TEXT("Поиск отменён: состав группы или подключение изменилось."));
 for (int32 I=Pending.Num()-1;I>=0;--I)
 {
  if (!ValidPending(Pending[I]))
  {
   // Invalidate broken accept phase without requeueing ghosts.
   FPendingMatch Broken=Pending[I]; Pending.RemoveAt(I);
   for (int32 M=0;M<Broken.Members.Num();++M) if (auto* PS=Broken.Members[M].Get())
   {
    ClearQueue(PS);
    if (Broken.Portals.IsValidIndex(M)) if (auto* Hero=Cast<ATowerCharacter>(PS->GetPawn())) Hero->MatchQueueBlockedPortal=Broken.Portals[M];
    Notice(PS,TEXT("Поиск отменён: состав или подключение изменились."));
   }
  }
 }
 if (const auto* GS=GetWorld()->GetGameState()) for (APlayerState* Base:GS->PlayerArray)
  if (auto* PS=Cast<ATowerPlayerState>(Base); PS && PS->MatchId.IsValid())
  {
   auto* Hero=Cast<ATowerCharacter>(PS->GetPawn());
   if (!Hero || Hero->TrialIndex<0 || PS->IsInactive()) { PS->MatchId.Invalidate(); PS->ForceNetUpdate(); }
   else if (PS->AccountId.IsEmpty()) ReturnToHub(Controller(PS));
  }
 ResolvePendingMatches();
 ProposeReadyMatches();
 for (int32 Trial=0;Trial<3;++Trial)
 {
  int32 Count=0;
  for (const auto& Ticket:Tickets) if (Ticket.Trial==Trial) Count+=Ticket.Members.Num();
  for (const auto& Match:Pending) if (Match.Trial==Trial) Count+=Match.Members.Num();
  for (const auto& Ticket:Tickets) if (Ticket.Trial==Trial) for (const auto& Weak:Ticket.Members) if (auto* PS=Weak.Get())
   if (PS->QueuePopulation!=Count) { PS->QueuePopulation=Count; PS->ForceNetUpdate(); }
  for (const auto& Match:Pending) if (Match.Trial==Trial) for (const auto& Weak:Match.Members) if (auto* PS=Weak.Get())
   if (PS->QueuePopulation!=Count) { PS->QueuePopulation=Count; PS->ForceNetUpdate(); }
 }
}

void UTowerMatchmakingSubsystem::ResolvePendingMatches()
{
 const float Now=GetWorld()->GetTimeSeconds();
 for (int32 M=Pending.Num()-1;M>=0;--M)
 {
  auto& Match=Pending[M];
  bool bAllAccepted=true;
  for (int32 I=0;I<Match.Accepted.Num();++I)
  {
   if (auto* PS=Match.Members[I].Get()) Match.Accepted[I]=Match.Accepted[I] || PS->bMatchAccepted;
   if (!Match.Accepted[I]) bAllAccepted=false;
  }
  if (bAllAccepted)
  {
   FPendingMatch Ready=Match; Pending.RemoveAt(M);
   if (!CommitMatch(Ready))
   {
    // Teleport failed: restore open queue tickets for everyone.
    for (int32 I=0;I<Ready.Members.Num();++I) if (auto* PS=Ready.Members[I].Get())
    {
     PS->MatchAcceptEndsAt=0; PS->bMatchAccepted=false; PS->ForceNetUpdate();
     FTicket Solo; Solo.Trial=Ready.Trial; Solo.PartyId=Ready.PartyIds.IsValidIndex(I)?Ready.PartyIds[I]:PS->PartyId;
     Solo.Portal=Ready.Portals.IsValidIndex(I)?Ready.Portals[I]:nullptr; Solo.Members.Add(PS); Solo.Accounts.Add(PS->AccountId);
     Tickets.Add(MoveTemp(Solo));
     Notice(PS,TEXT("Не удалось начать испытание. Поиск продолжается."));
    }
   }
   continue;
  }
  if (Now<Match.AcceptDeadline) continue;
  // Timeout: drop every player who has not accepted. Process from the end so indices stay valid via rebuild.
  TArray<int32> TimedOut;
  for (int32 I=0;I<Match.Accepted.Num();++I) if (!Match.Accepted[I]) TimedOut.Add(I);
  if (TimedOut.IsEmpty()) continue;
  // Drop the first timed-out player; DropPendingPlayer requeues the rest (including other non-accepters as open tickets).
  // Non-accepters should leave search entirely — remove them after requeue of accepters only.
  FPendingMatch Snapshot=Match; Pending.RemoveAt(M);
  TMap<FGuid,FTicket> AcceptedParties;
  for (int32 I=0;I<Snapshot.Members.Num();++I)
  {
   auto* PS=Snapshot.Members[I].Get();
   if (!PS) continue;
   if (!Snapshot.Accepted[I])
   {
    ClearQueue(PS);
    if (Snapshot.Portals.IsValidIndex(I)) if (auto* Hero=Cast<ATowerCharacter>(PS->GetPawn())) Hero->MatchQueueBlockedPortal=Snapshot.Portals[I];
    Notice(PS,TEXT("Время на принятие истекло (10 с). Вы вышли из поиска."));
    UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_ACCEPT_TIMEOUT Player=%s Trial=%d"),*PS->AccountUsername,Snapshot.Trial);
    continue;
   }
   PS->MatchAcceptEndsAt=0; PS->bMatchAccepted=false; PS->ForceNetUpdate();
   Notice(PS,TEXT("Кто-то не принял матч вовремя. Поиск продолжается."));
   const FGuid PartyKey=Snapshot.PartyIds.IsValidIndex(I) ? Snapshot.PartyIds[I] : FGuid();
   if (!PartyKey.IsValid())
   {
    FTicket Solo; Solo.Trial=Snapshot.Trial; Solo.PartyId=PS->PartyId;
    Solo.Portal=Snapshot.Portals.IsValidIndex(I)?Snapshot.Portals[I]:nullptr;
    Solo.Members.Add(PS); Solo.Accounts.Add(PS->AccountId); Tickets.Add(MoveTemp(Solo));
   }
   else
   {
    FTicket& Ticket=AcceptedParties.FindOrAdd(PartyKey);
    Ticket.Trial=Snapshot.Trial; Ticket.PartyId=PartyKey;
    if (Snapshot.Portals.IsValidIndex(I) && !Ticket.Portal.IsValid()) Ticket.Portal=Snapshot.Portals[I];
    Ticket.Members.Add(PS); Ticket.Accounts.Add(PS->AccountId);
   }
  }
  for (auto& Pair:AcceptedParties) Tickets.Add(MoveTemp(Pair.Value));
 }
}

void UTowerMatchmakingSubsystem::ProposeReadyMatches()
{
 for (int32 Trial=0;Trial<3;++Trial)
 {
  bool bOccupied=false;
  for (TActorIterator<ATowerCharacter> It(GetWorld());It;++It)
   if (const auto* PS=It->GetPlayerState<ATowerPlayerState>(); It->TrialIndex==Trial && PS && PS->MatchId.IsValid()) { bOccupied=true; break; }
  for (const auto& Match:Pending) if (Match.Trial==Trial) { bOccupied=true; break; }
  if (bOccupied) continue;
  const int32 Needed=Capacity(Trial);
  TArray<TArray<int32>> Picks; Picks.SetNum(Needed+1);
  TArray<bool> Reachable; Reachable.Init(false,Needed+1); Reachable[0]=true;
  for (int32 I=0;I<Tickets.Num();++I) if (Tickets[I].Trial==Trial)
  {
   const int32 Size=Tickets[I].Members.Num();
   for (int32 Sum=Needed;Sum>=Size;--Sum) if (!Reachable[Sum] && Reachable[Sum-Size])
   { Picks[Sum]=Picks[Sum-Size]; Picks[Sum].Add(I); Reachable[Sum]=true; }
  }
  if (!Reachable[Needed]) continue;
  FPendingMatch Match; Match.Trial=Trial; Match.AcceptDeadline=GetWorld()->GetTimeSeconds()+AcceptSeconds;
  for (int32 Index:Picks[Needed])
  {
   const FTicket& Ticket=Tickets[Index];
   for (int32 M=0;M<Ticket.Members.Num();++M)
   {
    Match.Members.Add(Ticket.Members[M]); Match.Accounts.Add(Ticket.Accounts[M]);
    Match.PartyIds.Add(Ticket.PartyId); Match.Portals.Add(Ticket.Portal); Match.Accepted.Add(false);
   }
  }
  auto Selected=Picks[Needed]; Selected.Sort(TGreater<int32>());
  for (int32 Index:Selected) Tickets.RemoveAt(Index);
  for (int32 I=0;I<Match.Members.Num();++I) if (auto* PS=Match.Members[I].Get())
  {
   PS->MatchAcceptEndsAt=Match.AcceptDeadline; PS->bMatchAccepted=false; PS->ForceNetUpdate();
   Notice(PS,TEXT("Матч найден! Примите игру за 10 секунд."));
  }
  Pending.Add(MoveTemp(Match));
  UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_PROPOSED Trial=%d Players=%d AcceptSec=%.0f"),Trial,Needed,AcceptSeconds);
 }
}

bool UTowerMatchmakingSubsystem::CommitMatch(FPendingMatch& Match)
{
 TArray<ATowerPlayerState*> Roster;
 for (const auto& Weak:Match.Members) Roster.Add(Weak.Get());
 TArray<FVector> Arrivals; bool bFits=true;
 for (int32 I=0;I<Roster.Num();++I)
 {
  auto* Hero=Roster[I] ? Cast<ATowerCharacter>(Roster[I]->GetPawn()) : nullptr;
  FVector Position=TowerTrialLocation(Match.Trial)+FVector(-600+(I/5)*150,(I%5-2)*150,0);
  if (!Hero || !GetWorld()->FindTeleportSpot(Hero,Position,FRotator::ZeroRotator)) { bFits=false; break; }
  Arrivals.Add(Position);
 }
 if (!bFits) return false;
 TArray<FTransform> OriginalTransforms;
 for (auto* PS:Roster) OriginalTransforms.Add(PS->GetPawn()->GetActorTransform());
 int32 Moved=0;
 for (;Moved<Roster.Num();++Moved)
 {
  auto* Hero=CastChecked<ATowerCharacter>(Roster[Moved]->GetPawn());
  Hero->GetCharacterMovement()->StopMovementImmediately();
  if (!Hero->TeleportTo(Arrivals[Moved],FRotator::ZeroRotator,false,true)) break;
 }
 if (Moved!=Roster.Num())
 {
  for (int32 I=0;I<Moved;++I) Roster[I]->GetPawn()->SetActorTransform(OriginalTransforms[I],false,nullptr,ETeleportType::TeleportPhysics);
  UE_LOG(LogTemp,Warning,TEXT("TOWER_MATCH_TELEPORT_ROLLBACK Trial=%d"),Match.Trial);
  return false;
 }
 const FGuid MatchId=FGuid::NewGuid();
 for (int32 I=0;I<Roster.Num();++I)
 {
  auto* PS=Roster[I]; auto* Hero=CastChecked<ATowerCharacter>(PS->GetPawn());
  Hero->TrialIndex=Match.Trial; Hero->NextPortalTime=GetWorld()->GetTimeSeconds()+2; Hero->ForceNetUpdate();
  PS->MatchId=MatchId; ClearQueue(PS); Notice(PS,TEXT("Матч начат. Испытание готово."));
 }
 UE_LOG(LogTemp,Display,TEXT("TOWER_MATCH_STARTED Trial=%d Players=%d Match=%s"),Match.Trial,Roster.Num(),*MatchId.ToString());
 return true;
}
