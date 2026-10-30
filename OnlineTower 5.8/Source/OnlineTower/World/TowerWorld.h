#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TowerWorld.generated.h"

class UPrimitiveComponent;
class UTextRenderComponent;
class UStaticMeshComponent;
void BuildTowerEnvironment(AActor* Actor, int32 Zone = -2);
FVector TowerHubOrigin();
FVector TowerTrialLocation(int32 Trial);

UCLASS()
class ONLINETOWER_API ATowerPortal : public AActor
{
	GENERATED_BODY()
public:
	ATowerPortal();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(Replicated) FVector Destination;
	UPROPERTY(ReplicatedUsing=UpdateLabel) int32 DestinationTrial = -1;
	UFUNCTION() void UpdateLabel();
private:
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
	FTimerHandle OverlapTimer;
	void CheckVisitors();
	UFUNCTION() void Enter(UPrimitiveComponent* Overlapped,AActor* Other,UPrimitiveComponent* OtherComponent,int32 BodyIndex,bool bSweep,const FHitResult& Hit);
};

UCLASS()
class ONLINETOWER_API ATowerPracticeDummy : public AActor
{
	GENERATED_BODY()
public:
	ATowerPracticeDummy();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* Instigator,AActor* Causer) override;
	UPROPERTY(ReplicatedUsing=UpdateLabel) float Health=200;
	UFUNCTION() void UpdateLabel();
private:
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
	FTimerHandle ResetTimer;
};
