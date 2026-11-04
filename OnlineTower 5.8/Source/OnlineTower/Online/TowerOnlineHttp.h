#pragma once
#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace TowerOnlineHttp
{
 inline FString BaseUrl()
 {
  FString Url=TEXT("http://127.0.0.1:18080");
  FParse::Value(FCommandLine::Get(),TEXT("TowerBackend="),Url);
  while(Url.EndsWith(TEXT("/"))) Url.LeftChopInline(1);
  return Url;
 }
 inline bool Decode(const FHttpResponsePtr& Response,bool bSuccess,TSharedPtr<FJsonObject>& Json,FString& Error)
 {
  if(!bSuccess || !Response.IsValid()) { Error=TEXT("Сервис аккаунтов недоступен. Проверьте запуск backend."); return false; }
  if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json) || !Json.IsValid())
  {
   Error=FString::Printf(TEXT("Сервис вернул неверный формат (HTTP %d). Проверьте backend и исключение localhost из прокси."),Response->GetResponseCode());
   return false;
  }
  if(Response->GetResponseCode()<200 || Response->GetResponseCode()>=300)
  { if(!Json->TryGetStringField(TEXT("error"),Error)) Error=FString::Printf(TEXT("Ошибка сервиса: HTTP %d."),Response->GetResponseCode()); return false; }
  return true;
 }
}
