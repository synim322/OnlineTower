#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "TowerAccountSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FTowerAccountChanged);

// Per game instance: PIE clients never share credentials. Tokens stay in memory.
UCLASS()
class ONLINETOWER_API UTowerAccountSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void Register(const FString& Username, const FString& Password);
	void Login(const FString& Username, const FString& Password);
	void Logout();
	void ConnectLocal();
	void CompleteConnection() { bConnecting=false; LastError.Empty(); }
	const FString& GetToken() const { return Token; }
	const FString& GetUserId() const { return UserId; }
	const FString& GetUsername() const { return Username; }
	const FString& GetLastError() const { return LastError; }
	bool IsBusy() const { return bBusy; }
	bool IsConnecting() const { return bConnecting; }
	bool IsLoggedIn() const { return !Token.IsEmpty(); }
	FTowerAccountChanged OnChanged;
private:
	void RequestAuth(const FString& Route, const FString& Name, const FString& Password);
	FString Token, UserId, Username, LastError;
	bool bBusy = false;
	uint32 RequestGeneration = 0;
	bool bConnecting = false;
	void HandleNetworkFailure(UWorld* World, class UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
};
