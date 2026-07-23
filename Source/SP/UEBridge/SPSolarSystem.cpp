// SPSolarSystem.cpp — portage UE de solar_system_map (scène + orbites + caméra).
//
// ÉCHELLE CARTE (déclarée, comme toute approximation [GDD 6.8]) :
//   1 UA = 50 000 unités UE (500 m). Neptune à 30 UA = 15 km de scène : trivial
//   pour les coordonnées double d'UE5 (LWC). Les positions restent en double de
//   l'éphéméride jusqu'au composant.
//   Rayons visuels EXAGÉRÉS (une carte, pas une maquette) : x200 planètes,
//   x8 Soleil, bornés [120..2600] u. Distances des lunes x20 pour sortir de la
//   sphère visuelle exagérée de leur planète (masquées par défaut).
//   Repère : écliptique J2000 (droitier, z-up) -> UE (gaucher, z-up) via y -> -y.

// Les entêtes du jeu AVANT tout entête UE (macros PI/check, cf. SP.Build.cs).
#include "app/bridge_flags.hpp"
#include "fen/core/Constants.hpp"
#include "fen/ephem/Ephemeris.hpp"

#include "SPSolarSystem.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Components/LineBatchComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace {

using fen::ephem::Body;

struct FBodyDef {
	Body B;
	Body Parent;             // Sun pour les planètes ; la planète pour les lunes
	const TCHAR* Asset;      // nom d'asset sous /Game/SolarSystem (import GLB)
	FLinearColor Color;      // repli si le GLB n'est pas importé
	bool bMoon;
	// Rotation propre : période SIDÉRALE vraie en heures, négatif = rétrograde
	// (Vénus, Uranus, Pluton). L'angle est une fonction DÉTERMINISTE de
	// l'époque (θ = 2π·t/T), pas une accumulation par frame : la carte montre
	// l'état au temps de jeu. Axe = normale à l'écliptique — l'obliquité est
	// ignorée : approximation DÉCLARÉE (HUD), pas une vérité.
	double SpinH;
};

const FBodyDef GBodies[] = {
	{Body::Sun,      Body::Sun,      TEXT("Sun"),     FLinearColor(1.00f, 0.85f, 0.30f), false,   609.12},
	{Body::Mercury,  Body::Sun,      TEXT("Mercury"), FLinearColor(0.55f, 0.50f, 0.45f), false,  1407.6},
	{Body::Venus,    Body::Sun,      TEXT("Venus"),   FLinearColor(0.90f, 0.75f, 0.50f), false, -5832.5},
	{Body::EarthBary,Body::Sun,      TEXT("Earth"),   FLinearColor(0.25f, 0.45f, 0.90f), false,    23.9345},
	{Body::Moon,     Body::EarthBary,TEXT("Moon"),    FLinearColor(0.62f, 0.62f, 0.62f), true,    655.72},
	{Body::Mars,     Body::Sun,      TEXT("Mars"),    FLinearColor(0.85f, 0.45f, 0.25f), false,    24.6229},
	{Body::Jupiter,  Body::Sun,      TEXT("Jupiter"), FLinearColor(0.80f, 0.65f, 0.50f), false,     9.925},
	{Body::Saturn,   Body::Sun,      TEXT("Saturn"),  FLinearColor(0.85f, 0.75f, 0.55f), false,    10.656},
	{Body::Titan,    Body::Saturn,   TEXT("Titan"),   FLinearColor(0.80f, 0.65f, 0.30f), true,    382.68},
	{Body::Uranus,   Body::Sun,      TEXT("Uranus"),  FLinearColor(0.55f, 0.75f, 0.85f), false,   -17.24},
	{Body::Neptune,  Body::Sun,      TEXT("Neptune"), FLinearColor(0.35f, 0.50f, 0.90f), false,    16.11},
	{Body::Pluto,    Body::Sun,      TEXT("Pluto"),   FLinearColor(0.65f, 0.60f, 0.55f), false,  -153.29},
};
constexpr int32 NUM_BODIES = UE_ARRAY_COUNT(GBodies);

constexpr double UU_PER_AU = 50000.0;
constexpr double UU_PER_M = UU_PER_AU / fen::cst::AU;
constexpr double PLANET_EXAG = 200.0;
constexpr double SUN_EXAG = 8.0;
constexpr double MOON_DIST_EXAG = 20.0;
constexpr double ORBIT_REDRAW_DAYS = 5.0;   // dérive des éléments : invisible en deçà
constexpr int32 ORBIT_SAMPLES = 128;
constexpr int32 FLEET_PER_CAT = 6;          // marqueurs affichés max par catégorie
constexpr int32 FLEET_GEO0 = 0, FLEET_MARS0 = 6, FLEET_PROBE0 = 12, FLEET_GEOFLIGHT = 18;
constexpr int32 FLEET_TOTAL = 19;

// Vue rapprochée Terre (vol GEO) : scène géocentrique 1:1 déportée LOIN de la
// carte (jamais superposée). 1 u = 100 m -> GEO (42 164 km) = 4,2 km de scène.
constexpr double KM_UU = 10.0;
const FVector CLOSE_ORIGIN(6.0e6, 0.0, 0.0);

// L'éphéméride Standish est sans état : une instance partagée suffit.
const fen::ephem::StandishEphemeris GEph;

// écliptique (droitier) -> UE (gaucher) : miroir en y. DÉCLARÉ, jamais caché.
FVector ToUE(const fen::Vec3& Meters)
{
	return FVector(Meters.x * UU_PER_M, -Meters.y * UU_PER_M, Meters.z * UU_PER_M);
}
FVector ToUEd(double Xm, double Ym, double Zm)
{
	return FVector(Xm * UU_PER_M, -Ym * UU_PER_M, Zm * UU_PER_M);
}
// km géocentriques -> scène rapprochée (même miroir y que la carte).
FVector ToClose(double Xkm, double Ykm, double Zkm)
{
	return CLOSE_ORIGIN + FVector(Xkm * KM_UU, -Ykm * KM_UU, Zkm * KM_UU);
}

double VisualRadiusUU(const FBodyDef& Def)
{
	const double Exag = (Def.B == Body::Sun) ? SUN_EXAG : PLANET_EXAG;
	return FMath::Clamp(fen::ephem::body_radius(Def.B) * UU_PER_M * Exag, 120.0, 2600.0);
}

const FBodyDef* FindDef(Body B)
{
	for (const FBodyDef& D : GBodies) if (D.B == B) return &D;
	return nullptr;
}

// Position monde UE d'un corps à l'époque donnée (lunes : distance exagérée
// autour de la position COURANTE de leur planète).
FVector BodyWorldPos(const FBodyDef& Def, double EpochTdb)
{
	const fen::Epoch E{EpochTdb};
	if (Def.B == Body::Sun) return FVector::ZeroVector;
	if (!Def.bMoon) return ToUE(GEph.state(Def.B, Body::Sun, E).r);
	const FBodyDef* Parent = FindDef(Def.Parent);
	const FVector ParentPos = Parent ? BodyWorldPos(*Parent, EpochTdb) : FVector::ZeroVector;
	return ParentPos + ToUE(GEph.state(Def.B, Def.Parent, E).r) * MOON_DIST_EXAG;
}

// Période orbitale par vis-viva : a = 1/(2/r - v^2/mu), T = 2*pi*sqrt(a^3/mu).
double OrbitalPeriodS(const FBodyDef& Def, double EpochTdb)
{
	const auto PV = GEph.state(Def.B, Def.Parent, fen::Epoch{EpochTdb});
	const double R = fen::norm(PV.r), V2 = fen::norm2(PV.v);
	const double Mu = fen::ephem::body_mu(Def.Parent);
	const double A = 1.0 / (2.0 / R - V2 / Mu);
	if (A <= 0.0) return 0.0;               // non elliptique : pas de trace fermée
	// UE_DOUBLE_TWO_PI et pas fen::cst::TWO_PI : UnrealMathUtility definit TWO_PI
	// en MACRO — toute constante fen homonyme est inutilisable apres les includes UE.
	return UE_DOUBLE_TWO_PI * FMath::Sqrt(A * A * A / Mu);
}

UStaticMesh* FindImportedMesh(const TCHAR* AssetName)
{
	// 1) chemin conventionnel du script d'import (Tools/import_glb_planets.py)
	const FString Direct = FString::Printf(TEXT("/Game/SolarSystem/%s.%s"), AssetName, AssetName);
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Direct)) return M;
	// 2) l'import Interchange nomme le mesh d'après le nœud GLB. Un GLB peut
	// contenir PLUSIEURS meshes (Saturne : "Sphere" = corps + "Circle" =
	// anneaux) : préférer le mesh au nom du corps, sinon "Sphere*" (le corps),
	// sinon le premier StaticMesh trouvé.
	const FAssetRegistryModule& ARM =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByPath(
		FName(*FString::Printf(TEXT("/Game/SolarSystem/%s"), AssetName)), Assets, true);
	const FAssetData* First = nullptr;
	const FAssetData* Named = nullptr;
	const FAssetData* Sphere = nullptr;
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName()) continue;
		if (!First) First = &A;
		const FString Name = A.AssetName.ToString();
		if (!Named && Name.Equals(AssetName, ESearchCase::IgnoreCase)) Named = &A;
		if (!Sphere && Name.StartsWith(TEXT("Sphere"), ESearchCase::IgnoreCase)) Sphere = &A;
	}
	const FAssetData* Pick = Named ? Named : (Sphere ? Sphere : First);
	return Pick ? Cast<UStaticMesh>(Pick->GetAsset()) : nullptr;
}

// Mesh annexe d'un corps (les anneaux de Saturne : nœud GLB "Circle").
UStaticMesh* FindImportedMeshNamed(const TCHAR* AssetName, const TCHAR* NodeName)
{
	const FAssetRegistryModule& ARM =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByPath(
		FName(*FString::Printf(TEXT("/Game/SolarSystem/%s"), AssetName)), Assets, true);
	for (const FAssetData& A : Assets)
		if (A.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName() &&
		    A.AssetName.ToString().StartsWith(NodeName, ESearchCase::IgnoreCase))
			return Cast<UStaticMesh>(A.GetAsset());
	return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
ASPSolarSystemMapActor::ASPSolarSystemMapActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

// ---------------------------------------------------------------------------
bool USPSolarSystemSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer)) return false;
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

void USPSolarSystemSubsystem::Deinitialize()
{
	fen::app::g_render_bridge.carte3d_active = false;
	Super::Deinitialize();
}

TStatId USPSolarSystemSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USPSolarSystemSubsystem, STATGROUP_Tickables);
}

void USPSolarSystemSubsystem::BuildScene()
{
	UWorld* W = GetWorld();
	if (!W) return;

	MapActor = W->SpawnActor<ASPSolarSystemMapActor>();
	UStaticMesh* Sphere =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	// Soleil et marqueur vaisseau : émissifs (param vectoriel "Color" vérifié).
	UMaterialInterface* EmissiveMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));

	for (const FBodyDef& Def : GBodies)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(MapActor);
		C->AttachToComponent(MapActor->GetRootComponent(),
		                     FAttachmentTransformRules::KeepRelativeTransform);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);

		const double RadiusUU = VisualRadiusUU(Def);
		double MeshRadius = 50.0;               // sphère moteur : 50 u de rayon
		if (UStaticMesh* Imported = FindImportedMesh(Def.Asset))
		{
			C->SetStaticMesh(Imported);
			MeshRadius = FMath::Max(1.0f, Imported->GetBounds().SphereRadius);
		}
		else if (Sphere)
		{
			C->SetStaticMesh(Sphere);
			const bool bStar = (Def.B == Body::Sun);
			UMaterialInterface* Base = (bStar && EmissiveMat) ? EmissiveMat : BaseMat;
			if (Base)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, C);
				M->SetVectorParameterValue(TEXT("Color"),
				                           bStar ? Def.Color * 5.0f : Def.Color);
				C->SetMaterial(0, M);
			}
		}
		C->SetWorldScale3D(FVector(RadiusUU / MeshRadius));
		C->RegisterComponent();
		MapActor->BodyMeshes.Add(C);

		// Les anneaux de Saturne : nœud GLB séparé ("Circle"), attaché en
		// enfant du corps — ils suivent position, échelle et rotation propre.
		if (Def.B == Body::Saturn)
			if (UStaticMesh* Rings = FindImportedMeshNamed(Def.Asset, TEXT("Circle")))
			{
				UStaticMeshComponent* R = NewObject<UStaticMeshComponent>(MapActor);
				R->AttachToComponent(C, FAttachmentTransformRules::KeepRelativeTransform);
				R->SetMobility(EComponentMobility::Movable);
				R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				R->SetCastShadow(false);
				R->SetStaticMesh(Rings);
				R->RegisterComponent();
			}
	}

	// Le Soleil éclaire tout le système : pas d'inverse carré à cette échelle
	// (l'éclairement physique ferait la nuit à Neptune — c'est une CARTE).
	MapActor->SunLight = NewObject<UPointLightComponent>(MapActor);
	MapActor->SunLight->AttachToComponent(MapActor->GetRootComponent(),
	                                      FAttachmentTransformRules::KeepRelativeTransform);
	MapActor->SunLight->SetMobility(EComponentMobility::Movable);
	MapActor->SunLight->bUseInverseSquaredFalloff = false;
	MapActor->SunLight->SetAttenuationRadius(3.0e6f);
	MapActor->SunLight->SetIntensity(12.0f);
	MapActor->SunLight->SetLightFalloffExponent(0.02f);
	MapActor->SunLight->SetCastShadows(false);
	MapActor->SunLight->RegisterComponent();

	// Le vaisseau en vol : marqueur émissif, visible seulement quand l'écran
	// publie une position (VehicleSnap.valid).
	MapActor->VehicleMarker = NewObject<UStaticMeshComponent>(MapActor);
	MapActor->VehicleMarker->AttachToComponent(
		MapActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	MapActor->VehicleMarker->SetMobility(EComponentMobility::Movable);
	MapActor->VehicleMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MapActor->VehicleMarker->SetCastShadow(false);
	if (Sphere) MapActor->VehicleMarker->SetStaticMesh(Sphere);
	if (EmissiveMat)
	{
		UMaterialInstanceDynamic* M =
			UMaterialInstanceDynamic::Create(EmissiveMat, MapActor->VehicleMarker);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.85f, 0.2f) * 6.0f);
		MapActor->VehicleMarker->SetMaterial(0, M);
	}
	MapActor->VehicleMarker->SetWorldScale3D(FVector(170.0 / 50.0));
	MapActor->VehicleMarker->SetVisibility(false);
	MapActor->VehicleMarker->RegisterComponent();

	// Marqueurs de flotte [GDD 8.3] : petits points émissifs, anneaux symboliques.
	const FLinearColor FleetCols[4] = {
		FLinearColor(0.45f, 0.75f, 1.0f) * 3.0f,   // relais GEO : bleu clair
		FLinearColor(1.0f, 0.55f, 0.25f) * 3.0f,   // orbiteurs Mars : orange
		FLinearColor(0.9f, 0.9f, 0.9f) * 3.0f,     // sondes lointaines : blanc
		FLinearColor(0.2f, 1.0f, 0.9f) * 5.0f,     // vol GEO en cours : cyan vif
	};
	for (int32 i = 0; i < FLEET_TOTAL; ++i)
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(MapActor);
		M->AttachToComponent(MapActor->GetRootComponent(),
		                     FAttachmentTransformRules::KeepRelativeTransform);
		M->SetMobility(EComponentMobility::Movable);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		if (Sphere) M->SetStaticMesh(Sphere);
		const int32 Cat = (i == FLEET_GEOFLIGHT) ? 3 : i / FLEET_PER_CAT;
		if (EmissiveMat)
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(EmissiveMat, M);
			Mat->SetVectorParameterValue(TEXT("Color"), FleetCols[Cat]);
			M->SetMaterial(0, Mat);
		}
		M->SetWorldScale3D(FVector((i == FLEET_GEOFLIGHT ? 200.0 : 130.0) / 50.0));
		M->SetVisibility(false);
		M->RegisterComponent();
		MapActor->FleetMarkers.Add(M);
	}

	// --- vue rapprochée Terre (vol GEO) : Terre 1:1 + marqueur + éclairage ----
	{
		MapActor->CloseEarth = NewObject<UStaticMeshComponent>(MapActor);
		MapActor->CloseEarth->AttachToComponent(
			MapActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		MapActor->CloseEarth->SetMobility(EComponentMobility::Movable);
		MapActor->CloseEarth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MapActor->CloseEarth->SetCastShadow(false);
		double MeshRadius = 50.0;
		if (UStaticMesh* Terre = FindImportedMesh(TEXT("Earth")))
		{
			MapActor->CloseEarth->SetStaticMesh(Terre);
			MeshRadius = FMath::Max(1.0f, Terre->GetBounds().SphereRadius);
		}
		else if (Sphere)
		{
			MapActor->CloseEarth->SetStaticMesh(Sphere);
			if (BaseMat)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, MapActor->CloseEarth);
				M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.25f, 0.45f, 0.90f));
				MapActor->CloseEarth->SetMaterial(0, M);
			}
		}
		// rayon VRAI : 6371 km -> 63 710 u (pas d'exagération en vue 1:1)
		MapActor->CloseEarth->SetWorldScale3D(FVector(6371.0 * KM_UU / MeshRadius));
		MapActor->CloseEarth->SetWorldLocation(CLOSE_ORIGIN);
		MapActor->CloseEarth->SetVisibility(false);
		MapActor->CloseEarth->RegisterComponent();

		MapActor->CloseMarker = NewObject<UStaticMeshComponent>(MapActor);
		MapActor->CloseMarker->AttachToComponent(
			MapActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		MapActor->CloseMarker->SetMobility(EComponentMobility::Movable);
		MapActor->CloseMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MapActor->CloseMarker->SetCastShadow(false);
		if (Sphere) MapActor->CloseMarker->SetStaticMesh(Sphere);
		if (EmissiveMat)
		{
			UMaterialInstanceDynamic* M =
				UMaterialInstanceDynamic::Create(EmissiveMat, MapActor->CloseMarker);
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 1.0f, 0.9f) * 6.0f);
			MapActor->CloseMarker->SetMaterial(0, M);
		}
		// marqueur SYMBOLIQUE (~400 km) : un satellite réel serait invisible
		MapActor->CloseMarker->SetWorldScale3D(FVector(4000.0 / 50.0));
		MapActor->CloseMarker->SetVisibility(false);
		MapActor->CloseMarker->RegisterComponent();

		MapActor->CloseLight = NewObject<UPointLightComponent>(MapActor);
		MapActor->CloseLight->AttachToComponent(
			MapActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		MapActor->CloseLight->SetMobility(EComponentMobility::Movable);
		MapActor->CloseLight->bUseInverseSquaredFalloff = false;
		MapActor->CloseLight->SetAttenuationRadius(4.0e6f);
		MapActor->CloseLight->SetIntensity(10.0f);
		MapActor->CloseLight->SetLightFalloffExponent(0.02f);
		MapActor->CloseLight->SetCastShadows(false);
		MapActor->CloseLight->SetWorldLocation(CLOSE_ORIGIN + FVector(-8.0e5, -8.0e5, 5.0e5));
		MapActor->CloseLight->RegisterComponent();
	}

	// Caméra carte : au-dessus de l'écliptique, cadrée sur le système interne
	// (~3.5 UA). Le suivi de focus est lissé dans Tick().
	const FVector CamLoc(0.0, -170000.0, 130000.0);
	MapCamera = W->SpawnActor<ACameraActor>(CamLoc,
		UKismetMathLibrary::FindLookAtRotation(CamLoc, FVector::ZeroVector));

	bBuilt = true;
}

// Rotation propre à l'époque donnée : θ = 2π·t/T_sid (période sidérale vraie,
// négatif = rétrograde). Miroir y de la carte (écliptique droitier -> UE
// gaucher) : yaw UE = −θ physique. Fonction pure de l'époque : déterministe.
FRotator SpinAt(const FBodyDef& Def, double EpochTdb)
{
	if (Def.SpinH == 0.0) return FRotator::ZeroRotator;
	const double Tsid = Def.SpinH * 3600.0;
	const double Theta = UE_DOUBLE_TWO_PI * FMath::Frac(EpochTdb / Tsid);
	return FRotator(0.0, -FMath::RadiansToDegrees(Theta), 0.0);
}

void USPSolarSystemSubsystem::UpdatePositions(double EpochTdb)
{
	const bool bMoons = fen::app::g_render_bridge.show_moons.load();
	for (int32 i = 0; i < NUM_BODIES && i < MapActor->BodyMeshes.Num(); ++i)
	{
		const FBodyDef& Def = GBodies[i];
		UStaticMeshComponent* C = MapActor->BodyMeshes[i];
		if (Def.bMoon && !bMoons) { C->SetVisibility(false); continue; }
		C->SetVisibility(true);
		// rotation propre AVANT la position : SetWorldRotation ne touche pas
		// la translation, et les enfants (anneaux de Saturne) suivent.
		C->SetWorldRotation(SpinAt(Def, EpochTdb));
		C->SetWorldLocation(BodyWorldPos(Def, EpochTdb));
	}
	// vue rapprochée : la Terre 1:1 tourne au même taux sidéral — un satellite
	// GEO reste au-dessus de la même longitude, c'est le point de la GEO.
	if (MapActor->CloseEarth)
		MapActor->CloseEarth->SetWorldRotation(
			SpinAt(*FindDef(Body::EarthBary), EpochTdb));
}

void USPSolarSystemSubsystem::RedrawOrbits(double EpochTdb)
{
	UWorld* W = GetWorld();
	ULineBatchComponent* LB =
		W ? W->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) : nullptr;
	if (!LB) return;
	LB->Flush();

	const bool bMoons = fen::app::g_render_bridge.show_moons.load();
	for (const FBodyDef& Def : GBodies)
	{
		if (Def.B == Body::Sun) continue;
		if (Def.bMoon && !bMoons) continue;
		const double T = OrbitalPeriodS(Def, EpochTdb);
		if (T <= 0.0) continue;

		// Trace = la trajectoire RÉELLE échantillonnée sur une période via
		// l'éphéméride (pas une ellipse idéalisée). Lunes : autour de la
		// position courante de la planète, distance exagérée (déclaré).
		const FBodyDef* Parent = FindDef(Def.Parent);
		const FVector ParentNow =
			(Def.bMoon && Parent) ? BodyWorldPos(*Parent, EpochTdb) : FVector::ZeroVector;
		const FLinearColor Col(Def.Color.R, Def.Color.G, Def.Color.B, 0.35f);

		FVector Prev = FVector::ZeroVector;
		for (int32 k = 0; k <= ORBIT_SAMPLES; ++k)
		{
			const double Tk = EpochTdb + (T * k) / ORBIT_SAMPLES;
			const fen::Vec3 Rel = GEph.state(Def.B, Def.bMoon ? Def.Parent : Body::Sun,
			                                 fen::Epoch{Tk}).r;
			const FVector P = Def.bMoon ? ParentNow + ToUE(Rel) * MOON_DIST_EXAG : ToUE(Rel);
			if (k > 0) LB->DrawLine(Prev, P, Col, SDPG_World, 18.0f, 0.0f);
			Prev = P;
		}
	}
	// Vol interplanétaire [GDD 8.3] : le rendu TRACE ce que l'écran publie —
	// trajectoire NOMINALE, corridor d'incertitude 3σ autour de la position
	// ESTIMÉE (échelle VRAIE : le HUD donne la valeur en km — aucune précision
	// artificielle, aucune inflation), nœuds de manœuvre TCM.
	const auto& V = fen::app::g_render_bridge.vehicle;
	if (V.valid.load() && V.n >= 2)
	{
		const FLinearColor Jaune(1.0f, 0.85f, 0.2f, 0.9f);
		for (int32 k = 1; k < V.n; ++k)
		{
			LB->DrawLine(ToUEd(V.traj_m[k - 1][0], V.traj_m[k - 1][1], V.traj_m[k - 1][2]),
			             ToUEd(V.traj_m[k][0], V.traj_m[k][1], V.traj_m[k][2]),
			             Jaune, SDPG_World, 26.0f, 0.0f);
		}

		// corridor 3σ : cercle dans le plan écliptique autour de l'estimé
		const FVector Centre = ToUEd(V.pos_m[0], V.pos_m[1], V.pos_m[2]);
		const double RayonUU = V.corridor_3s_m * UU_PER_M;
		if (RayonUU > 1.0)
		{
			const FLinearColor Orange(1.0f, 0.6f, 0.2f, 0.55f);
			constexpr int32 SEG = 48;
			FVector Prev = Centre + FVector(RayonUU, 0, 0);
			for (int32 k = 1; k <= SEG; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * k / SEG;
				const FVector P = Centre +
					FVector(RayonUU * FMath::Cos(A), RayonUU * FMath::Sin(A), 0.0);
				LB->DrawLine(Prev, P, Orange, SDPG_World, 10.0f, 0.0f);
				Prev = P;
			}
		}
		LastCorridorCenter = Centre;

		// nœuds de manœuvre : croix orange (à faire) / grise (exécutée)
		for (int32 i = 0; i < V.n_nodes && i < 2; ++i)
		{
			const FVector N = ToUEd(V.nodes_m[i][0], V.nodes_m[i][1], V.nodes_m[i][2]);
			const FLinearColor C = V.node_done[i]
				? FLinearColor(0.5f, 0.5f, 0.5f, 0.5f)
				: FLinearColor(1.0f, 0.55f, 0.15f, 0.95f);
			constexpr double L = 320.0;
			LB->DrawLine(N - FVector(L, 0, 0), N + FVector(L, 0, 0), C, SDPG_World, 22.0f, 0.0f);
			LB->DrawLine(N - FVector(0, L, 0), N + FVector(0, L, 0), C, SDPG_World, 22.0f, 0.0f);
			LB->DrawLine(N - FVector(0, 0, L), N + FVector(0, 0, L), C, SDPG_World, 22.0f, 0.0f);
		}
	}
	// Vue rapprochée Terre [GDD 8.3, 7.5] : anneau CIBLE (la nominale du
	// contrat), trace ESTIMÉE (solution de navigation), cercles 1σ/3σ à
	// l'échelle VRAIE. Scène déportée : invisible depuis la carte système.
	const auto& G = fen::app::g_render_bridge.geo;
	if (G.valid.load())
	{
		const double RCible = G.target_sma_km * KM_UU;
		const FLinearColor Vert(0.35f, 0.95f, 0.45f, 0.7f);
		constexpr int32 SEGC = 96;
		FVector PrevC = CLOSE_ORIGIN + FVector(RCible, 0, 0);
		for (int32 k = 1; k <= SEGC; ++k)
		{
			const double A = UE_DOUBLE_TWO_PI * k / SEGC;
			const FVector P = CLOSE_ORIGIN +
				FVector(RCible * FMath::Cos(A), RCible * FMath::Sin(A), 0.0);
			LB->DrawLine(PrevC, P, Vert, SDPG_World, 30.0f, 0.0f);
			PrevC = P;
		}
		const FLinearColor Cyan(0.2f, 1.0f, 0.9f, 0.85f);
		for (int32 k = 1; k < G.n; ++k)
			LB->DrawLine(ToClose(G.traj_km[k - 1][0], G.traj_km[k - 1][1], G.traj_km[k - 1][2]),
			             ToClose(G.traj_km[k][0], G.traj_km[k][1], G.traj_km[k][2]),
			             Cyan, SDPG_World, 30.0f, 0.0f);
		const FVector C = ToClose(G.pos_km[0], G.pos_km[1], G.pos_km[2]);
		for (int32 s = 1; s <= 3; s += 2)
		{
			const double R = G.sigma_km * s * KM_UU;
			if (R < 1.0) continue;
			const FLinearColor Col(1.0f, 0.6f, 0.2f, s == 1 ? 0.8f : 0.35f);
			constexpr int32 SEG = 48;
			FVector Pv = C + FVector(R, 0, 0);
			for (int32 k = 1; k <= SEG; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * k / SEG;
				const FVector P = C + FVector(R * FMath::Cos(A), R * FMath::Sin(A), 0.0);
				LB->DrawLine(Pv, P, Col, SDPG_World, 14.0f, 0.0f);
				Pv = P;
			}
		}
		LastGeoCenter = C;
	}
	LastGeoGen = G.gen.load();
	LastVehicleGen = V.gen.load();
	LastOrbitEpoch = EpochTdb;
	bLastShowMoons = bMoons;
}

void USPSolarSystemSubsystem::SetMapActive(bool bActive)
{
	if (MapActor) MapActor->SetActorHiddenInGame(!bActive);
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	if (bActive)
	{
		PreviousViewTarget = PC->GetViewTarget();
		if (MapCamera) PC->SetViewTargetWithBlend(MapCamera, 0.5f);
		LastOrbitEpoch = -1.0e300;         // force le retracé des orbites
	}
	else
	{
		if (ULineBatchComponent* LB =
		        W ? W->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) : nullptr)
			LB->Flush();
		if (PreviousViewTarget) PC->SetViewTargetWithBlend(PreviousViewTarget, 0.3f);
	}
}

void USPSolarSystemSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	auto& Bridge = fen::app::g_render_bridge;
	const bool bActive = Bridge.carte3d_active.load();

	if (bActive && !bBuilt) BuildScene();
	if (bActive != bWasActive) { SetMapActive(bActive); bWasActive = bActive; }
	if (!bActive || !bBuilt) return;

	const double Epoch = Bridge.epoch_tdb.load();
	UpdatePositions(Epoch);

	// --- marqueur vaisseau (position ESTIMÉE) : à chaque frame ---------------
	const bool bVehicle = Bridge.vehicle.valid.load();
	FVector VehiclePos = FVector::ZeroVector;
	if (MapActor->VehicleMarker)
	{
		MapActor->VehicleMarker->SetVisibility(bVehicle);
		if (bVehicle)
		{
			VehiclePos = ToUEd(Bridge.vehicle.pos_m[0], Bridge.vehicle.pos_m[1],
			                   Bridge.vehicle.pos_m[2]);
			MapActor->VehicleMarker->SetWorldLocation(VehiclePos);
		}
	}

	// --- vue rapprochée Terre : Terre 1:1 + marqueur estimé ------------------
	const bool bGeoValid = Bridge.geo.valid.load();
	FVector GeoPos = FVector::ZeroVector;
	if (MapActor->CloseEarth) MapActor->CloseEarth->SetVisibility(bGeoValid);
	if (MapActor->CloseMarker)
	{
		MapActor->CloseMarker->SetVisibility(bGeoValid);
		if (bGeoValid)
		{
			GeoPos = ToClose(Bridge.geo.pos_km[0], Bridge.geo.pos_km[1],
			                 Bridge.geo.pos_km[2]);
			MapActor->CloseMarker->SetWorldLocation(GeoPos);
		}
	}

	// retracé : dérive d'éphéméride, options, nouvel arc, corridor ou sigma qui
	// ont bougé avec l'estimé (tous les traits vivent dans le batcher persistant)
	if (FMath::Abs(Epoch - LastOrbitEpoch) > ORBIT_REDRAW_DAYS * fen::cst::DAY ||
	    Bridge.show_moons.load() != bLastShowMoons ||
	    Bridge.vehicle.gen.load() != LastVehicleGen ||
	    Bridge.geo.gen.load() != LastGeoGen ||
	    (bVehicle && FVector::Dist(VehiclePos, LastCorridorCenter) > 3.0) ||
	    (bGeoValid && FVector::Dist(GeoPos, LastGeoCenter) > 40.0))
	{
		RedrawOrbits(Epoch);
	}

	// --- flotte en service : éphéméride PAR ENGIN [GDD 8.3] ------------------
	// Le jeu publie la position ESTIMÉE de chaque engin relative à son corps de
	// référence (modèle képlérien déclaré). Ici : conversion m -> u (miroir y)
	// et rayon d'affichage PLANCHER autour des planètes — une orbite GEO fait
	// 14 u quand la Terre affichée en fait ~425 : direction et phase restent
	// VRAIES, l'amplification est déclarée dans le HUD [GDD 7.5].
	{
		const auto& F = Bridge.fleet;
		const int32 NCraft =
			FMath::Min(F.n.load(), fen::app::RenderBridge::FleetSnap::MAX);
		const int32 Base[3] = {FLEET_GEO0, FLEET_MARS0, FLEET_PROBE0};
		int32 Used[3] = {0, 0, 0};
		for (int32 i = 0; i < NCraft; ++i)
		{
			const auto& C = F.craft[i];
			const int32 Cat = FMath::Clamp(C.type, 0, 2);
			if (Used[Cat] >= FLEET_PER_CAT) continue;
			UStaticMeshComponent* M = MapActor->FleetMarkers[Base[Cat] + Used[Cat]++];
			const FBodyDef* Parent =
				(C.parent >= 0 && C.parent < static_cast<int>(Body::COUNT))
					? FindDef(static_cast<Body>(C.parent)) : nullptr;
			const FVector ParentPos =
				Parent ? BodyWorldPos(*Parent, Epoch) : FVector::ZeroVector;
			FVector Rel = ToUEd(C.rel_m[0], C.rel_m[1], C.rel_m[2]);
			if (Parent && Parent->B != Body::Sun)
			{
				const double RMin = VisualRadiusUU(*Parent) * 1.45;
				const double D = Rel.Size();
				if (D < RMin) Rel *= (D > 1e-6 ? RMin / D : 0.0);
			}
			M->SetVisibility(true);
			M->SetWorldLocation(ParentPos + Rel);
		}
		for (int32 Cat = 0; Cat < 3; ++Cat)
			for (int32 k = Used[Cat]; k < FLEET_PER_CAT; ++k)
				MapActor->FleetMarkers[Base[Cat] + k]->SetVisibility(false);
		// vol GEO en cours : marqueur cyan près de la Terre (symbolique, déclaré)
		UStaticMeshComponent* GeoM = MapActor->FleetMarkers[FLEET_GEOFLIGHT];
		const bool bGeo = F.vol_geo_actif.load();
		GeoM->SetVisibility(bGeo);
		if (bGeo)
		{
			const FBodyDef* Terre = FindDef(Body::EarthBary);
			const FVector TerrePos = BodyWorldPos(*Terre, Epoch);
			const double RTerre = VisualRadiusUU(*Terre) * 1.5;
			GeoM->SetWorldLocation(TerrePos +
				FVector(RTerre * 1.25, -RTerre * 0.4, 0.0));
		}
	}

	// --- navigation : vue rapprochée prioritaire, sinon suivi du focus -------
	const int FocusInt = Bridge.focus_body.load();
	const FBodyDef* Focus =
		(FocusInt >= 0 && FocusInt < static_cast<int>(Body::COUNT))
			? FindDef(static_cast<Body>(FocusInt)) : nullptr;
	FVector LookAt = FVector::ZeroVector;
	FVector Desired(0.0, -170000.0, 130000.0);   // vue système par défaut
	if (Bridge.close_view.load() == 1 && bGeoValid)
	{
		LookAt = CLOSE_ORIGIN;
		Desired = CLOSE_ORIGIN + FVector(0.0, -7.0e5, 5.0e5);
	}
	else if (Focus)
	{
		LookAt = BodyWorldPos(*Focus, Epoch);
		const double D = FMath::Max(VisualRadiusUU(*Focus) * 9.0, 4200.0);
		Desired = LookAt + FVector(0.0, -0.62 * D, 0.72 * D);
	}
	if (MapCamera)
	{
		const FVector NewLoc =
			FMath::VInterpTo(MapCamera->GetActorLocation(), Desired, DeltaTime, 3.0f);
		MapCamera->SetActorLocationAndRotation(
			NewLoc, UKismetMathLibrary::FindLookAtRotation(NewLoc, LookAt));
	}
}
