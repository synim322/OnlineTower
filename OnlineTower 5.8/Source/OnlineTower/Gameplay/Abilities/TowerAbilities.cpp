#include "TowerAbilities.h"
#include "TowerCharacter.h"
#include "TowerWorld.h"
#include "TowerFrontend.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffect.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

UTowerAbilityPool::UTowerAbilityPool()
{
	auto Add = [this](const TCHAR* Id, const TCHAR* Title, const TCHAR* Rarity, float Weight, float Damage, float Radius, float Cooldown, FLinearColor Color)
	{
		FTowerSkill S; S.Id=Id; S.Title=Title; S.Rarity=Rarity; S.Weight=Weight; S.Damage=Damage; S.Radius=Radius; S.Cooldown=Cooldown; S.Color=Color; Skills.Add(S);
	};
	Add(TEXT("Cleave"),TEXT("Рассечение"),TEXT("Обычный"),40,22,250,3,FLinearColor(.65,.8,1));
	Add(TEXT("Thrust"),TEXT("Выпад"),TEXT("Обычный"),30,30,350,4,FLinearColor(.7,.8,1));
	Add(TEXT("Whirlwind"),TEXT("Вихрь"),TEXT("Редкий"),15,32,430,7,FLinearColor(.1,.7,1));
	Add(TEXT("Shockwave"),TEXT("Ударная волна"),TEXT("Редкий"),10,45,600,9,FLinearColor(.1,.5,1));
	Add(TEXT("Siphon"),TEXT("Кровавый удар"),TEXT("Эпический"),4,55,400,12,FLinearColor(.8,.15,1));
	Add(TEXT("Judgement"),TEXT("Приговор"),TEXT("Легендарный"),1,80,500,16,FLinearColor(1,.6,.08));
	Add(TEXT("Earthbreaker"),TEXT("Раскол земли"),TEXT("Ульта"),0,100,850,30,FLinearColor(1,.25,.02));
}

TArray<int32> UTowerAbilityPool::Roll(FRandomStream& Random) const
{
	TArray<int32> Available, Result;
	for (int32 I=0; I<Skills.Num()-1; ++I) if (Skills[I].Weight>0) Available.Add(I);
	while (Result.Num()<3 && Available.Num())
	{
		float Total=0; for (int32 I:Available) Total+=Skills[I].Weight;
		float Pick=Random.FRand()*Total;
		int32 Chosen=Available.Num()-1;
		for (int32 I=0; I<Available.Num(); ++I) { Pick-=Skills[Available[I]].Weight; if (Pick<0) { Chosen=I; break; } }
		Result.Add(Available[Chosen]); Available.RemoveAt(Chosen);
	}
	return Result;
}

UTowerAttributes::UTowerAttributes() { Health.SetBaseValue(100); Health.SetCurrentValue(100); }
FGameplayAttribute UTowerAttributes::GetHealthAttribute() { return FGameplayAttribute(FindFieldChecked<FProperty>(StaticClass(),GET_MEMBER_NAME_CHECKED(UTowerAttributes,Health))); }
void UTowerAttributes::OnRep_Health(const FGameplayAttributeData& OldHealth) { GAMEPLAYATTRIBUTE_REPNOTIFY(UTowerAttributes,Health,OldHealth); }
void UTowerAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME_CONDITION_NOTIFY(UTowerAttributes,Health,COND_None,REPNOTIFY_Always);
}
ATowerPlayerState::ATowerPlayerState()
{
	Abilities=CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("Abilities"));
	Abilities->SetIsReplicated(true); Abilities->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	Attributes=CreateDefaultSubobject<UTowerAttributes>(TEXT("Attributes"));
	SetNetUpdateFrequency(30.f);
}
void ATowerPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ATowerPlayerState,SkillIds); DOREPLIFETIME(ATowerPlayerState,ReadyAt);
}
float ATowerPlayerState::ServerTime() const { const auto* GS=GetWorld()->GetGameState(); return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds(); }
float ATowerPlayerState::Health() const { return Attributes->Health.GetCurrentValue(); }
void ATowerPlayerState::InitializeCharacter(AActor* Avatar)
{
	Abilities->InitAbilityActorInfo(this,Avatar);
	if (!HasAuthority()) return;
	if (SkillIds.IsEmpty())
	{
		// Local saves are trusted only by the standalone authority, never by a network server.
		if(GetWorld()->GetNetMode()==NM_Standalone)
			if(const auto* GI=GetGameInstance<UTowerGameInstance>())
				if(const auto* Profile=GI->GetSelectedCharacter()) UTowerGameInstance::ResolveSkills(*Profile,SkillIds);
		if(SkillIds.IsEmpty())
		{
			FRandomStream Random(FMath::Rand());
			SkillIds=GetDefault<UTowerAbilityPool>()->Roll(Random);
			SkillIds.Add(GetDefault<UTowerAbilityPool>()->Skills.Num()-1);
		}
		ReadyAt.Init(0,SkillIds.Num());
		for (int32 Id:SkillIds) Handles.Add(Abilities->GiveAbility(FGameplayAbilitySpec(UTowerCombatAbility::StaticClass(),Id+1)));
		UE_LOG(LogTemp,Display,TEXT("TOWER_LOADOUT Player=%d Skills=%s"),GetPlayerId(),*FString::JoinBy(SkillIds,TEXT(","),[](int32 Id){return FString::FromInt(Id);}));
	}
	Abilities->SetNumericAttributeBase(UTowerAttributes::GetHealthAttribute(),100);
}
void ATowerPlayerState::ChangeHealth(float Amount)
{
	if (!HasAuthority()) return;
	const float Delta=FMath::Clamp(Health()+Amount,0.f,100.f)-Health();
	auto* Effect=NewObject<UGameplayEffect>(GetTransientPackage());
	Effect->DurationPolicy=EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo Modifier; Modifier.Attribute=UTowerAttributes::GetHealthAttribute(); Modifier.ModifierOp=EGameplayModOp::Additive; Modifier.ModifierMagnitude=FScalableFloat(Delta);
	Effect->Modifiers.Add(Modifier);
	Abilities->ApplyGameplayEffectToSelf(Effect,1,Abilities->MakeEffectContext());
}
void ATowerPlayerState::ActivateSlot(int32 Slot)
{
	if (HasAuthority() && Handles.IsValidIndex(Slot) && Health()>0) Abilities->TryActivateAbility(Handles[Slot]);
}
bool ATowerPlayerState::ExecuteSkill(int32 Id)
{
	auto* Character=Cast<ATowerCharacter>(Abilities->GetAvatarActor());
	const auto& Pool=GetDefault<UTowerAbilityPool>()->Skills;
	const int32 Slot=SkillIds.Find(Id);
	if (!HasAuthority() || !Character || Health()<=0 || !ReadyAt.IsValidIndex(Slot) || !Pool.IsValidIndex(Id) || ReadyAt[Slot]>ServerTime()) return false;
	const FTowerSkill& Skill=Pool[Id];
	ReadyAt[Slot]=ServerTime()+Skill.Cooldown;
	const FVector Center=Character->GetActorLocation();
	Character->MulticastSkillEffect(Id);
	int32 Hits=0;
	auto InRange=[&](AActor* Target)
	{
		const FVector Delta=Target->GetActorLocation()-Center;
		return Delta.Size()<=Skill.Radius && (Id>1 || FVector::DotProduct(Character->GetActorForwardVector(),Delta.GetSafeNormal())>.25f);
	};
	for (TActorIterator<ATowerPracticeDummy> It(GetWorld());It;++It)
		if (InRange(*It)) { UGameplayStatics::ApplyDamage(*It,Skill.Damage,Character->GetController(),Character,UDamageType::StaticClass()); ++Hits; }
	// PvP is enabled only on the duel training platform. The hub remains a safe zone.
	if (Character->TrialIndex==1)
		for (TActorIterator<ATowerCharacter> It(GetWorld());It;++It)
			if (*It!=Character && It->TrialIndex==1 && InRange(*It)) { UGameplayStatics::ApplyDamage(*It,Skill.Damage,Character->GetController(),Character,UDamageType::StaticClass()); ++Hits; }
	if (Id==4 && Hits) ChangeHealth(25);
	UE_LOG(LogTemp,Display,TEXT("TOWER_CAST Player=%d Skill=%d Hits=%d"),GetPlayerId(),Id,Hits);
	ForceNetUpdate(); return true;
}
UTowerCombatAbility::UTowerCombatAbility()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
}
void UTowerCombatAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,const FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event)
{
	bool bSuccess=false;
	if (auto* PS=Cast<ATowerPlayerState>(Info->OwnerActor.Get()))
		if (CommitAbility(Handle,Info,Activation)) bSuccess=PS->ExecuteSkill(GetAbilityLevel(Handle,Info)-1);
	EndAbility(Handle,Info,Activation,true,!bSuccess);
}
