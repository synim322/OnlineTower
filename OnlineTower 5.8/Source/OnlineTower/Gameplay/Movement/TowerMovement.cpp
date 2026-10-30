#include "TowerMovement.h"
#include "GameFramework/Character.h"

class FTowerSavedMove : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;
	bool bSprint = false;
	virtual void Clear() override { Super::Clear(); bSprint = false; }
	virtual uint8 GetCompressedFlags() const override { return Super::GetCompressedFlags() | (bSprint ? FLAG_Custom_0 : 0); }
	virtual bool CanCombineWith(const FSavedMovePtr& Move, ACharacter* Character, float MaxDelta) const override
	{
		return bSprint == static_cast<const FTowerSavedMove*>(Move.Get())->bSprint && Super::CanCombineWith(Move, Character, MaxDelta);
	}
	virtual void SetMoveFor(ACharacter* Character, float Delta, const FVector& Accel, FNetworkPredictionData_Client_Character& Data) override
	{
		Super::SetMoveFor(Character, Delta, Accel, Data);
		bSprint = CastChecked<UTowerMovement>(Character->GetCharacterMovement())->bWantsSprint;
	}
	virtual void PrepMoveFor(ACharacter* Character) override
	{
		Super::PrepMoveFor(Character);
		CastChecked<UTowerMovement>(Character->GetCharacterMovement())->bWantsSprint = bSprint;
	}
};

class FTowerPredictionData : public FNetworkPredictionData_Client_Character
{
public:
	using Super = FNetworkPredictionData_Client_Character;
	explicit FTowerPredictionData(const UCharacterMovementComponent& Movement) : Super(Movement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FTowerSavedMove()); }
};

float UTowerMovement::GetMaxSpeed() const { return bWantsSprint && IsMovingOnGround() ? 850.f : Super::GetMaxSpeed(); }
void UTowerMovement::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}
FNetworkPredictionData_Client* UTowerMovement::GetPredictionData_Client() const
{
	if (!ClientPredictionData) const_cast<UTowerMovement*>(this)->ClientPredictionData = new FTowerPredictionData(*this);
	return ClientPredictionData;
}
