#include "TowerPartyComponent.h"
#include "TowerAccountSubsystem.h"
#include "TowerSocialSubsystem.h"
#include "TowerOnlineHttp.h"
#include "TowerCharacter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TowerAbilities.h"
#include "Trials/TowerMatchmaking.h"
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
 DOREPLIFETIME_CONDITION(UTowerPartyComponent, SocialServerId, COND_OwnerOnly);
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
 if (IsAuthenticated() && !ServerToken.IsEmpty())
 {
  auto* PS=State();
  const FString Signature=FString::Printf(TEXT("%s:%s:%d:%d:%d"),*PS->AccountId,*PS->PartyId.ToString(),PS->PartyLeaderId,GetPartyMembers().Num(),PS->QueuedTrial)+PS->MatchId.ToString();
  if (Signature!=LastPresenceSignature || FPlatformTime::Seconds()>=NextPresenceAt)
  { LastPresenceSignature=Signature; PublishPresence(); }
 }
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
 Request->SetURL(TowerOnlineHttp::BaseUrl()+TEXT("/v1/auth/me"));
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
  PublishPresence();
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
 if (Own->MatchId.IsValid() || Target->MatchId.IsValid() || Own->QueuedTrial>=0 || Target->QueuedTrial>=0)
 { ClientNotice(TEXT("Сначала завершите испытание или отмените поиск матча.")); return; }
 if (Target->PartyId.IsValid() || GetPartyMembers().Num() >= 6)
 { ClientNotice(TEXT("Игрок уже в группе или группа заполнена (6 игроков).")); return; }
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
 if (!LeaderParty || LeaderParty->GetPartyMembers().Num() >= 6) { ClientNotice(TEXT("Группа заполнена (6 игроков).")); return; }
 if (Own->MatchId.IsValid() || Leader->MatchId.IsValid() || Own->QueuedTrial>=0 || Leader->QueuedTrial>=0)
 { ClientNotice(TEXT("Сначала завершите испытание или отмените поиск матча.")); return; }
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
 if (auto* Matchmaking=GetWorld()->GetSubsystem<UTowerMatchmakingSubsystem>()) Matchmaking->CancelQueue(Cast<APlayerController>(GetOwner()));
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

void UTowerPartyComponent::InviteByName_Implementation(const FString& Username)
{
 const FString Name=Username.TrimStartAndEnd();
 if (Name.Len()<3 || Name.Len()>24) { if (AllowCommand()) ClientNotice(TEXT("Введите ник от 3 до 24 символов.")); return; }
 if (const auto* GS=GetWorld()->GetGameState())
  for (APlayerState* Base:GS->PlayerArray)
   if (auto* PS=Cast<ATowerPlayerState>(Base); PS && !PS->IsInactive() && !PS->AccountId.IsEmpty() && PS->AccountUsername.Equals(Name,ESearchCase::IgnoreCase))
   { Invite_Implementation(PS->GetPlayerId()); return; }
 if (AllowCommand()) ClientNotice(TEXT("Игрок не найден. Он должен войти в аккаунт на этом сервере хаба."));
}

void UTowerPartyComponent::CancelMatchSearch_Implementation()
{
 if (!AllowCommand()) return;
 if (auto* Matchmaking=GetWorld()->GetSubsystem<UTowerMatchmakingSubsystem>()) Matchmaking->CancelQueue(Cast<APlayerController>(GetOwner()));
}

void UTowerPartyComponent::AcceptMatchSearch_Implementation()
{
 if (!AllowCommand()) return;
 if (auto* Matchmaking=GetWorld()->GetSubsystem<UTowerMatchmakingSubsystem>()) Matchmaking->AcceptMatch(Cast<APlayerController>(GetOwner()));
}

bool UTowerPartyComponent::LoadSocialCredentials()
{
 if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode()==NM_Client) return false;
 if (!SocialServerId.IsEmpty() && !SocialServerKey.IsEmpty()) return true;
 FString File=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../Backend/data/servers.json"));
 FParse::Value(FCommandLine::Get(),TEXT("TowerServersFile="),File);
 FString Text; TSharedPtr<FJsonObject> Json; const TArray<TSharedPtr<FJsonValue>>* Servers=nullptr;
 if (!FFileHelper::LoadFileToString(Text,*File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json) || !Json.IsValid() || !Json->TryGetArrayField(TEXT("servers"),Servers)) return false;
 FString Wanted; FParse::Value(FCommandLine::Get(),TEXT("TowerServerId="),Wanted);
 if (Wanted.IsEmpty() && GetWorld()->WorldType==EWorldType::PIE) Wanted=TEXT("hub_a");
 int32 Port=GetWorld()->URL.Port; FParse::Value(FCommandLine::Get(),TEXT("port="),Port);
 for (const auto& Value:*Servers)
 {
  const TSharedPtr<FJsonObject>* Row=nullptr; if (!Value->TryGetObject(Row)) continue;
  FString Id,Address,Key;
  if (!(*Row)->TryGetStringField(TEXT("id"),Id) || !(*Row)->TryGetStringField(TEXT("address"),Address) || !(*Row)->TryGetStringField(TEXT("key"),Key) || Key.Len()<16) continue;
  const bool Matches=Wanted.IsEmpty() ? Address.EndsWith(FString::Printf(TEXT(":%d"),Port)) : Id==Wanted;
  if (Matches) { SocialServerId=Id; SocialServerKey=Key; GetOwner()->ForceNetUpdate(); return true; }
 }
 return false;
}
TSharedRef<FJsonObject> UTowerPartyComponent::PresenceBody() const
{
 auto Body=MakeShared<FJsonObject>(); const auto* PS=State();
 Body->SetStringField(TEXT("user_token"),ServerToken);
 if (!PS) return Body;
 const auto* Hero=Cast<ATowerCharacter>(PS->GetPawn());
 const FString StateName=PS->MatchId.IsValid() || (Hero && Hero->TrialIndex>=0) ? TEXT("match") : PS->QueuedTrial>=0 ? TEXT("queued") : TEXT("hub");
 const auto* Leader=PS->PartyId.IsValid() ? FindPlayer(PS->PartyLeaderId) : PS;
 Body->SetStringField(TEXT("status"),StateName);
 Body->SetStringField(TEXT("party_id"),PS->PartyId.IsValid()?PS->PartyId.ToString(EGuidFormats::DigitsWithHyphens):FString());
 Body->SetStringField(TEXT("leader_id"),Leader?Leader->AccountId:FString());
 Body->SetNumberField(TEXT("party_size"),FMath::Max(1,GetPartyMembers().Num()));
 return Body;
}
void UTowerPartyComponent::ServerSocialRequest(const FString& Route,const TSharedRef<FJsonObject>& Body,TFunction<void(bool,const TSharedPtr<FJsonObject>&,const FString&)> Callback)
{
 if (!LoadSocialCredentials()) {Callback(false,nullptr,TEXT("Социальный сервис не настроен на игровом сервере. Запустите backend и проверьте servers.json."));return;}
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(TowerOnlineHttp::BaseUrl()+TEXT("/v1/server/")+Route);Request->SetVerb(TEXT("POST"));
 Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
 Request->SetHeader(TEXT("X-Tower-Server-Id"),SocialServerId);Request->SetHeader(TEXT("X-Tower-Server-Key"),SocialServerKey);Request->SetTimeout(8);
 FString Payload;FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload));Request->SetContentAsString(Payload);
 // Callback is executed only while this authority component still exists.
 Request->OnProcessRequestComplete().BindWeakLambda(this,[Callback](FHttpRequestPtr,FHttpResponsePtr Response,bool bSuccess)
 {
  TSharedPtr<FJsonObject> Json;FString Error;const bool bOK=TowerOnlineHttp::Decode(Response,bSuccess,Json,Error);Callback(bOK,Json,Error);
 });
 if(!Request->ProcessRequest()) Callback(false,nullptr,TEXT("Не удалось отправить запрос социальному сервису."));
}
void UTowerPartyComponent::PublishPresence()
{
 if (!IsAuthenticated() || ServerToken.IsEmpty() || bPresenceInFlight) return;
 NextPresenceAt=FPlatformTime::Seconds()+5;bPresenceInFlight=true;
 ServerSocialRequest(TEXT("presence"),PresenceBody(),[this](bool,const TSharedPtr<FJsonObject>&,const FString&){bPresenceInFlight=false;});
}
bool UTowerPartyComponent::CanInviteSocial() const
{
 const auto* Own=State();const auto* Hero=Own?Cast<ATowerCharacter>(Own->GetPawn()):nullptr;
 return Own && !Own->AccountId.IsEmpty() && Hero && Hero->TrialIndex==-1 && !Own->MatchId.IsValid() && Own->QueuedTrial<0 &&
   (!Own->PartyId.IsValid() || Own->PartyLeaderId==Own->GetPlayerId()) && GetPartyMembers().Num()<6;
}
void UTowerPartyComponent::InviteFriend_Implementation(const FString& TargetAccountId)
{
 if(!AllowCommand() || bSocialInviteInFlight) return;
 if(TargetAccountId.IsEmpty() || TargetAccountId.Len()>128 || !CanInviteSocial())
 {ClientNotice(TEXT("Пригласить друга может лидер свободной группы в хабе (максимум 6). Сначала отмените поиск или завершите испытание."));return;}
 const FString ExpectedAccount=State()->AccountId,ExpectedToken=ServerToken;
 bSocialInviteInFlight=true;
 // Publish a fresh server snapshot before backend validates the invitation.
 ServerSocialRequest(TEXT("presence"),PresenceBody(),[this,ExpectedAccount,ExpectedToken,TargetAccountId](bool bOK,const TSharedPtr<FJsonObject>&,const FString& Error)
 {
  if(!State() || State()->AccountId!=ExpectedAccount || ServerToken!=ExpectedToken) {bSocialInviteInFlight=false;return;}
  if(!bOK || !CanInviteSocial()) {bSocialInviteInFlight=false;ClientNotice(bOK?TEXT("Состояние группы изменилось. Повторите приглашение."):Error);return;}
  auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("user_token"),ServerToken);Body->SetStringField(TEXT("target_id"),TargetAccountId);
  ServerSocialRequest(TEXT("party-invite"),Body,[this,ExpectedAccount,ExpectedToken](bool bSent,const TSharedPtr<FJsonObject>&,const FString& InviteError)
  {
   bSocialInviteInFlight=false;
   if(!State() || State()->AccountId!=ExpectedAccount || ServerToken!=ExpectedToken) return;
   ClientNotice(bSent?TEXT("Приглашение другу отправлено. Оно появится и в другом хабе."):InviteError);
  });
 });
}
void UTowerPartyComponent::RedeemSocialTicket_Implementation(const FString& Ticket)
{
 if(bRedeemInFlight) return;
 if(!AllowCommand()) {ClientSocialJoinResult(false,TEXT("Слишком частые действия. Попросите повторное приглашение."));return;}
 auto* Own=State();auto* Hero=Own?Cast<ATowerCharacter>(Own->GetPawn()):nullptr;
 if(!Own || Own->AccountId.IsEmpty() || Ticket.IsEmpty() || Ticket.Len()>256 || !Hero || Hero->TrialIndex!=-1 || Own->PartyId.IsValid() || Own->MatchId.IsValid() || Own->QueuedTrial>=0)
 {ClientSocialJoinResult(false,TEXT("Не удалось вступить: нужен авторизованный игрок без группы и матча в хабе."));return;}
 const FString ExpectedAccount=Own->AccountId,ExpectedToken=ServerToken;
 auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("user_token"),ServerToken);Body->SetStringField(TEXT("join_ticket"),Ticket);
 bRedeemInFlight=true;
 ServerSocialRequest(TEXT("consume-ticket"),Body,[this,ExpectedAccount,ExpectedToken](bool bOK,const TSharedPtr<FJsonObject>& Json,const FString& Error)
 {
  bRedeemInFlight=false;auto* Self=State();
  if(!Self || Self->AccountId!=ExpectedAccount || ServerToken!=ExpectedToken) {ClientSocialJoinResult(false,TEXT("Аккаунт изменился во время подключения."));return;}
  if(!bOK) {ClientSocialJoinResult(false,Error);return;}
  FString InviterId,PartyId;
  if(!Json.IsValid() || !Json->TryGetStringField(TEXT("inviter_id"),InviterId) || !Json->TryGetStringField(TEXT("party_id"),PartyId))
  {ClientSocialJoinResult(false,TEXT("Сервис вернул неверное подтверждение приглашения."));return;}
  ATowerPlayerState* Leader=nullptr;
  if(const auto* GS=GetWorld()->GetGameState())for(APlayerState* Base:GS->PlayerArray)
   if(auto* P=Cast<ATowerPlayerState>(Base);P && !P->IsInactive() && P->AccountId==InviterId) {Leader=P;break;}
  auto* LeaderPC=Leader?Cast<APlayerController>(Leader->GetOwner()):nullptr;
  auto* LeaderParty=LeaderPC?LeaderPC->FindComponentByClass<UTowerPartyComponent>():nullptr;
  const auto* SelfHero=Cast<ATowerCharacter>(Self->GetPawn());
  const FString ActualParty=Leader && Leader->PartyId.IsValid()?Leader->PartyId.ToString(EGuidFormats::DigitsWithHyphens):FString();
  if(!Leader || Leader==Self || !LeaderParty || !LeaderParty->CanInviteSocial() || ActualParty!=PartyId || !SelfHero || SelfHero->TrialIndex!=-1 || Self->PartyId.IsValid() || Self->MatchId.IsValid() || Self->QueuedTrial>=0)
  {ClientSocialJoinResult(false,TEXT("Группа друга изменилась, заполнилась или он покинул хаб. Вы остались в хабе; запросите новое приглашение."));return;}
  if(!Leader->PartyId.IsValid())Leader->PartyId=FGuid::NewGuid();
  Leader->PartyLeaderId=Leader->GetPlayerId();Self->PartyId=Leader->PartyId;Self->PartyLeaderId=Leader->GetPlayerId();
  Invites.Empty();Self->ForceNetUpdate();Leader->ForceNetUpdate();
  PublishPresence();LeaderParty->PublishPresence();
  ClientSocialJoinResult(true,TEXT("Вы присоединились к группе друга."));LeaderParty->ClientNotice(TEXT("Друг присоединился к группе."));
  UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_PARTY_JOIN Members=%d"),LeaderParty->GetPartyMembers().Num());
 });
}
void UTowerPartyComponent::ClientSocialJoinResult_Implementation(bool bSuccess,const FString& Message)
{
 Status=Message;
 if(auto* Social=GetWorld()->GetGameInstance()->GetSubsystem<UTowerSocialSubsystem>()) Social->JoinFinished(bSuccess,Message);
}
