#pragma once
#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/PlayerState.h"
#include "TowerAbilities.generated.h"

USTRUCT(BlueprintType)
struct FTowerSkill
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) FName Id;
	UPROPERTY(EditAnywhere) FString Title;
	UPROPERTY(EditAnywhere) FString Rarity;
	UPROPERTY(EditAnywhere) float Weight = 1;
	UPROPERTY(EditAnywhere) float Damage = 20;
	UPROPERTY(EditAnywhere) float Radius = 250;
	UPROPERTY(EditAnywhere) float Cooldown = 5;
	UPROPERTY(EditAnywhere) FLinearColor Color = FLinearColor::White;
};

UCLASS(Config=Game, DefaultConfig)
class ONLINETOWER_API UTowerAbilityPool : public UObject
{
	GENERATED_BODY()
public:
	UTowerAbilityPool();
	UPROPERTY(Config, EditAnywhere) TArray<FTowerSkill> Skills;
	// Weighted sampling without replacement. The ultimate (last entry) is excluded.
	TArray<int32> Roll(FRandomStream& Random) const;
};

UCLASS()
class ONLINETOWER_API UTowerAttributes : public UAttributeSet
{
	GENERATED_BODY()
public:
	UTowerAttributes();
	UPROPERTY(ReplicatedUsing=OnRep_Health) FGameplayAttributeData Health;
	UFUNCTION() void OnRep_Health(const FGameplayAttributeData& OldHealth);
	static FGameplayAttribute GetHealthAttribute();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};

UCLASS()
class ONLINETOWER_API ATowerPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	ATowerPlayerState();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return Abilities; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UAbilitySystemComponent> Abilities;
	UPROPERTY() TObjectPtr<UTowerAttributes> Attributes;
	UPROPERTY(Replicated) TArray<int32> SkillIds;
	UPROPERTY(Replicated) TArray<float> ReadyAt;
	void InitializeCharacter(AActor* Avatar);
	void ActivateSlot(int32 Slot);
	float ServerTime() const;
	float Health() const;
	void ChangeHealth(float Amount);
	bool ExecuteSkill(int32 SkillId);
private:
	TArray<FGameplayAbilitySpecHandle> Handles;
};

UCLASS()
class ONLINETOWER_API UTowerCombatAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UTowerCombatAbility();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
