#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TowerPartyComponent.generated.h"

class ATowerPlayerState;

USTRUCT()
struct FTowerPartyInvite
{
 GENERATED_BODY()
 UPROPERTY() int32 InviterPlayerId = -1;
 UPROPERTY() FString InviterName;
 UPROPERTY() FString InviterAccountId;
 UPROPERTY() FGuid InviterPartyId;
 UPROPERTY() float ExpiresAt = 0;
};

// The owning controller sends intent; only the game server changes membership.
UCLASS()
class ONLINETOWER_API UTowerPartyComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UTowerPartyComponent();
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
 UFUNCTION(Server, Reliable) void Authenticate(const FString& Token);
 UFUNCTION(Server, Reliable) void Invite(int32 TargetPlayerId);
 UFUNCTION(Server, Reliable) void InviteByName(const FString& Username);
 UFUNCTION(Server, Reliable) void CancelMatchSearch();
 UFUNCTION(Server, Reliable) void AcceptMatchSearch();
 UFUNCTION(Server, Reliable) void InviteFriend(const FString& TargetAccountId);
 UFUNCTION(Server, Reliable) void RedeemSocialTicket(const FString& Ticket);
 UFUNCTION(Client, Reliable) void ClientSocialJoinResult(bool bSuccess,const FString& Message);
 UFUNCTION(Server, Reliable) void RespondInvite(int32 InviterPlayerId, bool bAccept);
 UFUNCTION(Server, Reliable) void LeaveParty();
 UFUNCTION(Client, Reliable) void ClientNotice(const FString& Message);
 const FString& GetStatus() const { return Status; }
 const TArray<FTowerPartyInvite>& GetInvites() const { return Invites; }
 TArray<ATowerPlayerState*> GetPartyMembers() const;
 bool IsAuthenticated() const;
 const FString& GetSocialServerId() const { return SocialServerId; }
private:
 ATowerPlayerState* State() const;
 ATowerPlayerState* FindPlayer(int32 Id) const;
 void RemoveFromParty();
 bool AllowCommand();
 UPROPERTY(Replicated) TArray<FTowerPartyInvite> Invites;
 UPROPERTY(Replicated) FString SocialServerId;
 bool LoadSocialCredentials();
 void PublishPresence();
 TSharedRef<class FJsonObject> PresenceBody() const;
 void ServerSocialRequest(const FString& Route,const TSharedRef<class FJsonObject>& Body,
     TFunction<void(bool,const TSharedPtr<class FJsonObject>&,const FString&)> Callback);
 bool CanInviteSocial() const;
 FString SocialServerKey, LastPresenceSignature;
 double NextPresenceAt=0;
 bool bPresenceInFlight=false,bSocialInviteInFlight=false,bRedeemInFlight=false;
 FString Status;
 FString LastLocalToken;
 FString ServerToken;
 double NextCommandAt = 0;
 double NextAuthAt = 0;
 double NextValidationAt = 0;
 double NextLocalAuthAt = 0;
 double CleanupNotBefore = 0;
 TWeakObjectPtr<UWorld> MembershipWorld;
 int32 AuthGeneration = 0;
};
