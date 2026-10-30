#include "TowerFrontend.h"

#if !UE_BUILD_SHIPPING
#include "TowerAbilities.h"
#include "TowerCharacter.h"
#include "TowerHub.h"
#include "TowerMaps.h"
#include "TowerWorld.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
// Opt-in standalone integration check. Use a fresh TowerMenuProbe_* slot each run.
// No existing profile is overwritten and the diagnostic save is retained for inspection.
struct FTowerMenuProbe
{
    enum class EStage { WaitingForMenu, WaitingForHub, WaitingForTrial, WaitingForHubReturn, WaitingForReturn, Finished };
    EStage Stage = EStage::WaitingForMenu;
    bool bInitialized = false;
    bool bEnabled = false;
    bool bMapLoop = false;
    int32 NextTrial = 0;
    double StartedAt = 0;
    FString Slot;
    FGuid CharacterId;
    TArray<FName> Skills;
    TArray<int32> ResolvedSkills;

    FTowerMenuProbe()
    {
        FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FTowerMenuProbe::Tick);
    }

    ~FTowerMenuProbe()
    {
        FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);
    }

    void Finish(bool bPassed, const TCHAR* Reason)
    {
        Stage = EStage::Finished;
        if (bPassed)
        {
            UE_LOG(LogTemp, Display, TEXT("TOWER_MENU_PROBE PASS %s Slot=%s"), Reason, *Slot);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("TOWER_MENU_PROBE FAIL %s Slot=%s"), Reason, *Slot);
        }
        FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
    }

    void Tick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
    {
        if (!bInitialized)
        {
            bInitialized = true;
            bMapLoop = FParse::Param(FCommandLine::Get(), TEXT("TowerMapProbe"));
            bEnabled = bMapLoop || FParse::Param(FCommandLine::Get(), TEXT("TowerMenuProbe"));
            StartedAt = FPlatformTime::Seconds();
            if (bEnabled && (!FParse::Value(FCommandLine::Get(), TEXT("TowerProfileSlot="), Slot)
                || !Slot.StartsWith(TEXT("TowerMenuProbe_")) || Slot.Contains(TEXT("/")) || Slot.Contains(TEXT("\\"))))
            {
                Finish(false, TEXT("Requires an isolated -TowerProfileSlot=TowerMenuProbe_<unique>"));
                return;
            }
        }
        if (!bEnabled || Stage == EStage::Finished) return;
        if (FPlatformTime::Seconds() - StartedAt > 120.0)
        {
            Finish(false, TEXT("Timed out waiting for frontend/hub travel"));
            return;
        }
        if (!World || World->WorldType != EWorldType::Game || World->GetNetMode() != NM_Standalone) return;
        auto* Instance = Cast<UTowerGameInstance>(World->GetGameInstance());
        auto* Controller = World->GetFirstPlayerController();
        if (!Instance || !Controller || !Controller->GetHUD()) return;

        if (Stage == EStage::WaitingForMenu)
        {
            if (!Cast<ATowerFrontendController>(Controller)) return;
            if (UGameplayStatics::DoesSaveGameExist(Slot, 0) || !Instance->GetCharacters().IsEmpty())
            {
                Finish(false, TEXT("Diagnostic slot must be fresh; existing saves were not changed"));
                return;
            }
            FString Error;
            if (!Instance->CreateCharacter(TEXT("Menu Probe"), TEXT("MeleeDamage"), Error))
            {
                UE_LOG(LogTemp, Error, TEXT("TOWER_MENU_PROBE CreateCharacter: %s"), *Error);
                Finish(false, TEXT("Character creation failed"));
                return;
            }
            const FTowerCharacterProfile* Profile = Instance->GetSelectedCharacter();
            if (!Profile || !UTowerGameInstance::ResolveSkills(*Profile, ResolvedSkills) || ResolvedSkills.Num() != 4)
            {
                Finish(false, TEXT("Created profile does not resolve to four abilities"));
                return;
            }
            CharacterId = Profile->Id;
            Skills = Profile->Skills;
            if (!Instance->SelectCharacter(CharacterId))
            {
                Finish(false, TEXT("Selecting the created character failed"));
                return;
            }
            Stage = EStage::WaitingForHub;
            Instance->EnterHub();
            return;
        }

        if (Stage == EStage::WaitingForHub || Stage == EStage::WaitingForHubReturn)
        {
            if (bMapLoop && TowerMaps::GetZone(World) != -1) return;
            auto* Hub = Cast<ATowerHubController>(Controller);
            if (!Hub || !Cast<ATowerCharacter>(Hub->GetPawn())) return;
            auto* Player = Hub->GetPlayerState<ATowerPlayerState>();
            if (!Player || Player->SkillIds.IsEmpty()) return;
            if (Player->SkillIds != ResolvedSkills)
            {
                Finish(false, TEXT("Possessed character loadout differs from saved profile"));
                return;
            }
            Hub->TogglePauseMenu();
            if (Hub->GetMenuScreen() != ETowerHubScreen::Pause || !Hub->IsGameplayInputBlocked() || !Hub->bShowMouseCursor)
            {
                Finish(false, TEXT("Pause did not block gameplay and reveal cursor"));
                return;
            }
            Hub->SetMenuScreen(ETowerHubScreen::Settings);
            if (!Hub->IsGameplayInputBlocked())
            {
                Finish(false, TEXT("Settings did not retain gameplay input block"));
                return;
            }
            Hub->SetMenuScreen(ETowerHubScreen::Gameplay);
            if (Hub->IsGameplayInputBlocked() || Hub->bShowMouseCursor || UGameplayStatics::IsGamePaused(World))
            {
                Finish(false, TEXT("Resume did not restore gameplay"));
                return;
            }
            if (bMapLoop && NextTrial < 3)
            {
                if (TowerMaps::GetZone(World) != -1) { Finish(false,TEXT("Expected separate hub map")); return; }
                if (World->GetTimeSeconds()<3) return;
                auto* Hero=Cast<ATowerCharacter>(Hub->GetPawn());
                Stage=EStage::WaitingForTrial;
                Hero->NextPortalTime=0;
                Hero->SetActorLocation(TowerHubOrigin()+FVector(900,(NextTrial-1)*800,120),false,nullptr,ETeleportType::TeleportPhysics);
                return;
            }
            Hub->TogglePauseMenu();
            Stage = EStage::WaitingForReturn;
            Hub->ReturnToMainMenu();
            return;
        }

        if (Stage == EStage::WaitingForTrial)
        {
            if (TowerMaps::GetZone(World)!=NextTrial || World->GetTimeSeconds()<3) return;
            auto* Hero=Cast<ATowerCharacter>(Controller->GetPawn());
            auto* Player=Controller->GetPlayerState<ATowerPlayerState>();
            if (!Hero || !Player || Player->SkillIds.IsEmpty()) return;
            const FTowerCharacterProfile* Profile=Instance->GetSelectedCharacter();
            if (Hero->TrialIndex!=NextTrial || Player->SkillIds!=ResolvedSkills || !Profile || Profile->Id!=CharacterId || Profile->Skills!=Skills)
            { Finish(false,TEXT("Trial travel changed character/loadout or zone")); return; }
            int32 PortalCount=0;
            for (TActorIterator<ATowerPortal> It(World);It;++It) ++PortalCount;
            if (PortalCount!=1) { Finish(false,TEXT("Trial must contain exactly one return portal")); return; }
            UE_LOG(LogTemp,Display,TEXT("TOWER_MAP_PROBE Trial=%d LoadoutPreserved=1 ReturnPortal=1"),NextTrial);
            ++NextTrial;
            Stage=EStage::WaitingForHubReturn;
            Hero->NextPortalTime=0;
            Hero->SetActorLocation(TowerHubOrigin()+FVector(-950,0,120),false,nullptr,ETeleportType::TeleportPhysics);
            return;
        }

        if (Stage == EStage::WaitingForReturn)
        {
            if (!Cast<ATowerFrontendController>(Controller)) return;
            const FTowerCharacterProfile* Profile = Instance->GetSelectedCharacter();
            auto* Saved = Cast<UTowerProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
            const FTowerCharacterProfile* SavedProfile = Saved ? Saved->Characters.FindByPredicate(
                [this](const FTowerCharacterProfile& Candidate) { return Candidate.Id == CharacterId; }) : nullptr;
            if (!Profile || Profile->Id != CharacterId || Profile->Skills != Skills || !SavedProfile
                || SavedProfile->Skills != Skills || Saved->SelectedId != CharacterId)
            {
                Finish(false, TEXT("Roundtrip or disk save did not preserve selected character and abilities"));
                return;
            }
            if (bMapLoop && NextTrial!=3) { Finish(false,TEXT("Not all trials visited")); return; }
            Finish(true, bMapLoop ? TEXT("Menu -> hub -> all three trial portals -> hub -> menu; profile and loadout preserved") : TEXT("Create/select -> hub possession/loadout -> pause/settings/resume -> menu -> disk save verified"));
        }
    }
};

FTowerMenuProbe TowerMenuProbe;
}
#endif
