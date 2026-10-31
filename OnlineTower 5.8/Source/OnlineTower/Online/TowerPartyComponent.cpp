#include "TowerPartyComponent.h"
#include "TowerAccountSubsystem.h"
#include "TowerAbilities.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Net/UnrealNetwork.h"

UTowerPartyComponent::UTowerPartyComponent()
{
 SetIsReplicatedByDefault(true);
 PrimaryComponentTick.bCanEverTick = true;
 PrimaryComponentTick.TickInterval = .25f;
}
ATowerPlayerState* UTowerPartyComponent::State() const
{
 const auto* PC = Cast<APlayerController>(GetOwner());
 return PC ? PC->GetPlayerState<ATowerPlayerState>() : nullptr;
}
ATowerPlayerState* UTowerPartyComponent::FindPlayer(int32 Id) const
{
 if (const auto* GS = GetWorld()->GetGameState())
  for (APlayerState* Player : GS->PlayerArray)
   if (auto* PS = Cast<ATowerPlayerState>(Player); PS && PS->GetPlayerId() == Id && !PS->IsInactive()) return PS;
 return nullptr;
}
bool UTowerPartyComponent::IsAuthenticated() const { const auto* PS = State(); return PS && !PS->AccountId.IsEmpty(); }
TArray<ATowerPlayerState*> UTowerPartyComponent::GetPartyMembers() const
{
 TArray<ATowerPlayerState*> Result;
 const auto* Own = State();
 if (!Own || !Own->PartyId.IsValid()) return Result;
 if (const auto* GS = GetWorld()->GetGameState())
  for (APlayerState* Player : GS->PlayerArray)
   if (auto* PS = Cast<ATowerPlayerState>(Player); PS && !PS->IsInactive() && PS->PartyId == Own->PartyId) Result.Add(PS);
 return Result;
}
void UTowerPartyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME_CONDITION(UTowerPartyComponent, Invites, COND_OwnerOnly);
}
void UTowerPartyComponent::ClientNotice_Implementation(const FString& Message) { Status = Message; }
bool UTowerPartyComponent::AllowCommand()
{
 const double Now = FPlatformTime::Seconds();
 if (Now < NextCommandAt) return false;
 NextCommandAt = Now + .35;
 return true;
}
void UTowerPartyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
 const auto* PC = Cast<APlayerController>(GetOwner());
 if (PC && PC->IsLocalController() && State())
  if (auto* Account = GetWorld()->GetGameInstance()->GetSubsystem<UTowerAccountSubsystem>())
   if (Account->GetToken() != LastLocalToken ||
       (!Account->GetToken().IsEmpty() && State()->AccountId != Account->GetUserId() && FPlatformTime::Seconds() >= NextLocalAuthAt))
   {
    LastLocalToken = Account->GetToken();
    NextLocalAuthAt = FPlatformTime::Seconds() + 5;
    Authenticate(LastLocalToken);
   }
 if (!GetOwner()->HasAuthority()) return;
 if (MembershipWorld.Get() != GetWorld())
 {
  MembershipWorld = GetWorld();
  CleanupNotBefore = FPlatformTime::Seconds() + 5;
 }
 if (!ServerToken.IsEmpty() && FPlatformTime::Seconds() >= NextValidationAt)
  Authenticate_Implementation(ServerToken);
 const float Now = GetWorld()->GetTimeSeconds();
 Invites.RemoveAll([this, Now](const FTowerPartyInvite& Invite)
 {
  const auto* Sender = FindPlayer(Invite.InviterPlayerId);
  return Invite.ExpiresAt < Now || !Sender || Sender->AccountId != Invite.InviterAccountId ||
   (Invite.InviterPartyId.IsValid() && Invite.InviterPartyId != Sender->PartyId);
 });
 // PlayerStates can arrive in different ticks during seamless travel. Do not dissolve that temporary singleton.
 if (GetWorld()->IsInSeamlessTravel()) { CleanupNotBefore = FPlatformTime::Seconds() + 5; return; }
 if (FPlatformTime::Seconds() < CleanupNotBefore) return;
 auto Members = GetPartyMembers();
 if (Members.Num() == 1) { Members[0]->PartyId.Invalidate(); Members[0]->PartyLeaderId = -1; Members[0]->ForceNetUpdate(); }
 else if (Members.Num() > 1 && !Members.Contains(FindPlayer(Members[0]->PartyLeaderId)))
 {
  int32 Leader = Members[0]->GetPlayerId();
  for (auto* Member : Members) Leader = FMath::Min(Leader, Member->GetPlayerId());
  for (auto* Member : Members) { Member->PartyLeaderId = Leader; Member->ForceNetUpdate(); }
 }
}
void UTowerPartyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 ++AuthGeneration;
 // Seamless travel preserves PlayerState membership; actual disconnect is cleaned by remaining members.
 Super::EndPlay(Reason);
}
void UTowerPartyComponent::Authenticate_Implementation(const FString& Token)
{
 const double Now = FPlatformTime::Seconds();
 if (Token.Len() > 256 || (!Token.IsEmpty() && Now < NextAuthAt) || !State()) return;
 NextAuthAt = Now + 1;
 NextValidationAt = Now + 60;
 const int32 Generation = ++AuthGeneration;
 ServerToken = Token;
 if (Token.IsEmpty())
 {
  RemoveFromParty(); Invites.Empty();
  State()->AccountId.Empty(); State()->AccountUsername.Empty(); State()->ForceNetUpdate();
  ClientNotice(TEXT("Вы вышли из аккаунта.")); return;
 }
 ClientNotice(TEXT("Проверка аккаунта сервером..."));
 auto Request = FHttpModule::Get().CreateRequest();
 Request->SetURL(TEXT("http://127.0.0.1:18080/v1/auth/me"));
 Request->SetVerb(TEXT("GET")); Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
 Request->SetTimeout(10);
 Request->OnProcessRequestComplete().BindWeakLambda(this, [this, Generation](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
 {
  auto* PS = State();
  if (Generation != AuthGeneration || !PS || !GetOwner()->HasAuthority()) return;
  TSharedPtr<FJsonObject> Json;
  const TSharedPtr<FJsonObject>* User = nullptr;
  FString Id, Name;
  if (!bSuccess || !Response.IsValid() || Response->GetResponseCode() != 200 ||
      !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json) || !Json.IsValid() ||
      !Json->TryGetObjectField(TEXT("user"), User) || !(*User)->TryGetStringField(TEXT("id"), Id) ||
      !(*User)->TryGetStringField(TEXT("username"), Name) || Id.IsEmpty() || Id.Len() > 128 || Name.Len() > 24)
  {
   RemoveFromParty(); Invites.Empty(); PS->AccountId.Empty(); PS->AccountUsername.Empty(); PS->ForceNetUpdate();
   ClientNotice(TEXT("Аккаунт не подтверждён. Проверьте backend и войдите снова.")); return;
  }
  if (const auto* GS = GetWorld()->GetGameState())
   for (APlayerState* Other : GS->PlayerArray)
    if (auto* OtherPS = Cast<ATowerPlayerState>(Other); OtherPS && OtherPS != PS && OtherPS->AccountId == Id && !OtherPS->IsInactive())
    {
     RemoveFromParty(); PS->AccountId.Empty(); PS->AccountUsername.Empty(); PS->ForceNetUpdate();
     ClientNotice(TEXT("Этот аккаунт уже подключён. Для второго окна нужен другой аккаунт.")); return;
    }
  if (PS->AccountId != Id) RemoveFromParty();
  PS->AccountId = Id; PS->AccountUsername = Name; PS->SetPlayerName(Name); PS->ForceNetUpdate();
  ClientNotice(TEXT("Аккаунт подтверждён. Можно приглашать игроков."));
  UE_LOG(LogTemp, Display, TEXT("TOWER_AUTH_OK Player=%d"), PS->GetPlayerId());
 });
 if (!Request->ProcessRequest())
 {
  RemoveFromParty(); State()->AccountId.Empty(); State()->AccountUsername.Empty(); State()->ForceNetUpdate();
  ClientNotice(TEXT("Не удалось обратиться к backend."));
 }
}
void UTowerPartyComponent::Invite_Implementation(int32 TargetPlayerId)
{
 if (!AllowCommand()) return;
 auto* Own = State(); auto* Target = FindPlayer(TargetPlayerId);
 if (!Own || Own->AccountId.IsEmpty() || !Target || Target == Own || Target->AccountId.IsEmpty())
 { ClientNotice(TEXT("Оба игрока должны войти в разные аккаунты.")); return; }
 if (Own->PartyId.IsValid() && Own->PartyLeaderId != Own->GetPlayerId())
 { ClientNotice(TEXT("Приглашать может только лидер группы.")); return; }
 if (Target->PartyId.IsValid() || GetPartyMembers().Num() >= 3)
 { ClientNotice(TEXT("Игрок уже в группе или группа заполнена (3 игрока).")); return; }
 auto* TargetPC = Cast<APlayerController>(Target->GetOwner());
 auto* TargetParty = TargetPC ? TargetPC->FindComponentByClass<UTowerPartyComponent>() : nullptr;
 if (!TargetParty) return;
 TargetParty->Invites.RemoveAll([Own](const FTowerPartyInvite& Invite) { return Invite.InviterPlayerId == Own->GetPlayerId(); });
 if (TargetParty->Invites.Num() >= 8) { ClientNotice(TEXT("У игрока слишком много приглашений.")); return; }
 FTowerPartyInvite Invitation; Invitation.InviterPlayerId = Own->GetPlayerId(); Invitation.InviterName = Own->AccountUsername;
 Invitation.InviterAccountId = Own->AccountId; Invitation.InviterPartyId = Own->PartyId;
 Invitation.ExpiresAt = GetWorld()->GetTimeSeconds() + 60;
 TargetParty->Invites.Add(Invitation);
 TargetParty->ClientNotice(TEXT("Получено приглашение в группу."));
 ClientNotice(TEXT("Приглашение отправлено (60 секунд)."));
}
void UTowerPartyComponent::RespondInvite_Implementation(int32 InviterPlayerId, bool bAccept)
{
 if (!AllowCommand()) return;
 const int32 Index = Invites.IndexOfByPredicate([InviterPlayerId](const FTowerPartyInvite& Invite) { return Invite.InviterPlayerId == InviterPlayerId; });
 if (Index == INDEX_NONE) return;
 const bool bExpired = Invites[Index].ExpiresAt < GetWorld()->GetTimeSeconds();
 const FString ExpectedAccount = Invites[Index].InviterAccountId;
 const FGuid ExpectedParty = Invites[Index].InviterPartyId;
 Invites.RemoveAt(Index);
 if (!bAccept) { ClientNotice(TEXT("Приглашение отклонено.")); return; }
 auto* Own = State(); auto* Leader = FindPlayer(InviterPlayerId);
 if (bExpired || !Own || Own->AccountId.IsEmpty() || Own->PartyId.IsValid() || !Leader || Leader->AccountId != ExpectedAccount ||
     (ExpectedParty.IsValid() && Leader->PartyId != ExpectedParty) ||
     (Leader->PartyId.IsValid() && Leader->PartyLeaderId != Leader->GetPlayerId()))
 { ClientNotice(TEXT("Приглашение больше недействительно.")); return; }
 auto* LeaderPC = Cast<APlayerController>(Leader->GetOwner());
 auto* LeaderParty = LeaderPC ? LeaderPC->FindComponentByClass<UTowerPartyComponent>() : nullptr;
 if (!LeaderParty || LeaderParty->GetPartyMembers().Num() >= 3) { ClientNotice(TEXT("Группа заполнена.")); return; }
 if (!Leader->PartyId.IsValid()) Leader->PartyId = FGuid::NewGuid();
 Leader->PartyLeaderId = Leader->GetPlayerId();
 Own->PartyId = Leader->PartyId; Own->PartyLeaderId = Leader->GetPlayerId();
 Invites.Empty(); Own->ForceNetUpdate(); Leader->ForceNetUpdate();
 ClientNotice(TEXT("Вы в группе. Для перехода все участники должны войти в один портал."));
 LeaderParty->ClientNotice(TEXT("Игрок присоединился к группе."));
 UE_LOG(LogTemp, Display, TEXT("TOWER_PARTY_JOIN Leader=%d Member=%d"), Leader->GetPlayerId(), Own->GetPlayerId());
}
void UTowerPartyComponent::RemoveFromParty()
{
 auto Members = GetPartyMembers(); auto* Own = State();
 if (!Own) return;
 Own->PartyId.Invalidate(); Own->PartyLeaderId = -1; Own->ForceNetUpdate();
 Members.Remove(Own);
 if (Members.Num() == 1) { Members[0]->PartyId.Invalidate(); Members[0]->PartyLeaderId = -1; Members[0]->ForceNetUpdate(); }
 else if (Members.Num() > 1)
 {
  int32 Leader = Members[0]->GetPlayerId();
  for (auto* Member : Members) Leader = FMath::Min(Leader, Member->GetPlayerId());
  for (auto* Member : Members) { Member->PartyLeaderId = Leader; Member->ForceNetUpdate(); }
 }
}
void UTowerPartyComponent::LeaveParty_Implementation()
{
 if (!AllowCommand()) return;
 RemoveFromParty(); ClientNotice(TEXT("Вы вышли из группы."));
}
