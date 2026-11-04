#include "TowerAccountSubsystem.h"
#include "TowerOnlineHttp.h"
#include "TowerMaps.h"
#include "TowerFrontend.h" // TowerMenuUI::CloseOnline + ATowerFrontendController::ServerEnterHubMap
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace TowerAccountPrivate
{
static bool IsHubWorld(const UWorld* World)
{
	if (!World) return false;
	const FString MapName = World->GetMapName();
	return MapName.Contains(TEXT("L_Hub"));
}
}

void UTowerAccountSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &UTowerAccountSubsystem::HandleNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &UTowerAccountSubsystem::HandleTravelFailure);
	}
}
void UTowerAccountSubsystem::Deinitialize()
{
	if (GEngine) { GEngine->OnNetworkFailure().RemoveAll(this); GEngine->OnTravelFailure().RemoveAll(this); }
	++RequestGeneration;
	Super::Deinitialize();
}
void UTowerAccountSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver*, ENetworkFailure::Type, const FString& Error)
{
	if (!World || World->GetGameInstance() != GetGameInstance()) return;
	bConnecting=false; LastError=TEXT("Не удалось подключиться к хабу. Проверьте запуск локального игрового сервера. ")+Error;
	OnChanged.Broadcast();
}
void UTowerAccountSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type, const FString& Error)
{
	if (!World || World->GetGameInstance() != GetGameInstance()) return;
	bConnecting=false; LastError=TEXT("Ошибка перехода в хаб: ")+Error; OnChanged.Broadcast();
}

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
	Request->SetURL(TowerOnlineHttp::BaseUrl()+TEXT("/v1/auth/")+Route);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
	Request->SetContentAsString(Payload);
	Request->SetTimeout(10.f);
	Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Generation](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
	{
		if (Generation!=RequestGeneration) return;
		bBusy=false;
		TSharedPtr<FJsonObject> Json; FString Error;
		if (!TowerOnlineHttp::Decode(Response,bSuccess,Json,Error)) LastError=Error;
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
	++RequestGeneration; bBusy=false; bConnecting=false;
	if (!Token.IsEmpty())
	{
		auto Request=FHttpModule::Get().CreateRequest();
		Request->SetURL(TowerOnlineHttp::BaseUrl()+TEXT("/v1/auth/logout"));
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
	if (!IsLoggedIn()) { LastError=TEXT("Сначала войдите в аккаунт."); OnChanged.Broadcast(); return; }
	UWorld* World = GetWorld();
	if (!World) { LastError=TEXT("Мир ещё не готов."); OnChanged.Broadcast(); return; }

	// Already on the hub map inside PIE/listen/client — release the player, do not look like a failed connect.
	if (TowerAccountPrivate::IsHubWorld(World))
	{
		bConnecting=false;
		LastError.Empty();
		OnChanged.Broadcast();
		if (APlayerController* PC=GetGameInstance()->GetFirstLocalPlayerController())
			TowerMenuUI::CloseOnline(PC);
		return;
	}

	if (bConnecting && World->GetNetMode()==NM_Standalone) return;

	// PIE / multiplayer already running on the main menu: take the whole session into L_Hub.
	if (World->GetNetMode()==NM_ListenServer)
	{
		bConnecting=true;
		LastError=TEXT("Переход в хаб…");
		OnChanged.Broadcast();
		if (!TowerMaps::TravelToHubListen(World))
		{
			bConnecting=false;
			LastError=TEXT("Не удалось начать переход в хаб. Повторите попытку.");
			OnChanged.Broadcast();
		}
		return;
	}
	if (World->GetNetMode()==NM_Client)
	{
		if (ATowerFrontendController* Frontend=Cast<ATowerFrontendController>(GetGameInstance()->GetFirstLocalPlayerController()))
		{
			bConnecting=true;
			LastError=TEXT("Переход в хаб вместе с хостом…");
			OnChanged.Broadcast();
			Frontend->ServerEnterHubMap();
			return;
		}
		bConnecting=false;
		LastError=TEXT("Нажмите «Войти в хаб» на окне хоста (Listen Server), клиенты перейдут следом.");
		OnChanged.Broadcast();
		return;
	}

	// Solo PIE: open hub locally. Packaged/standalone menu: join dedicated hub process.
	if (World->WorldType==EWorldType::PIE)
	{
		bConnecting=true;
		LastError=TEXT("Переход в хаб…");
		OnChanged.Broadcast();
		UGameplayStatics::OpenLevel(World,FName(TowerMaps::Hub),true);
		return;
	}

	if (APlayerController* PC=GetGameInstance()->GetFirstLocalPlayerController())
	{
		FString Address=TEXT("127.0.0.1:17777");
		FParse::Value(FCommandLine::Get(),TEXT("TowerServer="),Address);
		if (Address.IsEmpty() || Address.Contains(TEXT("?")) || Address.Contains(TEXT("/")))
		{ LastError=TEXT("Некорректный адрес игрового сервера."); OnChanged.Broadcast(); return; }
		bConnecting=true; LastError=TEXT("Подключение к хабу…"); OnChanged.Broadcast();
		PC->ClientTravel(Address,TRAVEL_Absolute);
	}
}
