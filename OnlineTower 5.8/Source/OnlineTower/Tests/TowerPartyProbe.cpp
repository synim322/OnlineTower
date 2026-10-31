#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "TowerAccountSubsystem.h"
#include "TowerPartyComponent.h"
#include "TowerAbilities.h"
#include "TowerCharacter.h"
#include "TowerWorld.h"
#include "TowerMaps.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace
{
// Explicit development-only real-network integration driver; no diagnostic RPCs.
struct FTowerPartyProbe
{
	bool bAuthStarted=false, bInvited=false, bAccepted=false, bClientPassed=false;
	bool bFirstAtPortal=false, bBothAtPortal=false, bServerPassed=false;
	double FirstAt=0;
	TMap<FString,TArray<int32>> Skills;
	FGuid OriginalParty;
	FTowerPartyProbe() { FWorldDelegates::OnWorldPostActorTick.AddRaw(this,&FTowerPartyProbe::Tick); }
	~FTowerPartyProbe() { FWorldDelegates::OnWorldPostActorTick.RemoveAll(this); }
	void Tick(UWorld* World,ELevelTick,float)
	{
		if (!FParse::Param(FCommandLine::Get(),TEXT("TowerPartyProbe")) || !World || World->WorldType!=EWorldType::Game) return;
		auto* GS=World->GetGameState();
		if (!GS) return;
		if (World->GetNetMode()==NM_Client)
		{
			auto* PC=World->GetFirstPlayerController();
			auto* Account=World->GetGameInstance()->GetSubsystem<UTowerAccountSubsystem>();
			auto* Party=PC ? PC->FindComponentByClass<UTowerPartyComponent>() : nullptr;
			if (!Account || !Party) return;
			if (!bAuthStarted)
			{
				FString User;
				if (!FParse::Value(FCommandLine::Get(),TEXT("TowerProbeUser="),User) || !User.StartsWith(TEXT("probe_"))) return;
				bAuthStarted=true; Account->Register(User,FGuid::NewGuid().ToString()+TEXT("Aa_9"));
			}
			if (!Party->IsAuthenticated()) return;
			auto* Self=PC->GetPlayerState<ATowerPlayerState>();
			if (!Self) return;
			if (!bInvited && Self->AccountUsername.EndsWith(TEXT("_a")))
			{
				for (APlayerState* PS:GS->PlayerArray) if (auto* Other=Cast<ATowerPlayerState>(PS); Other && Other!=Self && !Other->AccountId.IsEmpty())
				{ Party->Invite(Other->GetPlayerId()); bInvited=true; break; }
			}
			if (!bAccepted && !Party->GetInvites().IsEmpty())
			{ Party->RespondInvite(Party->GetInvites()[0].InviterPlayerId,true); bAccepted=true; }
			if (!bClientPassed && TowerMaps::GetZone(World)==0 && Party->GetPartyMembers().Num()==2 && Self->SkillIds.Num()==4)
			{
				bClientPassed=true;
				UE_LOG(LogTemp,Display,TEXT("TOWER_PARTY_PROBE CLIENT_PASS Auth=1 Members=2 Trial=0 Skills=4"));
			}
			return;
		}
		if (World->GetNetMode()!=NM_DedicatedServer || GS->PlayerArray.Num()!=2) return;
		TArray<ATowerCharacter*> Heroes;
		for (APlayerState* PS:GS->PlayerArray)
		{
			auto* Player=Cast<ATowerPlayerState>(PS);
			auto* Hero=Player ? Cast<ATowerCharacter>(Player->GetPawn()) : nullptr;
			if (!Player || Player->AccountId.IsEmpty() || !Player->PartyId.IsValid() || !Hero || Player->SkillIds.Num()!=4) return;
			Heroes.Add(Hero);
		}
		auto* First=Heroes[0]->GetPlayerState<ATowerPlayerState>();
		auto* Second=Heroes[1]->GetPlayerState<ATowerPlayerState>();
		if (First->PartyId!=Second->PartyId) return;
		if (TowerMaps::GetZone(World)==-1)
		{
			ATowerPortal* Portal=nullptr;
			for (TActorIterator<ATowerPortal> It(World);It;++It) if (It->DestinationTrial==0) { Portal=*It; break; }
			if (!Portal) return;
			if (!bFirstAtPortal)
			{
				for (auto* Hero:Heroes) { auto* PS=Hero->GetPlayerState<ATowerPlayerState>(); Skills.Add(PS->AccountId,PS->SkillIds); }
				OriginalParty=First->PartyId;
				Heroes[0]->NextPortalTime=0;
				Heroes[0]->TeleportTo(Portal->GetActorLocation()+FVector(0,-65,0),FRotator::ZeroRotator,false,true);
				bFirstAtPortal=true; FirstAt=FPlatformTime::Seconds();
			}
			else if (!bBothAtPortal && FPlatformTime::Seconds()-FirstAt>4)
			{
				UE_LOG(LogTemp,Display,TEXT("TOWER_PARTY_PROBE WAIT_PASS FirstPlayerHeldInHub=1"));
				bBothAtPortal=true;
				Heroes[1]->NextPortalTime=0;
				Heroes[1]->TeleportTo(Portal->GetActorLocation()+FVector(0,65,0),FRotator::ZeroRotator,false,true);
			}
		}
		else if (TowerMaps::GetZone(World)==0 && !bServerPassed)
		{
			bool bValid=bBothAtPortal && OriginalParty==First->PartyId;
			for (auto* Hero:Heroes) { auto* PS=Hero->GetPlayerState<ATowerPlayerState>(); const auto* Old=Skills.Find(PS->AccountId); bValid &= Old && *Old==PS->SkillIds; }
			bServerPassed=true;
			UE_LOG(LogTemp,Display,TEXT("TOWER_PARTY_PROBE SERVER_%s Members=2 IdentityPartyLoadoutPreserved=%d"),bValid?TEXT("PASS"):TEXT("FAIL"),bValid?1:0);
		}
	}
} GPartyProbe;
}
#endif
