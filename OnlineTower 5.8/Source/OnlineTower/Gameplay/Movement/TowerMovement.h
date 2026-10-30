#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TowerMovement.generated.h"

UCLASS()
class ONLINETOWER_API UTowerMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	bool bWantsSprint = false;
	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};
