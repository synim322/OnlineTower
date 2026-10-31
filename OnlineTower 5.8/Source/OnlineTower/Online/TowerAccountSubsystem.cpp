#include "TowerAccountSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

void UTowerAccountSubsystem::Register(const FString& Name, const FString& Password)
{
	RequestAuth(TEXT("register"), Name, Password);
}
void UTowerAccountSubsystem::Login(const FString& Name, const FString& Password)
{
	RequestAuth(TEXT("login"), Name, Password);
}
void UTowerAccountSubsystem::RequestAuth(const FString& Route, const FString& Name, const FString& Password)
{
	if (bBusy) return;
	if (IsLoggedIn()) { LastError=TEXT("Log out before switching accounts."); OnChanged.Broadcast(); return; }
	LastError.Empty();
	if (Name.Len()<3 || Name.Len()>24 || Password.Len()<8 || Password.Len()>128)
	{
		LastError=TEXT("Username: 3-24 letters, digits or _. Password: 8-128 characters.");
		OnChanged.Broadcast(); return;
	}
	bBusy=true;
	const uint32 Generation=++RequestGeneration;
	auto Body=MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("username"),Name);
	Body->SetStringField(TEXT("password"),Password);
	FString Payload;
	FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload));
	auto Request=FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("http://127.0.0.1:18080/v1/auth/")+Route);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
	Request->SetContentAsString(Payload);
	Request->SetTimeout(10.f);
	Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Generation](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		if (Generation!=RequestGeneration) return;
		bBusy=false;
		TSharedPtr<FJsonObject> Json;
		if (!bSuccess || !Response.IsValid()) LastError=TEXT("Backend unavailable. Start the local backend on port 18080.");
		else if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json) || !Json.IsValid()) LastError=TEXT("Invalid backend response.");
		else if (Response->GetResponseCode()<200 || Response->GetResponseCode()>=300)
		{
			if (!Json->TryGetStringField(TEXT("error"),LastError)) LastError=TEXT("Authentication failed.");
		}
		else
		{
			const TSharedPtr<FJsonObject>* User=nullptr;
			FString NewToken, NewId, NewName;
			if (Json->TryGetStringField(TEXT("token"),NewToken) && Json->TryGetObjectField(TEXT("user"),User)
				&& (*User)->TryGetStringField(TEXT("id"),NewId) && (*User)->TryGetStringField(TEXT("username"),NewName)
				&& !NewToken.IsEmpty() && !NewId.IsEmpty())
			{ Token=NewToken; UserId=NewId; Username=NewName; LastError.Empty(); }
			else LastError=TEXT("Incomplete authentication response.");
		}
		OnChanged.Broadcast();
	});
	if (!Request->ProcessRequest()) { bBusy=false; LastError=TEXT("Could not start authentication request."); }
	OnChanged.Broadcast();
}
void UTowerAccountSubsystem::Logout()
{
	++RequestGeneration; bBusy=false;
	if (!Token.IsEmpty())
	{
		auto Request=FHttpModule::Get().CreateRequest();
		Request->SetURL(TEXT("http://127.0.0.1:18080/v1/auth/logout"));
		Request->SetVerb(TEXT("POST"));
		Request->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token);
		Request->SetTimeout(10.f);
		Request->ProcessRequest();
	}
	Token.Empty(); UserId.Empty(); Username.Empty(); LastError.Empty();
	OnChanged.Broadcast();
}
void UTowerAccountSubsystem::ConnectLocal()
{
	if (!IsLoggedIn()) { LastError=TEXT("Sign in first."); OnChanged.Broadcast(); return; }
	if (UWorld* World=GetWorld(); World && World->GetNetMode()!=NM_Standalone)
	{
		LastError=TEXT("Already in a multiplayer session (PIE clients connect automatically)."); OnChanged.Broadcast(); return;
	}
	if (APlayerController* PC=GetGameInstance()->GetFirstLocalPlayerController()) PC->ClientTravel(TEXT("127.0.0.1:17777"),TRAVEL_Absolute);
}
