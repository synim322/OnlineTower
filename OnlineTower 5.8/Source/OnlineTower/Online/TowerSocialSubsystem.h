#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "TowerSocialSubsystem.generated.h"

struct FTowerSocialUser
{
 FString Id, Username, Status, ServerId;
 bool bOnline=false;
};
struct FTowerSocialRequest
{
 FString Id, UserId, Username;
};
struct FTowerSocialPartyInvite : FTowerSocialRequest
{
 FString ServerId;
 double ExpiresAt=0;
};

// Account-scoped state survives travel. No server credentials are held here.
UCLASS()
class ONLINETOWER_API UTowerSocialSubsystem : public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 void SearchUsers(const FString& Query);
 void RequestFriend(const FString& TargetId);
 void RespondFriend(const FString& RequestId,bool bAccept);
 void CancelFriendRequest(const FString& RequestId);
 void RemoveFriend(const FString& TargetId);
 void RespondPartyInvite(const FString& InviteId,bool bAccept);
 void Refresh();
 const TArray<FTowerSocialUser>& GetSearchResults() const { return SearchResults; }
 const TArray<FTowerSocialUser>& GetFriends() const { return Friends; }
 const TArray<FTowerSocialRequest>& GetIncomingRequests() const { return IncomingRequests; }
 const TArray<FTowerSocialRequest>& GetOutgoingRequests() const { return OutgoingRequests; }
 const TArray<FTowerSocialPartyInvite>& GetPartyInvites() const { return PartyInvites; }
 const FString& GetStatus() const { return Status; }
 bool IsBusy() const { return bActionInFlight || bSearchInFlight; }
 void JoinFinished(bool bSuccess,const FString& Message);
private:
 bool Tick(float Delta);
 void ResetForAccount(const FString& Token);
 void Heartbeat();
 void Post(const FString& Route,const TSharedRef<class FJsonObject>& Body,const FString& SuccessText,
           TFunction<void(const TSharedPtr<class FJsonObject>&)> OnSuccess={});
 bool CanAcceptParty(FString& Error) const;
 FString CurrentToken() const;
 FString LastToken, Status;
 TArray<FTowerSocialUser> SearchResults, Friends;
 TArray<FTowerSocialRequest> IncomingRequests, OutgoingRequests;
 TArray<FTowerSocialPartyInvite> PartyInvites;
 FTSTicker::FDelegateHandle TickerHandle;
 uint32 Generation=0, SearchGeneration=0;
 bool bActionInFlight=false,bSearchInFlight=false,bRefreshInFlight=false,bHeartbeatInFlight=false;
 double NextRefresh=0,NextHeartbeat=0;
 FString PendingJoinTicket,PendingServerId;
 double JoinDeadline=0;
 bool bRedeemSent=false;
};
