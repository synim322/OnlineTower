#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "GameFramework/SaveGame.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "TowerFrontend.generated.h"

USTRUCT()
struct FTowerCharacterProfile
{
	GENERATED_BODY()
	UPROPERTY() FGuid Id;
	UPROPERTY() FString Name;
	UPROPERTY() FName Direction = TEXT("MeleeDamage");
	UPROPERTY() TArray<FName> Skills;
};

UCLASS()
class ONLINETOWER_API UTowerProfileSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() int32 Version = 1;
	UPROPERTY() TArray<FTowerCharacterProfile> Characters;
	UPROPERTY() FGuid SelectedId;
};

UCLASS()
class ONLINETOWER_API UTowerGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
	virtual void Init() override;
	virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld) override;
	bool HasSelectedCharacter() const { return GetSelectedCharacter() != nullptr; }
	const FTowerCharacterProfile* GetSelectedCharacter() const;
	const TArray<FTowerCharacterProfile>& GetCharacters() const;
	bool CreateCharacter(const FString& Name, FName Direction, FString& Error);
	bool SelectCharacter(FGuid Id);
	void EnterHub();
	void ReturnToMainMenu();
	const FString& GetProfileError() const { return ProfileError; }
	static bool ResolveSkills(const FTowerCharacterProfile& Profile, TArray<int32>& Out);
private:
	UPROPERTY() TObjectPtr<UTowerProfileSaveGame> Profiles;
	FString ProfileError;
	bool bProfileWritable = true;
};

UCLASS(Config=GameUserSettings)
class ONLINETOWER_API UTowerUserSettings : public UGameUserSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(Config) float MasterVolume = 1.f;
	static UTowerUserSettings* Get();
	void ApplyAudio(UWorld* World) const;
};

namespace TowerMenuUI
{
	void DrawSettings(AHUD* HUD);
	bool HandleSettingsClick(APlayerController* PC, FName BoxName);
}

UCLASS()
class ONLINETOWER_API ATowerFrontendGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ATowerFrontendGameMode();
};

UCLASS()
class ONLINETOWER_API ATowerFrontendController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	void Back();
	// 0: home, 1: character selection, 2: creation, 3: settings.
	int32 Page = 0;
	FString Message;
};

UCLASS()
class ONLINETOWER_API ATowerFrontendHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;
};
