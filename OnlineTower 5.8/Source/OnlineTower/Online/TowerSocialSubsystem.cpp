#include "TowerSocialSubsystem.h"
#include "TowerAccountSubsystem.h"
#include "TowerPartyComponent.h"
#include "TowerOnlineHttp.h"
#include "TowerAbilities.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace
{
 void ReadUsers(const TSharedPtr<FJsonObject>& Json,const TCHAR* Key,TArray<FTowerSocialUser>& Out)
 {
  Out.Reset(); const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
  if(!Json->TryGetArrayField(Key,Values)) return;
  for(const auto& Value:*Values)
  {
   const TSharedPtr<FJsonObject>* Row=nullptr; if(!Value->TryGetObject(Row)) continue;
   FTowerSocialUser User;
   if(!(*Row)->TryGetStringField(TEXT("id"),User.Id) || !(*Row)->TryGetStringField(TEXT("username"),User.Username)) continue;
   (*Row)->TryGetBoolField(TEXT("online"),User.bOnline); (*Row)->TryGetStringField(TEXT("status"),User.Status); (*Row)->TryGetStringField(TEXT("server_id"),User.ServerId);
   Out.Add(MoveTemp(User));
  }
 }
 void ReadRequests(const TSharedPtr<FJsonObject>& Json,const TCHAR* Key,TArray<FTowerSocialRequest>& Out)
 {
  Out.Reset(); const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
  if(!Json->TryGetArrayField(Key,Values)) return;
  for(const auto& Value:*Values)
  {
   const TSharedPtr<FJsonObject>* Row=nullptr; if(!Value->TryGetObject(Row)) continue;
   FTowerSocialRequest Entry;
   if(!(*Row)->TryGetStringField(TEXT("id"),Entry.Id) || !(*Row)->TryGetStringField(TEXT("user_id"),Entry.UserId) || !(*Row)->TryGetStringField(TEXT("username"),Entry.Username)) continue;
   Out.Add(MoveTemp(Entry));
  }
 }
 TSharedRef<IHttpRequest,ESPMode::ThreadSafe> Request(const FString& Route,const FString& Token,const FString& Verb=TEXT("GET"))
 {
  auto R=FHttpModule::Get().CreateRequest(); R->SetURL(TowerOnlineHttp::BaseUrl()+Route); R->SetVerb(Verb);
  R->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token); R->SetHeader(TEXT("Content-Type"),TEXT("application/json")); R->SetTimeout(8);
  return R;
 }
 void SetBody(const TSharedRef<IHttpRequest,ESPMode::ThreadSafe>& R,const TSharedRef<FJsonObject>& Body)
 {
  FString Payload; FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload)); R->SetContentAsString(Payload);
 }
}

void UTowerSocialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection); Collection.InitializeDependency<UTowerAccountSubsystem>();
 TickerHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UTowerSocialSubsystem::Tick),.25f);
}
void UTowerSocialSubsystem::Deinitialize()
{
 FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle); ++Generation; Super::Deinitialize();
}
FString UTowerSocialSubsystem::CurrentToken() const
{
 const auto* Account=GetGameInstance()->GetSubsystem<UTowerAccountSubsystem>(); return Account ? Account->GetToken() : FString();
}
void UTowerSocialSubsystem::ResetForAccount(const FString& Token)
{
 ++Generation; ++SearchGeneration; LastToken=Token;
 SearchResults.Reset(); Friends.Reset(); IncomingRequests.Reset(); OutgoingRequests.Reset(); PartyInvites.Reset();
 bActionInFlight=bSearchInFlight=bRefreshInFlight=bHeartbeatInFlight=false;
 PendingJoinTicket.Empty(); PendingServerId.Empty(); bRedeemSent=false; JoinDeadline=0;
 Status.Empty(); NextRefresh=NextHeartbeat=0;
}
bool UTowerSocialSubsystem::Tick(float)
{
 const FString Token=CurrentToken(); if(Token!=LastToken) ResetForAccount(Token);
 if(Token.IsEmpty() || !GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer) return true;
 const double Now=FPlatformTime::Seconds();
 if(Now>=NextHeartbeat) { NextHeartbeat=Now+5; Heartbeat(); }
 if(Now>=NextRefresh) { NextRefresh=Now+3; Refresh(); }
 if(!PendingJoinTicket.IsEmpty())
 {
  if(Now>=JoinDeadline) JoinFinished(false,TEXT("Время подключения истекло. Попросите друга отправить приглашение ещё раз."));
  else if(!bRedeemSent)
  {
   auto* PC=GetGameInstance()->GetFirstLocalPlayerController(); auto* Party=PC?PC->FindComponentByClass<UTowerPartyComponent>():nullptr;
   if(Party && Party->IsAuthenticated() && Party->GetSocialServerId()==PendingServerId)
   { bRedeemSent=true; Party->RedeemSocialTicket(PendingJoinTicket); }
  }
 }
 return true;
}
void UTowerSocialSubsystem::Heartbeat()
{
 if(bHeartbeatInFlight || LastToken.IsEmpty()) return;
 bHeartbeatInFlight=true; const uint32 Expected=Generation;
 auto R=Request(TEXT("/v1/social/presence"),LastToken,TEXT("POST")); R->SetContentAsString(TEXT("{}"));
 R->OnProcessRequestComplete().BindWeakLambda(this,[this,Expected](FHttpRequestPtr,FHttpResponsePtr,bool)
 { if(Expected==Generation) bHeartbeatInFlight=false; });
 if(!R->ProcessRequest()) bHeartbeatInFlight=false;
}
void UTowerSocialSubsystem::Refresh()
{
 const FString Token=CurrentToken(); if(Token!=LastToken) ResetForAccount(Token);
 if(Token.IsEmpty() || bRefreshInFlight) return;
 bRefreshInFlight=true; const uint32 Expected=Generation;
 auto R=Request(TEXT("/v1/social/state"),Token);
 R->OnProcessRequestComplete().BindWeakLambda(this,[this,Expected](FHttpRequestPtr,FHttpResponsePtr Response,bool bOK)
 {
  if(Expected!=Generation || CurrentToken()!=LastToken) return;
  bRefreshInFlight=false; TSharedPtr<FJsonObject> Json; FString Error;
  if(!TowerOnlineHttp::Decode(Response,bOK,Json,Error)) { Status=Error; return; }
  ReadUsers(Json,TEXT("friends"),Friends); ReadRequests(Json,TEXT("incoming_requests"),IncomingRequests); ReadRequests(Json,TEXT("outgoing_requests"),OutgoingRequests);
  PartyInvites.Reset(); const TArray<TSharedPtr<FJsonValue>>* Invites=nullptr;
  if(Json->TryGetArrayField(TEXT("party_invites"),Invites)) for(const auto& Value:*Invites)
  {
   const TSharedPtr<FJsonObject>* Row=nullptr; if(!Value->TryGetObject(Row)) continue;
   FTowerSocialPartyInvite Entry;
   if(!(*Row)->TryGetStringField(TEXT("id"),Entry.Id) || !(*Row)->TryGetStringField(TEXT("user_id"),Entry.UserId) || !(*Row)->TryGetStringField(TEXT("username"),Entry.Username)) continue;
   (*Row)->TryGetStringField(TEXT("server_id"),Entry.ServerId); (*Row)->TryGetNumberField(TEXT("expires_at"),Entry.ExpiresAt); PartyInvites.Add(MoveTemp(Entry));
  }
 });
 if(!R->ProcessRequest()) {bRefreshInFlight=false;Status=TEXT("Не удалось обновить список друзей.");}
}
void UTowerSocialSubsystem::SearchUsers(const FString& Query)
{
 const FString Token=CurrentToken(); if(Token!=LastToken) ResetForAccount(Token);
 if(Token.IsEmpty()) {Status=TEXT("Сначала войдите в аккаунт.");return;}
 const FString Name=Query.TrimStartAndEnd();
 if(Name.Len()<2 || Name.Len()>24) { Status=TEXT("Введите минимум 2 символа ника (максимум 24)."); SearchResults.Reset(); return; }
 const uint32 Expected=Generation, Search=++SearchGeneration; bSearchInFlight=true;
 auto R=Request(TEXT("/v1/social/search?q=")+FGenericPlatformHttp::UrlEncode(Name),Token);
 R->OnProcessRequestComplete().BindWeakLambda(this,[this,Expected,Search](FHttpRequestPtr,FHttpResponsePtr Response,bool bOK)
 {
  if(Expected!=Generation || Search!=SearchGeneration || CurrentToken()!=LastToken) return;
  bSearchInFlight=false; TSharedPtr<FJsonObject> Json; FString Error;
  if(!TowerOnlineHttp::Decode(Response,bOK,Json,Error)) {Status=Error; SearchResults.Reset();return;}
  ReadUsers(Json,TEXT("users"),SearchResults); Status=SearchResults.IsEmpty()?TEXT("Игроки с таким ником не найдены."):TEXT("Найдены аккаунты, включая игроков вне вашего хаба.");
 });
 if(!R->ProcessRequest()) {bSearchInFlight=false;Status=TEXT("Не удалось начать поиск.");}
}
void UTowerSocialSubsystem::Post(const FString& Route,const TSharedRef<FJsonObject>& Body,const FString& SuccessText,TFunction<void(const TSharedPtr<FJsonObject>&)> OnSuccess)
{
 const FString Token=CurrentToken(); if(Token!=LastToken) ResetForAccount(Token);
 if(Token.IsEmpty()) {Status=TEXT("Сначала войдите в аккаунт.");return;}
 if(bActionInFlight) return;
 bActionInFlight=true; const uint32 Expected=Generation;
 auto R=Request(TEXT("/v1/social/")+Route,Token,TEXT("POST")); SetBody(R,Body);
 R->OnProcessRequestComplete().BindWeakLambda(this,[this,Expected,SuccessText,OnSuccess=MoveTemp(OnSuccess)](FHttpRequestPtr,FHttpResponsePtr Response,bool bOK)
 {
  if(Expected!=Generation || CurrentToken()!=LastToken) return;
  bActionInFlight=false; TSharedPtr<FJsonObject> Json; FString Error;
  if(!TowerOnlineHttp::Decode(Response,bOK,Json,Error)) {Status=Error;return;}
  Status=SuccessText; if(OnSuccess) OnSuccess(Json); NextRefresh=0;
 });
 if(!R->ProcessRequest()) {bActionInFlight=false;Status=TEXT("Не удалось отправить запрос.");}
}
void UTowerSocialSubsystem::RequestFriend(const FString& Id)
{ auto B=MakeShared<FJsonObject>(); B->SetStringField(TEXT("target_id"),Id); Post(TEXT("friend-request"),B,TEXT("Заявка в друзья отправлена.")); }
void UTowerSocialSubsystem::RespondFriend(const FString& Id,bool bAccept)
{ auto B=MakeShared<FJsonObject>(); B->SetStringField(TEXT("request_id"),Id);B->SetBoolField(TEXT("accept"),bAccept);Post(TEXT("friend-respond"),B,bAccept?TEXT("Игрок добавлен в друзья."):TEXT("Заявка отклонена.")); }
void UTowerSocialSubsystem::CancelFriendRequest(const FString& Id)
{ auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("request_id"),Id);Post(TEXT("friend-cancel"),B,TEXT("Заявка отменена.")); }
void UTowerSocialSubsystem::RemoveFriend(const FString& Id)
{ auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("target_id"),Id);Post(TEXT("friend-remove"),B,TEXT("Игрок удалён из друзей.")); }
bool UTowerSocialSubsystem::CanAcceptParty(FString& Error) const
{
 auto* PC=GetGameInstance()->GetFirstLocalPlayerController();
 if(!PC) {Error=TEXT("Игровое окно ещё не готово.");return false;}
 if(const auto* PS=PC->GetPlayerState<ATowerPlayerState>(); PS && (PS->MatchId.IsValid() || PS->QueuedTrial>=0 || PS->PartyId.IsValid()))
 {Error=TEXT("Сначала выйдите из текущей группы, отмените поиск и вернитесь в хаб.");return false;}
 if(!PendingJoinTicket.IsEmpty()) {Error=TEXT("Подключение по приглашению уже выполняется.");return false;}
 return true;
}
void UTowerSocialSubsystem::RespondPartyInvite(const FString& Id,bool bAccept)
{
 FString Error; if(bAccept && !CanAcceptParty(Error)) {Status=Error;return;}
 const auto* Entry=PartyInvites.FindByPredicate([&](const FTowerSocialPartyInvite& I){return I.Id==Id;});
 auto* PC=GetGameInstance()->GetFirstLocalPlayerController(); auto* Party=PC?PC->FindComponentByClass<UTowerPartyComponent>():nullptr;
 if(bAccept && GetWorld() && GetWorld()->GetNetMode()==NM_ListenServer && (!Entry || !Party || Entry->ServerId!=Party->GetSocialServerId()))
 {Status=TEXT("Хост PIE не может покинуть сервер с другими игроками. Для перехода между хабами используйте отдельный клиент.");return;}
 auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("invite_id"),Id);B->SetBoolField(TEXT("accept"),bAccept);
 Post(TEXT("party-respond"),B,bAccept?TEXT("Приглашение принято. Подключение к другу…"):TEXT("Приглашение отклонено."),[this,bAccept](const TSharedPtr<FJsonObject>& Json)
 {
  if(!bAccept) return;
  FString Ticket,Address,ServerId,Error;
  if(!Json->TryGetStringField(TEXT("join_ticket"),Ticket) || !Json->TryGetStringField(TEXT("address"),Address) || !Json->TryGetStringField(TEXT("server_id"),ServerId) ||
     Ticket.IsEmpty() || Ticket.Len()>256 || Address.IsEmpty() || Address.Len()>256 || Address.Contains(TEXT("?")) || Address.Contains(TEXT("/")) || Address.Contains(TEXT("\\")) || Address.Contains(TEXT(" ")) || ServerId.IsEmpty())
  {Status=TEXT("Сервис вернул неверное приглашение.");return;}
  if(!CanAcceptParty(Error)) {Status=Error;return;}
  auto* LocalPC=GetGameInstance()->GetFirstLocalPlayerController();auto* LocalParty=LocalPC?LocalPC->FindComponentByClass<UTowerPartyComponent>():nullptr;
  const bool SameHub=LocalParty && LocalParty->IsAuthenticated() && LocalParty->GetSocialServerId()==ServerId;
  if(!SameHub && GetWorld()->GetNetMode()==NM_ListenServer) {Status=TEXT("Хост не может перейти в другой хаб.");return;}
  PendingJoinTicket=Ticket;PendingServerId=ServerId;JoinDeadline=FPlatformTime::Seconds()+30;bRedeemSent=false;
  if(!SameHub) LocalPC->ClientTravel(Address,TRAVEL_Absolute);
 });
}
void UTowerSocialSubsystem::JoinFinished(bool bSuccess,const FString& Message)
{
 PendingJoinTicket.Empty();PendingServerId.Empty();JoinDeadline=0;bRedeemSent=false;
 Status=Message;NextRefresh=0;
 if(bSuccess) UE_LOG(LogTemp,Display,TEXT("TOWER_SOCIAL_JOIN_COMPLETE"));
}
