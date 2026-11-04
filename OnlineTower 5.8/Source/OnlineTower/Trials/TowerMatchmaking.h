#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Actor.h"
#include "TowerMatchmaking.generated.h"

class ATowerPlayerState;
class ATowerPortal;
class APlayerController;

// A local server owns one isolated spatial roster per trial; no unrelated player travels.
UCLASS()
class ONLINETOWER_API UTowerMatchmakingSubsystem : public UTickableWorldSubsystem
{
 GENERATED_BODY()
public:
 virtual void Tick(float DeltaTime) override;
 virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(TowerMatchmaking, STATGROUP_Tickables); }
 static int32 Capacity(int32 Trial) { return Trial==0 ? 5 : Trial==1 ? 2 : Trial==2 ? 12 : 0; }
 static constexpr float AcceptSeconds = 10.f;
 void RequestQueue(APlayerController* Player, int32 Trial);
 void CancelQueue(APlayerController* Player);
 void AcceptMatch(APlayerController* Player);
 void ReturnToHub(APlayerController* Player);
private:
 struct FTicket
 {
  TArray<TWeakObjectPtr<ATowerPlayerState>> Members;
  TArray<FString> Accounts;
  FGuid PartyId;
  int32 Trial=-1;
  TWeakObjectPtr<ATowerPortal> Portal;
 };
 struct FPendingMatch
 {
  TArray<TWeakObjectPtr<ATowerPlayerState>> Members;
  TArray<FString> Accounts;
  TArray<FGuid> PartyIds;
  TArray<TWeakObjectPtr<ATowerPortal>> Portals;
  TArray<bool> Accepted;
  int32 Trial=-1;
  float AcceptDeadline=0;
 };
 TArray<FTicket> Tickets;
 TArray<FPendingMatch> Pending;
 bool bZonesReady=false;
 float Elapsed=0;
 void EnsureZones();
 bool ValidTicket(const FTicket& Ticket) const;
 bool ValidPending(const FPendingMatch& Match) const;
 void ClearQueue(ATowerPlayerState* PS) const;
 void RemoveTicket(int32 Index, const FString& Reason);
 void DropPendingPlayer(int32 MatchIndex, int32 MemberIndex, const FString& Reason, bool bRequeueAccepted);
 void CancelPendingContaining(ATowerPlayerState* PS, const FString& Reason);
 void ProposeReadyMatches();
 void ResolvePendingMatches();
 bool CommitMatch(FPendingMatch& Match);
};

UCLASS()
class ONLINETOWER_API ATowerQueuedDungeonEnvironment : public AActor
{
 GENERATED_BODY()
public: ATowerQueuedDungeonEnvironment();
};
UCLASS()
class ONLINETOWER_API ATowerQueuedDuelEnvironment : public AActor
{
 GENERATED_BODY()
public: ATowerQueuedDuelEnvironment();
};
UCLASS()
class ONLINETOWER_API ATowerQueuedFinalEnvironment : public AActor
{
 GENERATED_BODY()
public: ATowerQueuedFinalEnvironment();
};
