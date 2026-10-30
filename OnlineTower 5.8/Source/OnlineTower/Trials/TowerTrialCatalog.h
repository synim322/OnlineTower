#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TowerTrialCatalog.generated.h"

USTRUCT(BlueprintType)
struct FTowerTrialEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName MapPath;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Title;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxPlayers = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRequiresFloorKey = false;
};

// Content definition only. Matchmaking and server-validated admission are separate systems.
UCLASS(BlueprintType)
class ONLINETOWER_API UTowerTrialCatalog : public UDataAsset
{
	GENERATED_BODY()
public:
	UTowerTrialCatalog();
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTowerTrialEntry> Trials;
};
