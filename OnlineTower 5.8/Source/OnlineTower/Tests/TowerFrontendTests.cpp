#include "TowerFrontend.h"
#include "TowerAbilities.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTowerProfileRoundtripTest,"OnlineTower.Frontend.ProfileRoundtrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FTowerProfileRoundtripTest::RunTest(const FString& Parameters)
{
	auto* Save=NewObject<UTowerProfileSaveGame>();
	FTowerCharacterProfile P; P.Id=FGuid::NewGuid(); P.Name=TEXT("Тестовый персонаж");
	P.Skills={TEXT("Cleave"),TEXT("Thrust"),TEXT("Whirlwind"),TEXT("Earthbreaker")};
	Save->Characters.Add(P); Save->SelectedId=P.Id;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Serialize profile without touching disk"),UGameplayStatics::SaveGameToMemory(Save,Bytes));
	auto* Loaded=Cast<UTowerProfileSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if(!TestNotNull(TEXT("Versioned save reloads"),Loaded)) return false;
	TestEqual(TEXT("Version retained"),Loaded->Version,1);
	TestTrue(TEXT("Selection retained"),Loaded->SelectedId==P.Id);
	if(!TestEqual(TEXT("Character count retained"),Loaded->Characters.Num(),1)) return false;
	TestTrue(TEXT("Stable skill identifiers survive serialization"),Loaded->Characters[0].Skills==P.Skills);
	TestEqual(TEXT("Unicode name retained"),Loaded->Characters[0].Name,P.Name);
	TArray<int32> Resolved;
	TestTrue(TEXT("Saved identifiers resolve to playable skills"),UTowerGameInstance::ResolveSkills(Loaded->Characters[0],Resolved));
	TestEqual(TEXT("Exactly four slots restored"),Resolved.Num(),4);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTowerProfileValidationTest,"OnlineTower.Frontend.ProfileValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FTowerProfileValidationTest::RunTest(const FString& Parameters)
{
	FTowerCharacterProfile P; P.Id=FGuid::NewGuid(); P.Name=TEXT("Test");
	P.Skills={TEXT("Cleave"),TEXT("Thrust"),TEXT("Whirlwind"),TEXT("Earthbreaker")};
	TArray<int32> Out;
	TestTrue(TEXT("Valid profile accepted"),UTowerGameInstance::ResolveSkills(P,Out));
	P.Skills[1]=TEXT("Cleave"); TestFalse(TEXT("Duplicate ability rejected"),UTowerGameInstance::ResolveSkills(P,Out));
	TestTrue(TEXT("Rejected profile has no partial loadout"),Out.IsEmpty());
	P.Skills[1]=TEXT("MissingAbility"); TestFalse(TEXT("Removed ability rejected without reroll"),UTowerGameInstance::ResolveSkills(P,Out));
	P.Skills[1]=TEXT("Thrust"); P.Skills.Swap(0,3); TestFalse(TEXT("Ultimate cannot occupy ordinary slot"),UTowerGameInstance::ResolveSkills(P,Out));
	P.Skills.Swap(0,3); P.Direction=TEXT("Unknown"); TestFalse(TEXT("Unknown direction rejected"),UTowerGameInstance::ResolveSkills(P,Out));
	return true;
}
#endif
