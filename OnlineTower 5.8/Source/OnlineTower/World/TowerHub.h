#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "TowerHub.generated.h"

class UTowerTrialCatalog;

enum class ETowerHubScreen : uint8 { Gameplay, Trials, Pause, Settings };

UCLASS()
class ONLINETOWER_API ATowerHubEnvironment : public AActor
{
	GENERATED_BODY()
public:
	ATowerHubEnvironment();
	virtual void BeginPlay() override;
};

UCLASS()
class ONLINETOWER_API ATowerOfficeEnvironment : public AActor
{
 GENERATED_BODY()
public: ATowerOfficeEnvironment();
};
UCLASS()
class ONLINETOWER_API ATowerDungeonEnvironment : public AActor
{
 GENERATED_BODY()
public: ATowerDungeonEnvironment();
};
UCLASS()
class ONLINETOWER_API ATowerDuelEnvironment : public AActor
{
 GENERATED_BODY()
public: ATowerDuelEnvironment();
};
UCLASS()
class ONLINETOWER_API ATowerFinalEnvironment : public AActor
{
 GENERATED_BODY()
public: ATowerFinalEnvironment();
};

UCLASS()
class ONLINETOWER_API ATowerHubGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ATowerHubGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
private:
	UPROPERTY() TArray<TObjectPtr<AActor>> HubStarts;
	int32 NextStart = 0;
};

UCLASS()
class ONLINETOWER_API ATowerTrialGameMode : public ATowerHubGameMode
{
 GENERATED_BODY()
};

UCLASS()
class ONLINETOWER_API ATowerHubController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void SetupInputComponent() override;
	UFUNCTION(Exec) void ToggleTrials();
	void SelectFirst();
	void SelectSecond();
	void SelectThird();
	void UseUltimate();
	virtual void BeginPlay() override;
	virtual void Tick(float Delta) override;
	UFUNCTION(Exec) void TowerRespawn();
	bool IsTrialMenuOpen() const { return Screen == ETowerHubScreen::Trials; }
	bool IsGameplayInputBlocked() const { return Screen != ETowerHubScreen::Gameplay; }
	ETowerHubScreen GetMenuScreen() const { return Screen; }
	void TogglePauseMenu();
	void SetMenuScreen(ETowerHubScreen NewScreen);
	void ReturnToMainMenu();
	void QuitToDesktop();
	UFUNCTION(Client, Reliable) void ClientTravelUnavailable();
	FString TravelNotice;
	float TravelNoticeUntil = 0;
private:
	ETowerHubScreen Screen = ETowerHubScreen::Gameplay;
	void SelectTrial(int32 Index);
	float ProbeAge=0;
	bool bProbeLogged=false;
	bool bProbeCast=false;
	bool bProbeRoll=false;
	bool bProbeVisitedTrial=false;
	bool bProbeReturned=false;
};

UCLASS()
class ONLINETOWER_API ATowerHubHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;
	void SelectTrial(int32 Index);
	UPROPERTY(EditDefaultsOnly, Category="Tower") TObjectPtr<UTowerTrialCatalog> TrialCatalog;
private:
	int32 SelectedTrial = 0;
	const UTowerTrialCatalog* Catalog() const;
};
