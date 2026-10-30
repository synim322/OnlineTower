#include "TowerTrialCatalog.h"
#include "TowerMaps.h"

UTowerTrialCatalog::UTowerTrialCatalog()
{
	auto Add = [this](FName Id, FText Title, FText Description, int32 Players, bool bKey)
	{
		FTowerTrialEntry Entry;
		Entry.Id = Id; Entry.Title = Title; Entry.Description = Description;
		Entry.MaxPlayers = Players; Entry.bRequiresFloorKey = bKey;
		Entry.MapPath=TowerMaps::Destination(Trials.Num());
		Trials.Add(Entry);
	};
	Add(TEXT("Dungeon"), NSLOCTEXT("Tower", "Dungeon", "ДАНЖ"), NSLOCTEXT("Tower", "DungeonDesc", "Совместное PvE-испытание на четырёх игроков."), 4, false);
	Add(TEXT("Duel"), NSLOCTEXT("Tower", "Duel", "ДУЭЛЬ"), NSLOCTEXT("Tower", "DuelDesc", "PvP один на один. Побеждает сильнейший."), 2, false);
	Add(TEXT("FloorFinal"), NSLOCTEXT("Tower", "Final", "ФИНАЛ ЭТАЖА"), NSLOCTEXT("Tower", "FinalDesc", "12 игроков. Четыре трио. Без полного трио — поражение."), 12, true);
}
