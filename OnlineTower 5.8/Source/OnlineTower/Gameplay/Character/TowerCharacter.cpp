#include "TowerCharacter.h"
#include "TowerHub.h"
#include "TowerMovement.h"
#include "TowerAbilities.h"
#include "TowerWorld.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	bool IsTowerInputBlocked(const AController* Controller)
	{
		const auto* HubController=Cast<ATowerHubController>(Controller);
		return !Controller || Controller->IsMoveInputIgnored() || (HubController && HubController->IsGameplayInputBlocked());
	}
}

ATowerCharacter::ATowerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTowerMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	GetCapsuleComponent()->InitCapsuleSize(38.f, 90.f);
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0, 540, 0);
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->JumpZVelocity = 520.f;
	GetCharacterMovement()->AirControl = 0.3f;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMaterial(TEXT("/Game/Core/Materials/M_Character.M_Character"));
	VisualRoot=CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(GetCapsuleComponent());
	auto Part = [this](const TCHAR* Name, UStaticMesh* ShapeMesh, FVector Location, FVector Scale)
	{
		auto* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(VisualRoot);
		Component->SetStaticMesh(ShapeMesh);
		Component->SetRelativeLocation(Location);
		Component->SetRelativeScale3D(Scale);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (BodyMaterial.Succeeded()) Component->SetMaterial(0,BodyMaterial.Object);
	};
	Part(TEXT("Torso"), Sphere.Object, FVector(0, 0, 0), FVector(.52, .45, .85));
	Part(TEXT("Head"), Sphere.Object, FVector(0, 0, 60), FVector(.38));
	Part(TEXT("Visor"), Cube.Object, FVector(18, 0, 62), FVector(.08, .3, .1));
	Part(TEXT("LeftArm"), Cube.Object, FVector(0, -33, 0), FVector(.17, .17, .65));
	Part(TEXT("RightArm"), Cube.Object, FVector(0, 33, 0), FVector(.17, .17, .65));
	Part(TEXT("LeftLeg"), Cube.Object, FVector(0, -14, -60), FVector(.21, .2, .52));
	Part(TEXT("RightLeg"), Cube.Object, FVector(0, 14, -60), FVector(.21, .2, .52));
	auto* Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	Boom->SetupAttachment(GetCapsuleComponent());
	Boom->TargetArmLength = 420.f;
	Boom->SocketOffset = FVector(0, 55, 70);
	Boom->bUsePawnControlRotation = true;
	auto* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	Camera->SetupAttachment(Boom);
	Camera->FieldOfView = 85.f;
	SkillPulse=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkillPulse"));
	SkillPulse->SetupAttachment(GetCapsuleComponent()); SkillPulse->SetStaticMesh(Sphere.Object);
	SkillPulse->SetCollisionEnabled(ECollisionEnabled::NoCollision); SkillPulse->SetVisibility(false); SkillPulse->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PulseMaterial(TEXT("/Game/Core/Materials/M_Portal.M_Portal"));
	if (PulseMaterial.Succeeded()) SkillPulse->SetMaterial(0,PulseMaterial.Object);
}

void ATowerCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	auto* Enhanced = Cast<UEnhancedInputComponent>(Input);
	auto* PC = Cast<APlayerController>(GetController());
	if (!Enhanced || !PC || !PC->GetLocalPlayer()) return;
	auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	if (!Subsystem) return;
	if (MovementContext) Subsystem->RemoveMappingContext(MovementContext);
	MovementContext = NewObject<UInputMappingContext>(this);
	auto Axis = [this](FKey Positive, FKey Negative)
	{
		auto* Action = NewObject<UInputAction>(MovementContext);
		Action->ValueType = EInputActionValueType::Axis1D;
		MovementContext->MapKey(Action, Positive);
		if (Negative.IsValid()) MovementContext->MapKey(Action, Negative).Modifiers.Add(NewObject<UInputModifierNegate>(MovementContext));
		return Action;
	};
	Enhanced->BindAction(Axis(EKeys::W, EKeys::S), ETriggerEvent::Triggered, this, &ATowerCharacter::MoveForward);
	Enhanced->BindAction(Axis(EKeys::D, EKeys::A), ETriggerEvent::Triggered, this, &ATowerCharacter::MoveRight);
	Enhanced->BindAction(Axis(EKeys::MouseX, FKey()), ETriggerEvent::Triggered, this, &ATowerCharacter::LookYaw);
	Enhanced->BindAction(Axis(EKeys::MouseY, FKey()), ETriggerEvent::Triggered, this, &ATowerCharacter::LookPitch);
	auto* JumpAction = NewObject<UInputAction>(MovementContext);
	JumpAction->ValueType = EInputActionValueType::Boolean;
	MovementContext->MapKey(JumpAction, EKeys::SpaceBar);
	Enhanced->BindAction(JumpAction, ETriggerEvent::Started, this, &ATowerCharacter::StartJump);
	Enhanced->BindAction(JumpAction, ETriggerEvent::Completed, this, &ATowerCharacter::EndJump);
	auto Button=[this](FKey Key) { auto* Action=NewObject<UInputAction>(MovementContext); Action->ValueType=EInputActionValueType::Boolean; MovementContext->MapKey(Action,Key); return Action; };
	auto* Sprint=Button(EKeys::LeftShift);
	Enhanced->BindAction(Sprint,ETriggerEvent::Started,this,&ATowerCharacter::SprintPressed);
	Enhanced->BindAction(Sprint,ETriggerEvent::Completed,this,&ATowerCharacter::SprintReleased);
	Enhanced->BindAction(Sprint,ETriggerEvent::Canceled,this,&ATowerCharacter::SprintReleased);
	Enhanced->BindAction(Button(EKeys::LeftControl),ETriggerEvent::Started,this,&ATowerCharacter::RollPressed);
	Enhanced->BindAction(Button(EKeys::LeftMouseButton),ETriggerEvent::Started,this,&ATowerCharacter::AttackPressed);
	Subsystem->AddMappingContext(MovementContext, 0);
}

void ATowerCharacter::MoveForward(const FInputActionValue& Value)
{
	if (Controller) AddMovementInput(FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::X), Value.Get<float>());
}
void ATowerCharacter::MoveRight(const FInputActionValue& Value)
{
	if (Controller) AddMovementInput(FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), Value.Get<float>());
}
void ATowerCharacter::LookYaw(const FInputActionValue& Value) { AddControllerYawInput(Value.Get<float>()); }
void ATowerCharacter::LookPitch(const FInputActionValue& Value) { AddControllerPitchInput(-Value.Get<float>()); }
void ATowerCharacter::StartJump(const FInputActionValue& Value) { if (!IsTowerInputBlocked(Controller)) Jump(); }
void ATowerCharacter::EndJump(const FInputActionValue& Value) { StopJumping(); }

void ATowerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (auto* PS=GetPlayerState<ATowerPlayerState>()) PS->InitializeCharacter(this);
}
void ATowerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (auto* PS=GetPlayerState<ATowerPlayerState>()) PS->InitializeCharacter(this);
}
void ATowerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ATowerCharacter,TrialIndex); DOREPLIFETIME(ATowerCharacter,RollReadyAt); DOREPLIFETIME(ATowerCharacter,RollStartedAt);
}
void ATowerCharacter::Tick(float Delta)
{
	Super::Tick(Delta);
	const auto* GS=GetWorld()->GetGameState();
	const float Time=GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float RollAge=Time-RollStartedAt;
	VisualRoot->SetRelativeRotation(RollAge>=0 && RollAge<.5f ? FRotator(-360*RollAge/.5f,0,0) : FRotator::ZeroRotator);
	PulseAge+=Delta;
	if (PulseAge<.45f) SkillPulse->SetWorldScale3D(FVector(FMath::Max(.1f,PulseRadius/50.f*PulseAge/.45f),FMath::Max(.1f,PulseRadius/50.f*PulseAge/.45f),.08f));
	else SkillPulse->SetVisibility(false);
}
void ATowerCharacter::SetSprint(bool bEnabled) { CastChecked<UTowerMovement>(GetCharacterMovement())->bWantsSprint=bEnabled; }
void ATowerCharacter::SprintPressed(const FInputActionValue& Value) { if (!IsTowerInputBlocked(Controller)) SetSprint(true); }
void ATowerCharacter::SprintReleased(const FInputActionValue& Value) { SetSprint(false); }
void ATowerCharacter::RollPressed(const FInputActionValue& Value) { RequestRoll(); }
void ATowerCharacter::RequestRoll()
{
	if (IsTowerInputBlocked(Controller)) return;
	FVector Direction=GetLastMovementInputVector().GetSafeNormal2D();
	if (Direction.IsNearlyZero()) Direction=GetActorForwardVector();
	ServerRoll(Direction);
}
void ATowerCharacter::ServerRoll_Implementation(FVector_NetQuantizeNormal Direction)
{
	auto* PS=GetPlayerState<ATowerPlayerState>();
	if (!PS || PS->Health()<=0 || !GetCharacterMovement()->IsMovingOnGround() || PS->ServerTime()<RollReadyAt) return;
	const FVector SafeDirection=FVector(Direction).GetSafeNormal2D();
	if (SafeDirection.IsNearlyZero()) return;
	RollStartedAt=PS->ServerTime(); RollReadyAt=RollStartedAt+2.f;
	LaunchCharacter(SafeDirection*1100.f+FVector(0,0,60),true,true); ForceNetUpdate();
	UE_LOG(LogTemp,Display,TEXT("TOWER_ROLL Player=%d"),PS->GetPlayerId());
}
void ATowerCharacter::ServerCast_Implementation(int32 Slot)
{
	if (auto* PS=GetPlayerState<ATowerPlayerState>()) PS->ActivateSlot(Slot);
}
void ATowerCharacter::AttackPressed(const FInputActionValue& Value)
{
	if (!IsTowerInputBlocked(Controller)) ServerBasicAttack();
}
void ATowerCharacter::ServerBasicAttack_Implementation()
{
	auto* PS=GetPlayerState<ATowerPlayerState>();
	if (!PS || PS->Health()<=0 || BasicReadyAt>PS->ServerTime()) return;
	BasicReadyAt=PS->ServerTime()+.65f; MulticastSkillEffect(-1);
	auto InRange=[this](AActor* Target)
	{
		const FVector Delta=Target->GetActorLocation()-GetActorLocation();
		return Delta.Size()<240 && FVector::DotProduct(GetActorForwardVector(),Delta.GetSafeNormal())>.25;
	};
	for (TActorIterator<ATowerPracticeDummy> It(GetWorld());It;++It)
		if (InRange(*It)) UGameplayStatics::ApplyDamage(*It,12,GetController(),this,UDamageType::StaticClass());
	if (TrialIndex==1) for (TActorIterator<ATowerCharacter> It(GetWorld());It;++It)
		if (*It!=this && It->TrialIndex==1 && InRange(*It)) UGameplayStatics::ApplyDamage(*It,12,GetController(),this,UDamageType::StaticClass());
}
void ATowerCharacter::MulticastSkillEffect_Implementation(int32 Id)
{
	const auto& Pool=GetDefault<UTowerAbilityPool>()->Skills;
	PulseRadius=Pool.IsValidIndex(Id) ? Pool[Id].Radius : 240;
	PulseAge=0; SkillPulse->SetVisibility(true); SkillPulse->SetRelativeLocation(FVector(0,0,-75));
	if (auto* Material=SkillPulse->CreateAndSetMaterialInstanceDynamic(0)) Material->SetVectorParameterValue(TEXT("Tint"),Pool.IsValidIndex(Id) ? Pool[Id].Color : FLinearColor::White);
}
float ATowerCharacter::TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
	auto* PS=GetPlayerState<ATowerPlayerState>();
	auto* Attacker=Cast<ATowerCharacter>(Causer);
	if (!HasAuthority() || !PS || bDying || Amount<=0 || TrialIndex!=1 || !Attacker || Attacker==this || Attacker->TrialIndex!=1) return 0;
	PS->ChangeHealth(-Amount);
	if (PS->Health()<=0)
	{
		bDying=true; GetCharacterMovement()->DisableMovement();
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle,FTimerDelegate::CreateWeakLambda(this,[this]
		{
			AController* PC=GetController();
			if (PC) { PC->UnPossess(); GetWorld()->GetAuthGameMode()->RestartPlayer(PC); }
			Destroy();
		}),2,false);
	}
	return Amount;
}
