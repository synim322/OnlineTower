#include "TowerMaps.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
int32 TowerMaps::GetZone(const UWorld* World) {
 if (!World) return -2;
 const FString Name=World->GetMapName();
 for(int32 I=0;I<3;++I) if(Name.EndsWith(FPackageName::GetShortName(Trials[I]))) return I;
 return Name.EndsWith(TEXT("L_Hub")) ? -1 : -2;
}
FName TowerMaps::Destination(int32 Trial) { return Trial==-1 ? FName(Hub) : Trial>=0 && Trial<3 ? FName(Trials[Trial]) : NAME_None; }
