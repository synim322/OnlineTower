#include "TowerAbilities.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTowerWeightedLoadoutTest,"OnlineTower.Abilities.WeightedUniqueLoadout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FTowerWeightedLoadoutTest::RunTest(const FString& Parameters)
{
	const auto* Pool=GetDefault<UTowerAbilityPool>();
	TArray<int32> FirstDraws; FirstDraws.Init(0,Pool->Skills.Num());
	FRandomStream Stream(20261023);
	for (int32 Draw=0;Draw<20000;++Draw)
	{
		const auto Skills=Pool->Roll(Stream);
		if (Skills.Num()!=3) { AddError(TEXT("A character must receive exactly three skills")); return false; }
		TSet<int32> Unique;
		for (int32 Skill:Skills)
		{
			if (Skill<0 || Skill>=Pool->Skills.Num()-1) { AddError(TEXT("Invalid skill or ultimate included in normal draw")); return false; }
			Unique.Add(Skill);
		}
		if (Unique.Num()!=3) { AddError(TEXT("Duplicate skill in loadout")); return false; }
		++FirstDraws[Skills[0]];
	}
	for (int32 I=1;I<Pool->Skills.Num()-1;++I)
		TestTrue(TEXT("Rarer abilities appear less often in the first draw"),FirstDraws[I-1]>FirstDraws[I]);
	TestTrue(TEXT("Legendary remains obtainable"),FirstDraws[5]>0);
	FRandomStream A(71),B(71);
	TestTrue(TEXT("Same seed yields same loadout"),Pool->Roll(A)==Pool->Roll(B));
	return true;
}
#endif
