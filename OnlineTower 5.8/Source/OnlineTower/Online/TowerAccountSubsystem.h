#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TowerAccountSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FTowerAccountChanged);

// Per game instance: PIE clients never share credentials. Tokens stay in memory.
UCLASS()
class ONLINETOWER_API UTowerAccountSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void Register(const FString& Username, const FString& Password);
	void Login(const FString& Username, const FString& Password);
	void Logout();
	void ConnectLocal();
	const FString& GetToken() const { return Token; }
	const FString& GetUserId() const { return UserId; }
	const FString& GetUsername() const { return Username; }
	const FString& GetLastError() const { return LastError; }
	bool IsBusy() const { return bBusy; }
	bool IsLoggedIn() const { return !Token.IsEmpty(); }
	FTowerAccountChanged OnChanged;
private:
	void RequestAuth(const FString& Route, const FString& Name, const FString& Password);
	FString Token, UserId, Username, LastError;
	bool bBusy = false;
	uint32 RequestGeneration = 0;
};
