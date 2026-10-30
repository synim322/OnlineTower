#include "TowerHub.h"
#include "TowerFrontend.h"
#include "TowerMaps.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "TowerCharacter.h"
#include "TowerAbilities.h"
#include "TowerWorld.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "TowerTrialCatalog.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ATowerHubEnvironment::ATowerHubEnvironment()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true);
    BuildTowerEnvironment(this);
}
ATowerOfficeEnvironment::ATowerOfficeEnvironment() { BuildTowerEnvironment(this,-1); }
ATowerDungeonEnvironment::ATowerDungeonEnvironment() { BuildTowerEnvironment(this,0); }
ATowerDuelEnvironment::ATowerDuelEnvironment() { BuildTowerEnvironment(this,1); }
ATowerFinalEnvironment::ATowerFinalEnvironment() { BuildTowerEnvironment(this,2); }
void ATowerHubEnvironment::BeginPlay() { Super::BeginPlay(); }
ATowerHubGameMode::ATowerHubGameMode()
{
	DefaultPawnClass = ATowerCharacter::StaticClass();
	PlayerControllerClass = ATowerHubController::StaticClass();
	HUDClass = ATowerHubHUD::StaticClass();
	PlayerStateClass = ATowerPlayerState::StaticClass();
}

void ATowerHubGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	const int32 Zone=TowerMaps::GetZone(GetWorld());
	const FVector Origin=TowerHubOrigin();
	UClass* EnvironmentClass=Zone == -1 ? ATowerOfficeEnvironment::StaticClass() : Zone == 0 ? ATowerDungeonEnvironment::StaticClass() : Zone == 1 ? ATowerDuelEnvironment::StaticClass() : Zone == 2 ? ATowerFinalEnvironment::StaticClass() : ATowerHubEnvironment::StaticClass();
	bool bHasEnvironment=false;
	for (TActorIterator<AActor> It(GetWorld(),EnvironmentClass);It;++It) { bHasEnvironment=true; break; }
	if (!bHasEnvironment) GetWorld()->SpawnActor<AActor>(EnvironmentClass,Origin,FRotator::ZeroRotator);
	auto SpawnPortal=[this](FVector Location,FVector Destination,int32 Trial)
	{
		const FTransform Transform(FRotator::ZeroRotator,Location);
		auto* Portal=GetWorld()->SpawnActorDeferred<ATowerPortal>(ATowerPortal::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Portal) return;
		Portal->Destination=Destination; Portal->DestinationTrial=Trial; Portal->FinishSpawning(Transform);
	};
	if (Zone < 0)
	{
		for (int32 I=0;I<3;++I) SpawnPortal(Origin+FVector(900,(I-1)*800,205),TowerTrialLocation(I)+FVector(-600,0,0),I);
		GetWorld()->SpawnActor<ATowerPracticeDummy>(Origin+FVector(0,650,75),FRotator::ZeroRotator);
	}
	for (int32 I=0;I<3;++I)
	{
		if (Zone != -2 && Zone != I) continue;
		const FVector Center=Zone == -2 ? TowerTrialLocation(I) : Origin+FVector(0,0,120);
		SpawnPortal(Center+FVector(-950,0,85),Origin+FVector(-500,0,110),-1);
		for (int32 J=0;J<3;++J) GetWorld()->SpawnActor<ATowerPracticeDummy>(Center+FVector(200,(J-1)*250,-45),FRotator::ZeroRotator);
	}
	for (int32 Index=0; Index<12; ++Index)
	{
		const FVector Position=Origin+FVector(-600+(Index/4)*150,-300+(Index%4)*200,120);
		if (auto* Start=GetWorld()->SpawnActor<APlayerStart>(Position,FRotator::ZeroRotator)) HubStarts.Add(Start);
	}
	UE_LOG(LogTemp,Display,TEXT("TOWER_MAP_READY Map=%s Zone=%d"),*MapName,Zone);
}

AActor* ATowerHubGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (HubStarts.Num()) return HubStarts[NextStart++ % HubStarts.Num()];
	return Super::ChoosePlayerStart_Implementation(Player);
}

void ATowerHubController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ATowerHubController::ToggleTrials);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ATowerHubController::SelectFirst);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ATowerHubController::SelectSecond);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ATowerHubController::SelectThird);
	InputComponent->BindKey(EKeys::Tab,IE_Pressed,this,&ATowerHubController::ToggleTrials);
	InputComponent->BindKey(EKeys::R,IE_Pressed,this,&ATowerHubController::UseUltimate);
	InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&ATowerHubController::TogglePauseMenu).bExecuteWhenPaused = true;
}

void ATowerHubController::ToggleTrials()
{
	if (Screen == ETowerHubScreen::Pause || Screen == ETowerHubScreen::Settings) return;
	SetMenuScreen(IsTrialMenuOpen() ? ETowerHubScreen::Gameplay : ETowerHubScreen::Trials);
}
void ATowerHubController::SetMenuScreen(ETowerHubScreen NewScreen)
{
	Screen = NewScreen;
	const bool bBlocked = IsGameplayInputBlocked();
	bShowMouseCursor = bBlocked;
	bEnableClickEvents = bBlocked;
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
	SetIgnoreMoveInput(bBlocked);
	SetIgnoreLookInput(bBlocked);
	if (auto* Hero=Cast<ATowerCharacter>(GetPawn())) { Hero->SetSprint(false); Hero->StopJumping(); }
	if (GetNetMode() == NM_Standalone) SetPause(Screen == ETowerHubScreen::Pause || Screen == ETowerHubScreen::Settings);
	if (bBlocked)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}
	else SetInputMode(FInputModeGameOnly());
}
void ATowerHubController::TogglePauseMenu()
{
	if (Screen == ETowerHubScreen::Settings) SetMenuScreen(ETowerHubScreen::Pause);
	else SetMenuScreen(Screen == ETowerHubScreen::Gameplay ? ETowerHubScreen::Pause : ETowerHubScreen::Gameplay);
}
void ATowerHubController::ReturnToMainMenu()
{
	SetMenuScreen(ETowerHubScreen::Gameplay);
	if (auto* GI=Cast<UTowerGameInstance>(GetGameInstance())) GI->ReturnToMainMenu();
}
void ATowerHubController::QuitToDesktop()
{
	SetMenuScreen(ETowerHubScreen::Gameplay);
	UKismetSystemLibrary::QuitGame(this,this,EQuitPreference::Quit,false);
}
void ATowerHubController::SelectTrial(int32 Index)
{
	if (IsTrialMenuOpen()) { if (auto* HubHUD=Cast<ATowerHubHUD>(GetHUD())) HubHUD->SelectTrial(Index); }
	else if (!IsGameplayInputBlocked()) if (auto* Hero=Cast<ATowerCharacter>(GetPawn())) Hero->ServerCast(Index);
}
void ATowerHubController::SelectFirst() { SelectTrial(0); }
void ATowerHubController::SelectSecond() { SelectTrial(1); }
void ATowerHubController::SelectThird() { SelectTrial(2); }
void ATowerHubController::UseUltimate() { if (!IsGameplayInputBlocked()) if (auto* Hero=Cast<ATowerCharacter>(GetPawn())) Hero->ServerCast(3); }

void ATowerHubGameMode::RestartPlayer(AController* NewPlayer)
{
 Super::RestartPlayer(NewPlayer);
 if (auto* Hero=NewPlayer ? Cast<ATowerCharacter>(NewPlayer->GetPawn()) : nullptr)
 {
  const int32 Zone=TowerMaps::GetZone(GetWorld());
  Hero->TrialIndex=Zone >= 0 ? Zone : -1;
  Hero->NextPortalTime=GetWorld()->GetTimeSeconds()+2;
  Hero->ForceNetUpdate();
 }
}
void ATowerHubController::ClientTravelUnavailable_Implementation()
{
 TravelNotice=TEXT("Переход между серверами испытаний ещё не подключён. Сетевой прототип доступен на Mainlvl.");
 TravelNoticeUntil=GetWorld()->GetTimeSeconds()+5;
}
void ATowerHubGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	UE_LOG(LogTemp,Display,TEXT("TOWER_LOGIN Players=%d Player=%s"),GetNumPlayers(),*NewPlayer->GetName());
}
void ATowerHubController::TowerRespawn()
{
#if !UE_BUILD_SHIPPING
	// Host-only diagnostic: no client RPC can request a free reset.
	if (HasAuthority()) { APawn* Old=GetPawn(); UnPossess(); GetWorld()->GetAuthGameMode()->RestartPlayer(this); if (Old) Old->Destroy(); }
#endif
}
void ATowerHubController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController()) if (auto* Settings=UTowerUserSettings::Get()) Settings->ApplyAudio(GetWorld());
#if !UE_BUILD_SHIPPING
	int32 ArtView = -1;
	if (IsLocalController() && FParse::Value(FCommandLine::Get(), TEXT("TowerArtView="), ArtView) && ArtView >= 0 && ArtView <= 3)
	{
		FTimerHandle CameraTimer;
		GetWorldTimerManager().SetTimer(CameraTimer, FTimerDelegate::CreateWeakLambda(this, [this, ArtView]
		{
			const bool bLegacy=TowerMaps::GetZone(GetWorld()) == -2;
			const FVector Centers[] = { TowerHubOrigin(), bLegacy ? TowerTrialLocation(0) : TowerHubOrigin(), bLegacy ? TowerTrialLocation(1) : TowerHubOrigin(), bLegacy ? TowerTrialLocation(2) : TowerHubOrigin() };
			const FVector Offsets[] = { FVector(-1300,-900,340), FVector(-1000,-350,270), FVector(-1700,-1400,1000), FVector(-3600,-3100,1600) };
			const FVector Targets[] = { FVector(650,0,240), FVector(1600,0,160), FVector(100,0,0), FVector(700,500,0) };
			const FVector Position = Centers[ArtView] + Offsets[ArtView];
			const FRotator Rotation = (Centers[ArtView] + Targets[ArtView] - Position).Rotation();
			if (ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(Position, Rotation))
			{
				Camera->GetCameraComponent()->SetFieldOfView(85.f);
				SetViewTarget(Camera);
				if (GetHUD()) GetHUD()->bShowHUD = false;
				UE_LOG(LogTemp, Display, TEXT("TOWER_ART_CAMERA View=%d Position=%s"), ArtView, *Position.ToString());
			}
		}), 2.f, false);
		FTimerHandle CaptureTimer;
		GetWorldTimerManager().SetTimer(CaptureTimer, FTimerDelegate::CreateWeakLambda(this, [ArtView]
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Screenshots/ArtView%d.png"), ArtView), false, false);
		}), 35.f, false);
	}
	if (IsLocalController() && FParse::Param(FCommandLine::Get(),TEXT("TowerScreenshot")))
	{
		FTimerHandle ScreenshotTimer;
		GetWorldTimerManager().SetTimer(ScreenshotTimer,FTimerDelegate::CreateWeakLambda(this,[this]
		{
			int32 HUDPage = 0;
			FParse::Value(FCommandLine::Get(),TEXT("TowerHUDPage="),HUDPage);
			if (HUDPage == 1) SetMenuScreen(ETowerHubScreen::Pause);
			else if (HUDPage == 2) SetMenuScreen(ETowerHubScreen::Settings);
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/HubDungeon.png"),true,false);
		}),20.f,false);
	}
#endif
}
void ATowerHubController::Tick(float Delta)
{
	Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
	// Opt-in integration driver. Exercises the ordinary replicated movement and ability paths.
	if (!IsLocalController() || !FParse::Param(FCommandLine::Get(),TEXT("TowerNetProbe"))) return;
	auto* Hero=Cast<ATowerCharacter>(GetPawn()); auto* PS=GetPlayerState<ATowerPlayerState>();
	if (!Hero || !PS || PS->SkillIds.Num()!=4) return;
	ProbeAge+=Delta;
	if (ProbeAge<1) Hero->AddMovementInput(FVector(1,0,0),1);
	if (ProbeAge>1 && ProbeAge<2) { Hero->SetSprint(true); Hero->AddMovementInput(FVector(1,0,0),1); }
	if (ProbeAge>2 && !bProbeRoll) { Hero->SetSprint(false); Hero->RequestRoll(); bProbeRoll=true; }
	if (ProbeAge>3 && !bProbeCast) { Hero->ServerCast(0); bProbeCast=true; }
	if (ProbeAge>8 && !bProbeLogged)
	{
		int32 VisibleCharacters=0; for (TActorIterator<ATowerCharacter> It(GetWorld());It;++It) ++VisibleCharacters;
		UE_LOG(LogTemp,Display,TEXT("TOWER_NET_PROBE Characters=%d PlayerStates=%d Skills=%d Position=%s"),VisibleCharacters,GetWorld()->GetGameState()->PlayerArray.Num(),PS->SkillIds.Num(),*Hero->GetActorLocation().ToCompactString());
		bProbeLogged=true;
	}
	if (bProbeLogged && !bProbeReturned)
	{
		int32 ProbeTrial = 0;
		FParse::Value(FCommandLine::Get(), TEXT("TowerProbeTrial="), ProbeTrial);
		ProbeTrial = FMath::Clamp(ProbeTrial, 0, 2);
		if (Hero->TrialIndex==ProbeTrial) bProbeVisitedTrial=true;
		if (bProbeVisitedTrial && Hero->TrialIndex==-1)
		{
			bProbeReturned=true;
			UE_LOG(LogTemp,Display,TEXT("TOWER_PORTAL_PROBE ReturnedToHub=1 Skills=%s"),*FString::JoinBy(PS->SkillIds,TEXT(","),[](int32 Id){return FString::FromInt(Id);}));
		}
		else
		{
			const FVector Goal=bProbeVisitedTrial ? TowerTrialLocation(ProbeTrial)+FVector(-950,0,0) : TowerHubOrigin()+FVector(900,(ProbeTrial-1)*800,110);
			Hero->AddMovementInput((Goal-Hero->GetActorLocation()).GetSafeNormal2D(),1);
		}
	}
#endif
}

const UTowerTrialCatalog* ATowerHubHUD::Catalog() const
{
	return TrialCatalog ? TrialCatalog.Get() : GetDefault<UTowerTrialCatalog>();
}
void ATowerHubHUD::SelectTrial(int32 Index)
{
	if (Catalog()->Trials.IsValidIndex(Index)) SelectedTrial = Index;
}
void ATowerHubHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	if (auto* PC=Cast<ATowerHubController>(PlayerOwner))
	{
		if (PC->GetMenuScreen() == ETowerHubScreen::Settings)
		{
			if (BoxName == TEXT("PauseBack")) PC->SetMenuScreen(ETowerHubScreen::Pause);
			else TowerMenuUI::HandleSettingsClick(PC,BoxName);
			return;
		}
		if (PC->GetMenuScreen() == ETowerHubScreen::Pause)
		{
			if (BoxName == TEXT("PauseResume")) PC->SetMenuScreen(ETowerHubScreen::Gameplay);
			else if (BoxName == TEXT("PauseSettings")) PC->SetMenuScreen(ETowerHubScreen::Settings);
			else if (BoxName == TEXT("PauseMain")) PC->ReturnToMainMenu();
			else if (BoxName == TEXT("PauseQuit")) PC->QuitToDesktop();
			return;
		}
		if (!PC->IsTrialMenuOpen()) return;
	}
	if (BoxName == TEXT("Close"))
	{
		if (auto* PC = Cast<ATowerHubController>(PlayerOwner)) PC->ToggleTrials();
	}
	for (int32 Index=0; Index<Catalog()->Trials.Num(); ++Index)
		if (BoxName == FName(*FString::Printf(TEXT("Trial%d"), Index))) SelectTrial(Index);
}

void ATowerHubHUD::DrawHUD()
{
	Super::DrawHUD();
	if (auto* PC=Cast<ATowerHubController>(PlayerOwner); PC && GetWorld()->GetTimeSeconds()<PC->TravelNoticeUntil)
		DrawText(PC->TravelNotice,FLinearColor::Yellow,30,100,nullptr,1.f);
	if (!Canvas || !PlayerOwner) return;
	auto* PC = Cast<ATowerHubController>(PlayerOwner);
	if (!PC) return;
	const float S = FMath::Min(Canvas->SizeX/1100.f, Canvas->SizeY/760.f);
	const float X = (Canvas->SizeX-1000*S)*.5f;
	const float Y = (Canvas->SizeY-660*S)*.5f;
	const FLinearColor Ink(.035f,.05f,.075f,.97f), Accent(.2f,.85f,1.f), Muted(.62f,.7f,.78f);
	auto Rect = [&](FLinearColor Color, float A, float B, float W, float H) { DrawRect(Color,X+A*S,Y+B*S,W*S,H*S); };
	auto Text = [&](const FString& Value, float A, float B, float Size, FLinearColor Color)
	{
		DrawText(Value,Color,X+A*S,Y+B*S,GEngine->GetMediumFont(),Size*S);
	};
	if (PC->GetMenuScreen() == ETowerHubScreen::Settings)
	{
		DrawRect(FLinearColor(.015f,.025f,.04f,.96f),0,0,Canvas->SizeX,Canvas->SizeY);
		TowerMenuUI::DrawSettings(this);
		const FVector2D Position(24*S,Canvas->SizeY-54*S), Size(180*S,34*S);
		DrawRect(Ink,Position.X,Position.Y,Size.X,Size.Y);
		DrawText(TEXT("НАЗАД / ESC"),Accent,Position.X+12*S,Position.Y+7*S,GEngine->GetMediumFont(),.9f*S);
		AddHitBox(Position,Size,TEXT("PauseBack"),true);
		return;
	}
	if (PC->GetMenuScreen() == ETowerHubScreen::Pause)
	{
		DrawRect(FLinearColor(.015f,.025f,.04f,.86f),0,0,Canvas->SizeX,Canvas->SizeY);
		Rect(Ink,290,110,420,440);
		Rect(Accent,290,110,420,2);
		Text(TEXT("ПАУЗА"),326,146,1.8f,FLinearColor::White);
		Text(GetNetMode()==NM_Standalone ? TEXT("Можно передохнуть") : TEXT("Сетевая игра продолжается"),326,190,.85f,Muted);
		const TCHAR* Titles[] = { TEXT("ПРОДОЛЖИТЬ"),TEXT("НАСТРОЙКИ"),TEXT("ГЛАВНОЕ МЕНЮ"),TEXT("ВЫЙТИ ИЗ ИГРЫ") };
		const FName Names[] = { TEXT("PauseResume"),TEXT("PauseSettings"),TEXT("PauseMain"),TEXT("PauseQuit") };
		for (int32 I=0;I<4;++I)
		{
			const float Top=244+I*65;
			Rect(FLinearColor(.075f,.105f,.14f),326,Top,348,48);
			Text(Titles[I],344,Top+15,.95f,I==0 ? Accent : FLinearColor::White);
			AddHitBox(FVector2D(X+326*S,Y+Top*S),FVector2D(348*S,48*S),Names[I],true);
		}
		return;
	}
	if (!PC->IsTrialMenuOpen())
	{
        DrawText(TEXT("ESC  Меню    TAB  Испытания"),Muted,24*S,24*S,GEngine->GetSmallFont(),.9f*S);
        auto* PS=PC->GetPlayerState<ATowerPlayerState>();
        if (PS)
        {
            const auto& Pool=GetDefault<UTowerAbilityPool>()->Skills;
            const float Width=640.f*S, Left=(Canvas->SizeX-Width)*.5f, Bottom=Canvas->SizeY-86*S;
            DrawRect(Ink,Left,Bottom-16*S,Width,4*S);
            DrawRect(Accent,Left,Bottom-16*S,Width*FMath::Clamp(PS->Health()/100.f,0.f,1.f),4*S);
            DrawText(FString::Printf(TEXT("%.0f"),PS->Health()),FLinearColor::White,Left,Bottom-37*S,GEngine->GetSmallFont(),.9f*S);
            for (int32 I=0;I<FMath::Min(4,PS->SkillIds.Num());++I)
            {
                if (!Pool.IsValidIndex(PS->SkillIds[I])) continue;
                const auto& Skill=Pool[PS->SkillIds[I]];
                const float A=Left+I*160*S;
                const float Remaining=PS->ReadyAt.IsValidIndex(I) ? FMath::Max(0.f,PS->ReadyAt[I]-PS->ServerTime()):0;
                DrawRect(Ink,A,Bottom,154*S,62*S);
                DrawRect(Remaining>0 ? Muted : Skill.Color,A,Bottom,154*S,2*S);
                const FString Key=I==3 ? TEXT("R") : FString::FromInt(I+1);
                DrawText(Key,Remaining>0 ? Muted : FLinearColor::White,A+10*S,Bottom+8*S,GEngine->GetMediumFont(),S);
                if (Remaining>0) DrawText(FString::Printf(TEXT("%.1f"),Remaining),Muted,A+103*S,Bottom+9*S,GEngine->GetSmallFont(),S);
                float TextWidth=0,TextHeight=0;
                GetTextSize(Skill.Title,TextWidth,TextHeight,GEngine->GetSmallFont(),1.f);
                const float FontScale=FMath::Min(.9f,134.f/FMath::Max(1.f,TextWidth));
                DrawText(Skill.Title,Remaining>0 ? Muted : FLinearColor::White,A+10*S,Bottom+37*S,GEngine->GetSmallFont(),FontScale*S);
            }
        }
		return;
	}
	DrawRect(FLinearColor(0,0,0,.55f),0,0,Canvas->SizeX,Canvas->SizeY);
	Rect(Ink,0,0,1000,660);
	Rect(Accent,0,0,1000,3);
	Text(TEXT("ONLINE TOWER"),36,30,1.2f,Accent);
	Text(TEXT("ВЫБОР ИСПЫТАНИЯ"),36,76,2.f,FLinearColor::White);
	Text(TEXT("Этаж 01 / Прототип хаба"),36,123,1.f,Muted);
	Rect(FLinearColor(.1f,.15f,.2f),840,30,124,36);
	Text(TEXT("ЗАКРЫТЬ / E"),850,39,.85f,FLinearColor::White);
	AddHitBox(FVector2D(X+840*S,Y+30*S),FVector2D(124*S,36*S),TEXT("Close"),true);
	const auto& Trials = Catalog()->Trials;
	const float CardWidth = Trials.Num() ? 928.f/Trials.Num() : 928.f;
	for (int32 Index=0; Index<Trials.Num(); ++Index)
	{
		const float Left = 36+Index*CardWidth;
		const bool bSelected = Index == SelectedTrial;
		Rect(bSelected ? FLinearColor(.08f,.23f,.3f) : FLinearColor(.07f,.1f,.14f),Left,185,CardWidth-12,175);
		if (bSelected) Rect(Accent,Left,185,CardWidth-12,3);
		Text(FString::Printf(TEXT("0%d"),Index+1),Left+20,205,1.f,Accent);
		Text(Trials[Index].Title.ToString(),Left+20,245,1.25f,FLinearColor::White);
		Text(FString::Printf(TEXT("Игроков: %d"),Trials[Index].MaxPlayers),Left+20,289,1.f,Muted);
		Text(bSelected ? TEXT("ВЫБРАНО") : TEXT("ВЫБРАТЬ"),Left+20,327,.8f,bSelected ? Accent : Muted);
		AddHitBox(FVector2D(X+Left*S,Y+185*S),FVector2D((CardWidth-12)*S,175*S),*FString::Printf(TEXT("Trial%d"),Index),true);
	}
	if (Trials.IsValidIndex(SelectedTrial))
	{
		Text(Trials[SelectedTrial].Description.ToString(),36,397,1.f,FLinearColor::White);
		Text(TEXT("Сейчас портал ведёт на тренировочную площадку. Награды не выдаются."),36,440,.9f,Muted);
	}
	Rect(FLinearColor(.12f,.15f,.18f),36,506,928,62);
	Text(TEXT("ЗАКРОЙ МЕНЮ И ВОЙДИ В СВЕТЯЩИЙСЯ ПОРТАЛ"),58,526,1.f,Accent);
	Text(TEXT("Выбор: мышь или 1 / 2 / 3     Закрыть: E / Tab / Esc"),36,607,.9f,Accent);
}
