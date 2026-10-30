#include "TowerWorld.h"
#include "TowerCharacter.h"
#include "TowerMaps.h"
#include "TowerHub.h"
#include "Kismet/GameplayStatics.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

FVector TowerHubOrigin() { return FVector(0,0,10000); }
// Separate environments by two kilometres so adjacent runtime sets do not float in each other's sky.
FVector TowerTrialLocation(int32 Trial) { return TowerHubOrigin()+FVector(200000, (Trial-1)*200000,120); }

void BuildTowerEnvironment(AActor* Actor, int32 Zone)
{
    Actor->SetRootComponent(Actor->CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Ridge(TEXT("/Game/Environment/MountainsV2/SM_RidgeA.SM_RidgeA"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Peak(TEXT("/Game/Environment/MountainsV2/SM_PeakB.SM_PeakB"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Saddle(TEXT("/Game/Environment/MountainsV2/SM_SaddleC.SM_SaddleC"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesa(TEXT("/Game/Environment/MountainsV2/SM_MesaD.SM_MesaD"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cliff(TEXT("/Game/Environment/MountainsV2/SM_CliffE.SM_CliffE"));
    const TArray<UStaticMesh*> MountainMeshes={Ridge.Object,Peak.Object,Saddle.Object,Mesa.Object,Cliff.Object};
    // Repeated architectural modules share draw batches instead of hundreds of components.
    TMap<FString,UInstancedStaticMeshComponent*> Batches;
    auto Shape=[&](const TCHAR* Mat, FVector P,FVector Scale,int32 Type=0,bool Solid=true,FRotator R=FRotator::ZeroRotator)
    {
        // Real bitmap materials use world-aligned projection, so architectural scaling does not stretch them.
        const FString OriginalMaterial(Mat);
        if (OriginalMaterial==TEXT("M_OfficeFloor")) Mat=TEXT("M_EnvTerrazzoV2");
        else if (OriginalMaterial==TEXT("M_OfficeWood")) Mat=TEXT("M_EnvWoodV2");
        else if (OriginalMaterial==TEXT("M_OfficeMetal") || OriginalMaterial==TEXT("M_DungeonMetal")) Mat=TEXT("M_EnvMetalV2");
        else if (OriginalMaterial==TEXT("M_MountainRock") || OriginalMaterial==TEXT("M_ArenaStone")) Mat=TEXT("M_EnvRockV2");
        else if (OriginalMaterial==TEXT("M_DesertRock")) Mat=TEXT("M_EnvSandstoneV2");
        else if (OriginalMaterial==TEXT("M_DesertSand")) Mat=TEXT("M_EnvSandV2");
        else if (OriginalMaterial==TEXT("M_Shrub")) Mat=TEXT("M_EnvShrubV2");
        const FString Key=FString::Printf(TEXT("%s_%d_%d"),Mat,Type,Solid?1:0);
        UInstancedStaticMeshComponent** Existing=Batches.Find(Key);
        UInstancedStaticMeshComponent* Batch=Existing?*Existing:nullptr;
        if (!Batch)
        {
            Batch=Actor->CreateDefaultSubobject<UInstancedStaticMeshComponent>(*Key);
            Batch->SetupAttachment(Actor->GetRootComponent());
            UStaticMesh* Mesh=Type>=4 ? MountainMeshes[FMath::Clamp(Type-4,0,4)] :
                Type==1?Cylinder.Object.Get():Type==2?Sphere.Object.Get():Type==3?Cone.Object.Get():Cube.Object.Get();
            Batch->SetStaticMesh(Mesh?Mesh:Sphere.Object.Get());
            Batch->SetCollisionProfileName(Solid?TEXT("BlockAll"):TEXT("NoCollision"));
            const FString Path=FString::Printf(TEXT("/Game/Core/Materials/%s.%s"),Mat,Mat);
            if (auto* Material=LoadObject<UMaterialInterface>(nullptr,*Path)) Batch->SetMaterial(0,Material);
            Batches.Add(Key,Batch);
        }
        Batch->AddInstance(FTransform(R,P,Scale));
    };
    auto Mountain=[&](const TCHAR* Mat,FVector Base,FVector Dimensions,int32 Variant,float Yaw)
    {
        UStaticMesh* Mesh=MountainMeshes[FMath::Clamp(Variant,0,4)];
        if (!Mesh) Mesh=Sphere.Object.Get();
        if (!Mesh) return;
        const FBox Bounds=Mesh->GetBoundingBox();
        const FVector Size=Bounds.GetSize();
        const FVector Scale(Dimensions.X/FMath::Max(Size.X,1.),Dimensions.Y/FMath::Max(Size.Y,1.),Dimensions.Z/FMath::Max(Size.Z,1.));
        Base.Z-=Bounds.Min.Z*Scale.Z;
        Shape(Mat,Base,Scale,4+FMath::Clamp(Variant,0,4),false,FRotator(0,Yaw,0));
    };
    auto Lamp=[&](FString Name,FVector P,FLinearColor Color,float Power,float Radius)
    {
        auto* Light=Actor->CreateDefaultSubobject<UPointLightComponent>(*Name);
        Light->SetupAttachment(Actor->GetRootComponent()); Light->SetRelativeLocation(P);
        Light->SetLightColor(Color); Light->SetIntensity(Power); Light->SetAttenuationRadius(Radius);
        Light->SetCastShadows(false);
    };
    auto Sign=[&](FString Name,FString Text,FVector P,float Size=32.f,FRotator R=FRotator(0,180,0))
    {
        auto* Label=Actor->CreateDefaultSubobject<UTextRenderComponent>(*Name);
        Label->SetupAttachment(Actor->GetRootComponent()); Label->SetRelativeLocation(P); Label->SetRelativeRotation(R);
        Label->SetText(FText::FromString(Text)); Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetWorldSize(Size); Label->SetTextRenderColor(FColor(200,231,239));
    };
    auto Boundary=[&](FString Name,FVector P,FVector Extent,FRotator R=FRotator::ZeroRotator)
    {
        auto* Box=Actor->CreateDefaultSubobject<UBoxComponent>(*Name);
        Box->SetupAttachment(Actor->GetRootComponent()); Box->SetRelativeLocation(P);
        Box->SetRelativeRotation(R);
        Box->SetBoxExtent(Extent); Box->SetCollisionProfileName(TEXT("BlockAll"));
        Box->SetHiddenInGame(true);
    };

    FRandomStream Random(7319);
    if (Zone == -2 || Zone == -1)
    {
    // OFFICE / reception gallery with three enclosed trial suites.
    Shape(TEXT("M_OfficeFloor"),FVector(50,0,-45),FVector(31,30,.9));
    Shape(TEXT("M_OfficeWall"),FVector(50,0,650),FVector(31,30,.6));
    Shape(TEXT("M_OfficeWall"),FVector(1600,0,310),FVector(.4,30,6.2));
    Shape(TEXT("M_OfficeWall"),FVector(-1500,0,310),FVector(.4,30,6.2));
    for (int32 Side=-1;Side<=1;Side+=2)
    {
        Shape(TEXT("M_OfficeWall"),FVector(50,Side*1500,310),FVector(31,.4,6.2));
        Shape(TEXT("M_OfficeMetal"),FVector(50,Side*1470,35),FVector(31,.12,.7));
        for (int32 W=0;W<5;++W)
        {
            const float X=-1200+W*540;
            Shape(TEXT("M_OfficeGlass"),FVector(X,Side*1460,330),FVector(4.8,.1,4.6),0,false);
            Shape(TEXT("M_OfficeMetal"),FVector(X+250,Side*1440,330),FVector(.12,.16,4.8));
        }
    }
    // Ceiling cassettes and suspended linear lighting make the roof visible from play height.
    for (int32 X=0;X<6;++X)
        for (int32 Y=0;Y<6;++Y)
        {
            Shape(TEXT("M_OfficeWall"),FVector(-1250+X*500,-1250+Y*500,610),FVector(4.8,4.8,.18),0,false);
            if (X%2==0)
                Shape(TEXT("M_OfficeLight"),FVector(-1250+X*500,-1250+Y*500,596),FVector(3.5,.18,.08),0,false);
        }
    for (int32 X=0;X<3;++X)
        for (int32 Y=0;Y<3;++Y)
            Lamp(FString::Printf(TEXT("OfficeLamp_%d_%d"),X,Y),FVector(-1000+X*900,-1000+Y*1000,540),FLinearColor(.82,.91,1),3800,1050);
    // Thin floor joints and a continuous wayfinding spine.
    for (int32 X=0;X<15;++X)
        Shape(TEXT("M_OfficeMetal"),FVector(-1400+X*200,0,1),FVector(.012,29,.012),0,false);
    Shape(TEXT("M_OfficeWood"),FVector(450,0,2),FVector(1.6,27,.02),0,false);
    const TCHAR* OfficeNames[]={TEXT("01 / AETHER VAULT"),TEXT("02 / SUMMIT DUEL"),TEXT("03 / DUNE BASIN")};
    for (int32 I=0;I<3;++I)
    {
        const float Y=(I-1)*800.f;
        // The existing portal is centered in this open 360 x 480 cm door.
        for (int32 Side=-1;Side<=1;Side+=2)
        {
            Shape(TEXT("M_OfficeWall"),FVector(900,Y+Side*290,310),FVector(.3,2.2,6.2));
            Shape(TEXT("M_OfficeMetal"),FVector(875,Y+Side*185,240),FVector(.18,.12,4.8));
            Shape(TEXT("M_OfficeWall"),FVector(1250,Y+Side*390,310),FVector(7,.2,6.2));
        }
        Shape(TEXT("M_OfficeWall"),FVector(900,Y,550),FVector(.3,3.6,1.4));
        Shape(TEXT("M_OfficeLight"),FVector(870,Y,482),FVector(.12,3.7,.06),0,false);
        Shape(TEXT("M_OfficeMetal"),FVector(875,Y,554),FVector(.12,5.4,.9),0,false);
        Sign(FString::Printf(TEXT("SuiteSign%d"),I),OfficeNames[I],FVector(860,Y,548),24);
        Shape(TEXT("M_OfficeWood"),FVector(1400,Y,82),FVector(2.2,3.8,.18));
        Shape(TEXT("M_OfficeMetal"),FVector(1400,Y,39),FVector(1.6,2.6,.78));
        Shape(TEXT("M_OfficeGlass"),FVector(1430,Y,122),FVector(.12,1.5,.8),0,false);
        Shape(TEXT("M_OfficeFloor"),FVector(1220,Y,43),FVector(.75,.75,.86));
        Lamp(FString::Printf(TEXT("SuiteLamp%d"),I),FVector(1230,Y,500),FLinearColor(.65,.85,1),2400,650);
    }
    // Reception desk, seating, wall fins and planters keep the arrival routes clear.
    Shape(TEXT("M_OfficeWood"),FVector(-850,-1060,62),FVector(7.5,2.4,1.24));
    Shape(TEXT("M_OfficeFloor"),FVector(-850,-1060,130),FVector(8,2.7,.12));
    Shape(TEXT("M_OfficeGlass"),FVector(-820,-1040,173),FVector(1.5,.13,.8),0,false);
    Sign(TEXT("ReceptionTitle"),TEXT("T O W E R  /  TRIAL DIVISION"),FVector(-1455,0,440),38,FRotator::ZeroRotator);
    for (int32 I=0;I<8;++I)
        Shape(TEXT("M_OfficeWood"),FVector(-1460,-510+I*145,260),FVector(.2,.35,3.6));
    for (int32 I=0;I<3;++I)
    {
        const FVector P(-1000+I*400,1200,0);
        Shape(TEXT("M_OfficeMetal"),P+FVector(0,0,24),FVector(2.8,.9,.48));
        Shape(TEXT("M_OfficeFloor"),P+FVector(0,0,58),FVector(2.8,1.2,.2));
        Shape(TEXT("M_OfficeFloor"),P+FVector(0,50,100),FVector(2.8,.2,.9));
    }
    for (int32 Side=-1;Side<=1;Side+=2)
    {
        FVector P(620,Side*1320,0);
        Shape(TEXT("M_OfficeMetal"),P+FVector(0,0,45),FVector(1.4,1.4,.9));
        for (int32 K=0;K<5;++K)
            Shape(TEXT("M_Shrub"),P+FVector(FMath::Cos(K*2.f)*38,FMath::Sin(K*2.f)*38,110+K*17),FVector(.75,.65,1.5),2,false);
    }

    }
    if (Zone == -2 || Zone == 0)
    {
    // PVE / enclosed aether-machine corridor, gallery, dogleg and terminal chamber.
    const FVector Dungeon=Zone == -2 ? TowerTrialLocation(0)-TowerHubOrigin()-FVector(0,0,120) : FVector::ZeroVector;
    auto Hall=[&](FVector Center,FVector Size,bool AlongX,int32 Id)
    {
        const FVector C=Dungeon+Center;
        Shape(TEXT("M_DungeonMetal"),C+FVector(0,0,-50),FVector(Size.X/100,Size.Y/100,1));
        Shape(TEXT("M_DungeonMetal"),C+FVector(0,0,850),FVector(Size.X/100,Size.Y/100,.6));
        for (int32 Side=-1;Side<=1;Side+=2)
        {
            FVector WallScale=AlongX?FVector(Size.X/100,.5,8.5):FVector(.5,Size.Y/100,8.5);
            FVector WallP=AlongX?FVector(0,Side*Size.Y/2,425):FVector(Side*Size.X/2,0,425);
            if (Id==3 && Side==-1)
            {
                // Leave the west entry open from y=-900 to +900 for the gallery dogleg.
                WallP.Y=900; WallScale.Y=28;
            }
            Shape(TEXT("M_MountainRock"),C+WallP,WallScale);
            Shape(TEXT("M_DungeonGlow"),C+(AlongX?FVector(0,Side*(Size.Y/2-30),90):FVector(Side*(Size.X/2-30),0,90)),
                AlongX?FVector(Size.X/100,.06,.08):FVector(.06,Size.Y/100,.08),0,false);
        }
        const int32 Count=FMath::FloorToInt((AlongX?Size.X:Size.Y)/600);
        for (int32 I=0;I<Count;++I)
        {
            const float Along=-(AlongX?Size.X:Size.Y)/2+300+I*600;
            const FVector Step=AlongX?FVector(Along,0,0):FVector(0,Along,0);
            for (int32 Side=-1;Side<=1;Side+=2)
            {
                const FVector Edge=AlongX?FVector(0,Side*(Size.Y/2-75),410):FVector(Side*(Size.X/2-75),0,410);
                Shape(TEXT("M_DungeonMetal"),C+Step+Edge,FVector(1.4,1.4,8.2));
                Shape(TEXT("M_DungeonGlow"),C+Step+Edge+FVector(AlongX?-73:0,AlongX?0:-73,0),FVector(.06,.12,4.8),0,false);
            }
            Shape(TEXT("M_OfficeMetal"),C+Step+FVector(0,0,780),
                AlongX?FVector(1.3,Size.Y/100,1):FVector(Size.X/100,1.3,1));
            Shape(TEXT("M_DungeonGlow"),C+Step+FVector(0,0,720),
                AlongX?FVector(.16,Size.Y/100-3,.08):FVector(Size.X/100-3,.16,.08),0,false);
            Lamp(FString::Printf(TEXT("AetherLamp%d_%d"),Id,I),C+Step+FVector(0,0,600),FLinearColor(.1,.62,1),4200,950);
        }
    };
    Hall(FVector(300,0,0),FVector(3000,1800,0),true,0);
    Hall(FVector(3000,0,0),FVector(2400,1000,0),true,1);
    // Gallery's east opening leads into the offset turn, not through a solid wall.
    Hall(FVector(5200,0,0),FVector(2000,1800,0),true,2);
    Hall(FVector(6600,1400,0),FVector(800,4600,0),false,3);
    Hall(FVector(6600,4600,0),FVector(2800,1800,0),false,4);
    Shape(TEXT("M_MountainRock"),Dungeon+FVector(-1220,0,420),FVector(.4,18,8.4));
    Shape(TEXT("M_MountainRock"),Dungeon+FVector(6600,-920,420),FVector(8,.4,8.4));
    Shape(TEXT("M_MountainRock"),Dungeon+FVector(6600,5520,420),FVector(28,.4,8.4));
    // Transition shoulders close the unused sides while leaving the walking connection open.
    for (int32 Side=-1;Side<=1;Side+=2)
    {
        Shape(TEXT("M_DungeonMetal"),Dungeon+FVector(1800,Side*710,420),FVector(.4,3.8,8.4));
        Shape(TEXT("M_DungeonMetal"),Dungeon+FVector(4200,Side*710,420),FVector(.4,3.8,8.4));
        Shape(TEXT("M_DungeonMetal"),Dungeon+FVector(6600+Side*900,3700,420),FVector(10,.4,8.4));
    }
    // Crystal machinery is kept to gallery edges; the central combat lane stays clear.
    for (int32 I=0;I<6;++I)
        for (int32 Side=-1;Side<=1;Side+=2)
        {
            FVector P=Dungeon+FVector(-300+I*1100,Side*700,0);
            Shape(TEXT("M_OfficeMetal"),P+FVector(0,0,70),FVector(2.6,2.6,1.4),1);
            Shape(TEXT("M_DungeonGlow"),P+FVector(0,0,260),FVector(1.3,1.3,3.8),3,false,FRotator(0,0,Side*12));
        }
    Shape(TEXT("M_DungeonGlow"),Dungeon+FVector(6600,5100,350),FVector(3,3,6),3,false);
    Sign(TEXT("DungeonTitle"),TEXT("AETHER VAULT  /  01"),Dungeon+FVector(1600,0,650),42);

    }
    if (Zone == -2 || Zone == 1)
    {
    // DUEL / exposed summit platform over a layered mountain massif.
    const FVector Duel=Zone == -2 ? TowerTrialLocation(1)-TowerHubOrigin()-FVector(0,0,120) : FVector::ZeroVector;
    Shape(TEXT("M_ArenaStone"),Duel+FVector(0,0,-75),FVector(36,36,1.5),1);
    // A broad sculpted mesa reaches almost to the original map elevation; it is not a floating cone.
    Mountain(TEXT("M_MountainRock"),Duel+FVector(0,0,-8020),FVector(9600,9600,8000),3,12);
    for (int32 I=0;I<7;++I)
    {
        const float A=I*2*PI/7;
        Mountain(TEXT("M_MountainRock"),Duel+FVector(FMath::Cos(A)*2700,FMath::Sin(A)*2700,-7800),
            FVector(5200,4600,5900+(I%3)*400),I%2==0?4:0,I*51.f);
    }
    Shape(TEXT("M_ArenaStone"),Duel+FVector(0,0,1),FVector(29,29,.025),1,false);
    for (int32 I=0;I<24;++I)
    {
        const float A=I*2*PI/24;
        const FVector Radial(FMath::Cos(A),FMath::Sin(A),0);
        Shape(TEXT("M_OfficeMetal"),Duel+Radial*1710+FVector(0,0,55),FVector(1.6,4.3,1.1),0,true,FRotator(0,FMath::RadiansToDegrees(A),0));
        Shape(TEXT("M_OfficeLight"),Duel+Radial*1640+FVector(0,0,6),FVector(.15,2.8,.06),0,false,FRotator(0,FMath::RadiansToDegrees(A),0));
        Boundary(FString::Printf(TEXT("SummitBoundary%d"),I),Duel+Radial*1790+FVector(0,0,650),FVector(30,255,750),FRotator(0,FMath::RadiansToDegrees(A),0));
    }
    // High invisible boundary preserves the prototype's safe playable space while leaving sky unobstructed.

    for (int32 I=0;I<18;++I)
    {
        const float A=I*2*PI/18+Random.FRandRange(-.08f,.08f);
        const float Radius=Random.FRandRange(4900,7100);
        const float H=Random.FRandRange(3600,6500);
        const FVector P=Duel+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,-3100);
        Mountain(TEXT("M_MountainRock"),P,FVector(Random.FRandRange(3700,5200),Random.FRandRange(3200,4800),H),
            I%5,Random.FRandRange(0,360));
        if (I%2==0)
            Mountain(TEXT("M_MountainRock"),Duel+FVector(FMath::Cos(A)*10000,FMath::Sin(A)*10000,-4300),
                FVector(6500,5400,Random.FRandRange(5300,7600)),(I+2)%5,Random.FRandRange(0,360));
    }

    }
    if (Zone == -2 || Zone == 2)
    {
    // FINAL / broad sand basin, scrub islands and an unbroken mountain horizon.
    const FVector Desert=Zone == -2 ? TowerTrialLocation(2)-TowerHubOrigin()-FVector(0,0,120) : FVector::ZeroVector;
    Shape(TEXT("M_DesertSand"),Desert+FVector(0,0,-100),FVector(90,90,2));
    for (int32 I=0;I<18;++I)
    {
        const float A=I*2*PI/18+Random.FRandRange(-.045f,.045f);
        const float R=Random.FRandRange(6600,7100);
        const FVector P=Desert+FVector(FMath::Cos(A)*R,FMath::Sin(A)*R,-350);
        Mountain(TEXT("M_DesertRock"),P,FVector(Random.FRandRange(4200,5100),Random.FRandRange(3500,4400),Random.FRandRange(2500,4300)),
            (I*3+1)%5,Random.FRandRange(0,360));
        if (I%2==0)
            Mountain(TEXT("M_DesertRock"),Desert+FVector(FMath::Cos(A+.1f)*10000,FMath::Sin(A+.1f)*10000,-600),
                FVector(6500,5600,Random.FRandRange(4200,6500)),(I+2)%5,Random.FRandRange(0,360));
    }
    // Circular containment stays inside the surrounding mountain silhouettes, including diagonal travel.
    for (int32 I=0;I<40;++I)
    {
        const float A=I*2*PI/40;
        Boundary(FString::Printf(TEXT("BasinBoundary%d"),I),Desert+FVector(FMath::Cos(A)*4300,FMath::Sin(A)*4300,650),
            FVector(35,370,750),FRotator(0,FMath::RadiansToDegrees(A),0));
    }
    for (int32 I=0;I<58;++I)
    {
        FVector P=Desert+FVector(Random.FRandRange(-3900,3900),Random.FRandRange(-3900,3900),0);
        // Arrival, return portal and starting combat targets have a generous clear lane.
        if (FMath::Abs(P.Y-Desert.Y)<600 && P.X-Desert.X<1100 && P.X-Desert.X>-1400) continue;
        if (I%4==0)
        {
            Shape(TEXT("M_DesertRock"),P+FVector(0,0,75),FVector(2.8,2.1,1.8),2,true,FRotator(9,I*19.f,8));
            Shape(TEXT("M_DesertSand"),P-FVector(160,0,32),FVector(10,6,1),2,false);
        }
        for (int32 J=0;J<5;++J)
        {
            const FVector Q=P+FVector(FMath::Cos(J*2.1f)*70,FMath::Sin(J*2.1f)*65,35+J*8);
            Shape(TEXT("M_Shrub"),Q,FVector(1.25,.9,.8),2,false,FRotator(0,J*37.f,0));
            Shape(TEXT("M_OfficeWood"),Q-FVector(0,0,22),FVector(.09,.09,.65),1,false,FRotator(0,J*37.f,20));
        }
    }
    // Four low stone landmarks suggest the final's four-team composition without altering match rules.
    for (int32 I=0;I<4;++I)
    {
        const float A=PI*.25f+I*PI*.5f;
        FVector P=Desert+FVector(FMath::Cos(A)*2700,FMath::Sin(A)*2700,0);
        Shape(TEXT("M_DesertRock"),P+FVector(0,0,100),FVector(5,2.5,2),0,true,FRotator(0,I*90.f,0));
        Shape(TEXT("M_OfficeMetal"),P+FVector(0,0,215),FVector(.7,.7,2.3),0,false,FRotator(0,45+I*90.f,0));
    }

    }
    auto* Sun=Actor->CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
    Sun->SetupAttachment(Actor->GetRootComponent()); Sun->SetRelativeRotation(FRotator(-38,-35,0));
    Sun->SetAtmosphereSunLight(true); Sun->SetIntensity(6); Sun->SetLightColor(FLinearColor(1,.91,.78));
    auto* Atmosphere=Actor->CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
    Atmosphere->SetupAttachment(Actor->GetRootComponent());
    auto* Sky=Actor->CreateDefaultSubobject<USkyLightComponent>(TEXT("Sky"));
    Sky->SetupAttachment(Actor->GetRootComponent()); Sky->SetRealTimeCapture(true); Sky->SetIntensity(.8f);
    auto* Clouds=Actor->CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
    Clouds->SetupAttachment(Actor->GetRootComponent()); Clouds->SetLayerBottomAltitude(1.5f); Clouds->SetLayerHeight(3);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> CloudMaterial(TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
    if (CloudMaterial.Succeeded()) Clouds->SetMaterial(CloudMaterial.Object);
    auto* Fog=Actor->CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Mist"));
    Fog->SetupAttachment(Actor->GetRootComponent()); Fog->SetRelativeLocation(FVector(0,0,-1000));
    Fog->SetFogDensity(.003f); Fog->SetFogHeightFalloff(.3f); Fog->SetFogInscatteringColor(FLinearColor(.36,.44,.55));
}


ATowerPortal::ATowerPortal()
{
	bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true);
	auto* Trigger=CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger")); RootComponent=Trigger;
	Trigger->InitBoxExtent(FVector(60,145,190)); Trigger->SetCollisionProfileName(TEXT("OverlapOnlyPawn"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this,&ATowerPortal::Enter);
	auto* Surface=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface")); Surface->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/Core/Materials/M_Portal.M_Portal"));
	Surface->SetStaticMesh(Sphere.Object); Surface->SetRelativeScale3D(FVector(.16,2.8,3.8));
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision); if (Glow.Succeeded()) Surface->SetMaterial(0,Glow.Object);
	Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label")); Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(-80,0,245)); Label->SetRelativeRotation(FRotator(0,180,0));
	Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(28); Label->SetTextRenderColor(FColor(200,220,255));
	auto* Light=CreateDefaultSubobject<UPointLightComponent>(TEXT("Light")); Light->SetupAttachment(RootComponent);
	Light->SetRelativeLocation(FVector(-150,0,0)); Light->SetLightColor(FLinearColor(.05,.5,1)); Light->SetIntensity(3000); Light->SetAttenuationRadius(700); Light->SetCastShadows(false);
}
void ATowerPortal::BeginPlay()
{
	Super::BeginPlay(); UpdateLabel();
	if (HasAuthority()) GetWorldTimerManager().SetTimer(OverlapTimer,this,&ATowerPortal::CheckVisitors,.25f,true);
}
void ATowerPortal::CheckVisitors()
{
	TArray<AActor*> Visitors; GetOverlappingActors(Visitors,ATowerCharacter::StaticClass());
	for (AActor* Visitor:Visitors) Enter(nullptr,Visitor,nullptr,0,false,FHitResult());
}
void ATowerPortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ATowerPortal,Destination); DOREPLIFETIME(ATowerPortal,DestinationTrial);
}
void ATowerPortal::UpdateLabel()
{
	const TCHAR* Names[]={TEXT("DUNGEON / TRAINING"),TEXT("DUEL / TRAINING"),TEXT("FINAL / TRAINING")};
	Label->SetText(FText::FromString(DestinationTrial>=0 && DestinationTrial<3 ? Names[DestinationTrial] : TEXT("RETURN TO HUB")));
}
void ATowerPortal::Enter(UPrimitiveComponent* Overlapped,AActor* Other,UPrimitiveComponent* OtherComponent,int32 BodyIndex,bool bSweep,const FHitResult& Hit)
{
	if (!HasAuthority()) return;
	auto* Character=Cast<ATowerCharacter>(Other);
	if (!Character || Character->NextPortalTime>GetWorld()->GetTimeSeconds()) return;
	if (TowerMaps::GetZone(GetWorld()) != -2)
	{
		Character->NextPortalTime=GetWorld()->GetTimeSeconds()+3;
		if (GetNetMode()!=NM_Standalone)
		{
			if (auto* PC=Cast<ATowerHubController>(Character->GetController())) PC->ClientTravelUnavailable();
			return;
		}
		const FName Map=TowerMaps::Destination(DestinationTrial);
		if (!Map.IsNone()) { UE_LOG(LogTemp,Display,TEXT("TOWER_MAP_TRAVEL Destination=%s"),*Map.ToString()); UGameplayStatics::OpenLevel(this,Map,true); }
		return;
	}
	// Offset arrivals so multiple players do not appear inside each other.
	int32 Occupants=0;
	for (TActorIterator<ATowerCharacter> It(GetWorld());It;++It) if (*It!=Character && It->TrialIndex==DestinationTrial) ++Occupants;
	const int32 Capacity=DestinationTrial==0 ? 4 : DestinationTrial==1 ? 2 : 12;
	if (DestinationTrial>=0 && Occupants>=Capacity) return;
	const FVector Arrival=Destination+FVector(0,(Occupants%4)*130-195,0);
	if (Character->TeleportTo(Arrival,FRotator::ZeroRotator))
	{
		Character->TrialIndex=DestinationTrial; Character->NextPortalTime=GetWorld()->GetTimeSeconds()+2;
		Character->ForceNetUpdate();
		UE_LOG(LogTemp,Display,TEXT("TOWER_PORTAL Player=%s Trial=%d"),*Character->GetName(),DestinationTrial);
	}
}

ATowerPracticeDummy::ATowerPracticeDummy()
{
	bReplicates=true; SetReplicateMovement(true);
	auto* Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body")); RootComponent=Body;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Trim(TEXT("/Game/Core/Materials/M_HubTrim.M_HubTrim"));
	Body->SetStaticMesh(Cylinder.Object); Body->SetRelativeScale3D(FVector(.7,.7,1.5)); Body->SetCollisionProfileName(TEXT("BlockAll"));
	if (Trim.Succeeded()) Body->SetMaterial(0,Trim.Object);
	Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("HealthLabel")); Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(0,0,110)); Label->SetRelativeRotation(FRotator(0,180,0)); Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(25);
}
void ATowerPracticeDummy::BeginPlay() { Super::BeginPlay(); UpdateLabel(); }
void ATowerPracticeDummy::UpdateLabel() { Label->SetText(FText::FromString(FString::Printf(TEXT("TARGET  %.0f / 200"),Health))); }
void ATowerPracticeDummy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ATowerPracticeDummy,Health); }
float ATowerPracticeDummy::TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
	if (!HasAuthority() || Amount<=0 || Health<=0) return 0;
	const float Applied=FMath::Min(Amount,Health); Health-=Applied; UpdateLabel(); ForceNetUpdate();
	if (Health<=0) GetWorldTimerManager().SetTimer(ResetTimer,FTimerDelegate::CreateWeakLambda(this,[this]{Health=200;UpdateLabel();ForceNetUpdate();}),3,false);
	return Applied;
}
