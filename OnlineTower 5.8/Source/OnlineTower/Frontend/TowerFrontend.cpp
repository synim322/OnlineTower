#include "TowerFrontend.h"
#include "TowerMaps.h"
#include "TowerAbilities.h"
#include "AudioDevice.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	FString ProfileSlot()
	{
		FString Slot = TEXT("TowerCharacters_v1");
#if !UE_BUILD_SHIPPING
		FString Override;
		if (FParse::Value(FCommandLine::Get(), TEXT("TowerProfileSlot="), Override) && !Override.IsEmpty() && !Override.Contains(TEXT("/")) && !Override.Contains(TEXT("\\"))) Slot = Override;
#endif
		return Slot;
	}
	const FLinearColor Ink(.025f,.035f,.05f,1), Panel(.055f,.075f,.10f,1), Accent(.35f,.82f,.9f,1), Muted(.57f,.64f,.69f,1);
	struct FMenuDraw
	{
		AHUD* HUD;
		float S, X, Y;
		FMenuDraw(AHUD* In) : HUD(In)
		{
			int32 W=1280,H=720; if (HUD->PlayerOwner) HUD->PlayerOwner->GetViewportSize(W,H);
			S=FMath::Max(.1f,FMath::Min(W/1100.f,H/760.f)); X=(W-1000*S)*.5f; Y=(H-660*S)*.5f;
		}
		void Rect(FLinearColor Color,float A,float B,float W,float H) { HUD->DrawRect(Color,X+A*S,Y+B*S,W*S,H*S); }
		void Text(const FString& Value,float A,float B,float Size=1,FLinearColor Color=FLinearColor::White) { HUD->DrawText(Value,Color,X+A*S,Y+B*S,GEngine->GetMediumFont(),Size*S); }
		void Button(FName Id,const FString& Value,float A,float B,float W=400,bool Enabled=true)
		{
			float MX=0,MY=0; const bool Hover=HUD->PlayerOwner && HUD->PlayerOwner->GetMousePosition(MX,MY) && MX>=X+A*S && MX<=X+(A+W)*S && MY>=Y+B*S && MY<=Y+(B+48)*S;
			Rect(Hover && Enabled ? FLinearColor(.10f,.20f,.24f,1) : Panel,A,B,W,48);
			if(Hover && Enabled) Rect(Accent,A,B,3,48);
			Text(Value,A+18,B+15,.95f,Enabled ? FLinearColor::White : Muted);
			if(Enabled) HUD->AddHitBox(FVector2D(X+A*S,Y+B*S),FVector2D(W*S,48*S),Id,true);
		}
	};
}

bool UTowerGameInstance::ResolveSkills(const FTowerCharacterProfile& Profile,TArray<int32>& Out)
{
	Out.Reset();
	const auto& Pool=GetDefault<UTowerAbilityPool>()->Skills;
	if (!Profile.Id.IsValid() || Profile.Name.TrimStartAndEnd().IsEmpty() || Profile.Direction!=TEXT("MeleeDamage") || Profile.Skills.Num()!=4 || Pool.Num()<4) return false;
	TSet<FName> Unique;
	for(int32 I=0;I<4;++I)
	{
		const int32 Index=Pool.IndexOfByPredicate([&](const FTowerSkill& Skill){return Skill.Id==Profile.Skills[I];});
		if(Index==INDEX_NONE || Unique.Contains(Profile.Skills[I]) || (I<3 && Index==Pool.Num()-1) || (I==3 && Index!=Pool.Num()-1)) { Out.Reset(); return false; }
		Unique.Add(Profile.Skills[I]); Out.Add(Index);
	}
	return true;
}

void UTowerGameInstance::Init()
{
	Super::Init();
	if(UGameplayStatics::DoesSaveGameExist(ProfileSlot(),0))
	{
		Profiles=Cast<UTowerProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(ProfileSlot(),0));
		bool Valid=Profiles && Profiles->Version==1 && Profiles->Characters.Num()<=8;
		TSet<FGuid> Ids;
		if(Valid) for(const auto& Profile:Profiles->Characters)
		{
			TArray<int32> Resolved;
			if(!ResolveSkills(Profile,Resolved) || Ids.Contains(Profile.Id)) { Valid=false; break; }
			Ids.Add(Profile.Id);
		}
		if(!Valid)
		{
			Profiles=nullptr; bProfileWritable=false;
			ProfileError=TEXT("Не удалось прочитать персонажей. Исходное сохранение не изменено.");
		}
	}
	if(!Profiles) Profiles=NewObject<UTowerProfileSaveGame>(this);
}
void UTowerGameInstance::OnWorldChanged(UWorld* OldWorld,UWorld* NewWorld)
{
	Super::OnWorldChanged(OldWorld,NewWorld);
	if(auto* Settings=UTowerUserSettings::Get()) Settings->ApplyAudio(NewWorld);
}
const FTowerCharacterProfile* UTowerGameInstance::GetSelectedCharacter() const
{
	return Profiles ? Profiles->Characters.FindByPredicate([this](const FTowerCharacterProfile& P){return P.Id==Profiles->SelectedId;}) : nullptr;
}
const TArray<FTowerCharacterProfile>& UTowerGameInstance::GetCharacters() const
{
	static const TArray<FTowerCharacterProfile> Empty;
	return Profiles ? Profiles->Characters : Empty;
}
bool UTowerGameInstance::CreateCharacter(const FString& Name,FName Direction,FString& Error)
{
	Error.Empty();
	if(!bProfileWritable) { Error=ProfileError; return false; }
	if(!GetWorld() || GetWorld()->GetNetMode()!=NM_Standalone) { Error=TEXT("Создание доступно в главном меню."); return false; }
	if(!Profiles || Profiles->Characters.Num()>=8) { Error=TEXT("Достигнут предел: 8 персонажей."); return false; }
	if(Direction!=TEXT("MeleeDamage") || Name.TrimStartAndEnd().IsEmpty() || Name.Len()>32) { Error=TEXT("Некорректное имя или направление."); return false; }
	FTowerCharacterProfile Profile; Profile.Id=FGuid::NewGuid(); Profile.Name=Name.TrimStartAndEnd(); Profile.Direction=Direction;
	const auto* Pool=GetDefault<UTowerAbilityPool>(); FRandomStream Random(FMath::Rand());
	if(Pool->Skills.Num()<4) { Error=TEXT("Пул способностей недоступен."); return false; }
	for(int32 Index:Pool->Roll(Random)) Profile.Skills.Add(Pool->Skills[Index].Id);
	Profile.Skills.Add(Pool->Skills.Last().Id);
	TArray<int32> Resolved; if(!ResolveSkills(Profile,Resolved)) { Error=TEXT("Пул способностей недоступен."); return false; }
	const FGuid Previous=Profiles->SelectedId;
	Profiles->Characters.Add(Profile); Profiles->SelectedId=Profile.Id;
	if(!UGameplayStatics::SaveGameToSlot(Profiles,ProfileSlot(),0))
	{
		Profiles->Characters.Pop(); Profiles->SelectedId=Previous; Error=TEXT("Не удалось сохранить персонажа. Проверьте доступ к диску."); return false;
	}
	UE_LOG(LogTemp,Display,TEXT("TOWER_PROFILE_CREATED Id=%s Skills=%d"),*Profile.Id.ToString(),Profile.Skills.Num());
	return true;
}
bool UTowerGameInstance::SelectCharacter(FGuid Id)
{
	if(!Profiles || !bProfileWritable || !Profiles->Characters.ContainsByPredicate([&](const FTowerCharacterProfile& P){return P.Id==Id;})) return false;
	const FGuid Previous=Profiles->SelectedId; Profiles->SelectedId=Id;
	if(!UGameplayStatics::SaveGameToSlot(Profiles,ProfileSlot(),0)) { Profiles->SelectedId=Previous; ProfileError=TEXT("Не удалось сохранить выбор персонажа."); return false; }
	return true;
}
void UTowerGameInstance::EnterHub()
{
	if(HasSelectedCharacter()) UGameplayStatics::OpenLevel(this,FName(TowerMaps::Hub),true);
}
void UTowerGameInstance::ReturnToMainMenu()
{
	if(auto* PC=GetFirstLocalPlayerController()) PC->SetPause(false);
	UGameplayStatics::OpenLevel(this,FName(TowerMaps::Menu),true);
}

UTowerUserSettings* UTowerUserSettings::Get() { return GEngine ? Cast<UTowerUserSettings>(GEngine->GetGameUserSettings()) : nullptr; }
void UTowerUserSettings::ApplyAudio(UWorld* World) const
{
	if(World) if(FAudioDeviceHandle Audio=World->GetAudioDevice()) Audio->SetTransientPrimaryVolume(FMath::Clamp(MasterVolume,0.f,1.f));
}
void TowerMenuUI::DrawSettings(AHUD* HUD)
{
	if(!HUD || !HUD->PlayerOwner) return;
	FMenuDraw D(HUD); auto* Settings=UTowerUserSettings::Get();
	D.Text(TEXT("НАСТРОЙКИ"),60,65,2);
	if(!Settings) { D.Text(TEXT("Настройки недоступны"),60,140); return; }
	D.Text(TEXT("ЗВУК"),60,135,.85f,Accent);
	D.Text(FString::Printf(TEXT("Общая громкость   %d%%"),FMath::RoundToInt(Settings->MasterVolume*100)),60,177);
	D.Button(TEXT("SettingsVolumeDown"),TEXT("−"),660,160,80); D.Button(TEXT("SettingsVolumeUp"),TEXT("+"),755,160,80);
	D.Text(TEXT("ИЗОБРАЖЕНИЕ"),60,235,.85f,Accent);
	const TCHAR* Qualities[]={TEXT("Низкое"),TEXT("Среднее"),TEXT("Высокое"),TEXT("Эпическое"),TEXT("Кино")};
	const int32 Quality=Settings->GetOverallScalabilityLevel();
	D.Button(TEXT("SettingsQuality"),FString(TEXT("Качество графики: "))+(Quality>=0 && Quality<5 ? Qualities[Quality] : TEXT("Своё")),60,270,775);
	const FIntPoint Size=Settings->GetScreenResolution();
	D.Button(TEXT("SettingsResolution"),FString::Printf(TEXT("Разрешение окна: %d × %d"),Size.X,Size.Y),60,332,775);
	D.Button(TEXT("SettingsWindow"),Settings->GetFullscreenMode()==EWindowMode::Windowed ? TEXT("Режим: в окне") : TEXT("Режим: без рамки (размер рабочего стола)"),60,394,775);
	D.Text(TEXT("Нажмите на строку, чтобы сменить значение."),60,465,.85f,Muted);
	D.Button(TEXT("SettingsApply"),TEXT("Применить и сохранить"),60,510,775);
}
bool TowerMenuUI::HandleSettingsClick(APlayerController* PC,FName BoxName)
{
	auto* Settings=UTowerUserSettings::Get(); if(!Settings || !PC) return false;
	if(BoxName==TEXT("SettingsVolumeDown")) Settings->MasterVolume=FMath::Clamp(Settings->MasterVolume-.1f,0.f,1.f);
	else if(BoxName==TEXT("SettingsVolumeUp")) Settings->MasterVolume=FMath::Clamp(Settings->MasterVolume+.1f,0.f,1.f);
	else if(BoxName==TEXT("SettingsQuality")) Settings->SetOverallScalabilityLevel((Settings->GetOverallScalabilityLevel()+1)%4);
	else if(BoxName==TEXT("SettingsWindow")) Settings->SetFullscreenMode(Settings->GetFullscreenMode()==EWindowMode::Windowed ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
	else if(BoxName==TEXT("SettingsResolution"))
	{
		const FIntPoint Choices[]={FIntPoint(1280,720),FIntPoint(1600,900),FIntPoint(1920,1080),FIntPoint(2560,1440)};
		int32 Index=0; for(int32 I=0;I<4;++I) if(Choices[I]==Settings->GetScreenResolution()) Index=(I+1)%4;
		Settings->SetScreenResolution(Choices[Index]); Settings->SetFullscreenMode(EWindowMode::Windowed);
	}
	else if(BoxName==TEXT("SettingsApply"))
	{
		// Only windowed and borderless modes are exposed: no exclusive display-mode switch.
		if(Settings->GetFullscreenMode()==EWindowMode::Fullscreen) Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
		Settings->ApplySettings(false); Settings->ConfirmVideoMode(); Settings->SaveSettings();
		UE_LOG(LogTemp,Display,TEXT("TOWER_SETTINGS_APPLIED Resolution=%s Volume=%.2f"),*Settings->GetScreenResolution().ToString(),Settings->MasterVolume);
	}
	else return false;
	Settings->ApplyAudio(PC->GetWorld()); return true;
}

ATowerFrontendGameMode::ATowerFrontendGameMode()
{
	DefaultPawnClass=nullptr; PlayerControllerClass=ATowerFrontendController::StaticClass(); HUDClass=ATowerFrontendHUD::StaticClass();
}
void ATowerFrontendController::BeginPlay()
{
	Super::BeginPlay(); if(!IsLocalController()) return;
	bShowMouseCursor=true; bEnableClickEvents=true; bEnableMouseOverEvents=true;
	FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); SetInputMode(Mode);
	if(auto* Settings=UTowerUserSettings::Get()) Settings->ApplyAudio(GetWorld());
	#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(),TEXT("TowerMenuPage="),Page); Page=FMath::Clamp(Page,0,3);
	if(FParse::Param(FCommandLine::Get(),TEXT("TowerMenuScreenshot")))
	{
		FTimerHandle Capture;
		GetWorldTimerManager().SetTimer(Capture,FTimerDelegate::CreateWeakLambda(this,[]
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/MainMenu.png"),true,false);
		}),5.f,false);
	}
	#endif
	UE_LOG(LogTemp,Display,TEXT("TOWER_FRONTEND_READY"));
}
void ATowerFrontendController::SetupInputComponent()
{
	Super::SetupInputComponent(); InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&ATowerFrontendController::Back);
}
void ATowerFrontendController::Back() { Page=Page==2 ? 1 : 0; Message.Empty(); }
void ATowerFrontendHUD::DrawHUD()
{
	Super::DrawHUD(); if(!Canvas) return;
	auto* PC=Cast<ATowerFrontendController>(PlayerOwner); auto* GI=GetGameInstance<UTowerGameInstance>(); if(!PC || !GI) return;
	DrawRect(Ink,0,0,Canvas->SizeX,Canvas->SizeY); FMenuDraw D(this);
	D.Rect(Accent,0,0,3,660); D.Text(TEXT("ONLINE TOWER"),60,15,1,Accent);
	if(PC->Page==3) TowerMenuUI::DrawSettings(this);
	else if(PC->Page==0)
	{
		D.Text(TEXT("ТВОЙ ПУТЬ"),60,105,3); D.Text(TEXT("НАЧИНАЕТСЯ ЗДЕСЬ"),60,165,2);
		D.Text(TEXT("Этаж 01"),60,235,1,Muted);
		D.Button(TEXT("Play"),TEXT("Играть"),60,320);
		D.Button(TEXT("Settings"),TEXT("Настройки"),60,382);
		D.Button(TEXT("Quit"),TEXT("Выход из игры"),60,444);
		D.Rect(Panel,620,110,320,410); D.Text(TEXT("01"),660,170,6,Accent);
		D.Text(TEXT("ПЕРВЫЙ ЭТАЖ"),660,330,1); D.Text(TEXT("Хаб • Испытания"),660,368,.9f,Muted);
	}
	else if(PC->Page==1)
	{
		D.Text(TEXT("ВЫБОР ПЕРСОНАЖА"),60,70,2);
		const auto& Profiles=GI->GetCharacters(); const auto* Selected=GI->GetSelectedCharacter();
		for(int32 I=0;I<Profiles.Num();++I)
			D.Button(*FString::Printf(TEXT("Character%d"),I),(Selected && Selected->Id==Profiles[I].Id ? TEXT("●  ") : TEXT("○  "))+Profiles[I].Name,60,135+I*51,380);
		if(Selected)
		{
			D.Text(Selected->Name,490,145,1.4f); D.Text(TEXT("ДД ближнего боя"),490,190,1,Accent);
			const auto& Pool=GetDefault<UTowerAbilityPool>()->Skills;
			for(int32 I=0;I<Selected->Skills.Num();++I)
			{
				const auto* Skill=Pool.FindByPredicate([&](const FTowerSkill& S){return S.Id==Selected->Skills[I];});
				if(Skill) D.Text((I==3 ? TEXT("R   ") : FString::FromInt(I+1)+TEXT("   "))+Skill->Title,490,260+I*40,.95f,I==3 ? Accent : FLinearColor::White);
			}
			D.Button(TEXT("EnterHub"),TEXT("Войти в хаб"),490,470,400);
		}
		D.Button(TEXT("CreatePage"),TEXT("+ Создать персонажа"),490,532,400,Profiles.Num()<8);
	}
	else if(PC->Page==2)
	{
		D.Text(TEXT("НОВЫЙ ПЕРСОНАЖ"),60,70,2);
		D.Text(FString::Printf(TEXT("Имя: Странник %02d"),GI->GetCharacters().Num()+1),60,145,1.2f);
		D.Rect(Panel,60,205,775,130); D.Text(TEXT("●  ДД БЛИЖНЕГО БОЯ"),85,230,1.2f,Accent);
		D.Text(TEXT("Ближняя дистанция, мощные удары и контроль пространства."),85,285,.9f,Muted);
		D.Text(TEXT("Три случайных умения выдаются один раз при создании."),60,370,1);
		D.Text(TEXT("Ульта прототипа: Раскол земли."),60,410,1,Muted);
		D.Button(TEXT("Create"),TEXT("Создать персонажа"),60,490,775,GI->GetProfileError().IsEmpty());
	}
	if(PC->Page!=0) D.Button(TEXT("Back"),TEXT("Назад / Esc"),60,594,300);
	const FString Error=!PC->Message.IsEmpty() ? PC->Message : GI->GetProfileError();
	if(!Error.IsEmpty()) D.Text(Error,60,565,.8f,FLinearColor(1,.5f,.4f));
}
void ATowerFrontendHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	auto* PC=Cast<ATowerFrontendController>(PlayerOwner); auto* GI=GetGameInstance<UTowerGameInstance>(); if(!PC || !GI) return;
	if(PC->Page==3 && TowerMenuUI::HandleSettingsClick(PC,BoxName)) return;
	PC->Message.Empty();
	if(BoxName==TEXT("Play")) PC->Page=GI->GetCharacters().IsEmpty() ? 2 : 1;
	else if(BoxName==TEXT("Settings")) PC->Page=3;
	else if(BoxName==TEXT("Back")) PC->Back();
	else if(BoxName==TEXT("CreatePage")) PC->Page=2;
	else if(BoxName==TEXT("Create"))
	{
		if(GI->CreateCharacter(FString::Printf(TEXT("Странник %02d"),GI->GetCharacters().Num()+1),TEXT("MeleeDamage"),PC->Message)) PC->Page=1;
	}
	else if(BoxName==TEXT("EnterHub")) GI->EnterHub();
	else if(BoxName==TEXT("Quit")) UKismetSystemLibrary::QuitGame(this,PC,EQuitPreference::Quit,false);
	else for(int32 I=0;I<GI->GetCharacters().Num();++I)
		if(BoxName==FName(*FString::Printf(TEXT("Character%d"),I))) if(!GI->SelectCharacter(GI->GetCharacters()[I].Id)) PC->Message=GI->GetProfileError();
}
