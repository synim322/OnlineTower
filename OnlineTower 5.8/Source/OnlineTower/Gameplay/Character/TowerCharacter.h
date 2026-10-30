#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TowerCharacter.generated.h"

class UInputMappingContext;
struct FInputActionValue;

UCLASS()
class ONLINETOWER_API ATowerCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ATowerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* Instigator,AActor* Causer) override;
	UPROPERTY(Replicated) int32 TrialIndex = -1;
	UPROPERTY(Replicated) float RollReadyAt = 0;
	UPROPERTY(Replicated) float RollStartedAt = -100;
	float NextPortalTime = 0;
	UFUNCTION(Server,Reliable) void ServerCast(int32 Slot);
	UFUNCTION(Server,Reliable) void ServerRoll(FVector_NetQuantizeNormal Direction);
	UFUNCTION(Server,Reliable) void ServerBasicAttack();
	UFUNCTION(NetMulticast,Unreliable) void MulticastSkillEffect(int32 SkillId);
	void SetSprint(bool bEnabled);
	void RequestRoll();
private:
	UPROPERTY() TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> SkillPulse;
	float PulseAge = 100;
	float PulseRadius = 0;
	float BasicReadyAt = 0;
	bool bDying = false;
	void SprintPressed(const FInputActionValue& Value);
	void SprintReleased(const FInputActionValue& Value);
	void RollPressed(const FInputActionValue& Value);
	void AttackPressed(const FInputActionValue& Value);
	UPROPERTY() TObjectPtr<UInputMappingContext> MovementContext;
	void MoveForward(const FInputActionValue& Value);
	void MoveRight(const FInputActionValue& Value);
	void LookYaw(const FInputActionValue& Value);
	void LookPitch(const FInputActionValue& Value);
	void StartJump(const FInputActionValue& Value);
	void EndJump(const FInputActionValue& Value);
};
