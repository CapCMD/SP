// SPSolarSystem.cpp — LA CARTE DU SYSTÈME SOLAIRE (écran principal du jeu).
// Portage UE5 natif de render/app/solar_system_map.cpp (référence Vulkan `spr`).
//
// ═══ ÉCHELLE VRAIE ═══ (plus aucune exagération à déclarer)
//   1 unité UE = 1 km. Les rayons sont les rayons RÉELS, les distances les
//   distances RÉELLES : la Terre fait 6 371 u de rayon, l'orbite GEO 42 164 u,
//   Neptune est à 4,5e9 u. Une planète devient donc sous-pixellique dès qu'on
//   s'éloigne — c'est le comportement voulu (`body_min_size = 0` de la
//   référence) : le HUD prend alors le relais avec un MARQUEUR et un libellé.
//
// ═══ RENDU RELATIF À LA CAMÉRA (floating origin) ═══
//   Les positions viennent de l'éphéméride en double (m), sont converties en km
//   double, et l'ORIGINE DE RENDU EST L'ŒIL : on place chaque objet à
//   (monde − œil). La caméra reste à l'origine du monde UE. Conséquence : la
//   précision est maximale là où on regarde, et l'erreur relative d'un corps
//   lointain (float, ~1e-7) reste très inférieure au pixel.
//   Repère : écliptique J2000 (droitier, z-up) -> UE (gaucher, z-up) via y -> −y.
//
// ═══ L'ENTRÉE EST NATIVE, LE PONT RESTE LA FRONTIÈRE ═══
//   Depuis le passage en rendu total UE5, la souris et le clavier arrivent
//   normalement dans le pipeline UE (ASPPlayerController) : c'est lui qui
//   COMMANDE la caméra via le pont (RenderBridge::cam), et ce fichier
//   l'APPLIQUE. En retour il PUBLIE la projection écran des corps
//   (RenderBridge::screen), dont se servent le HUD pour ses marqueurs et
//   libellés, et le contrôleur pour le picking. Sens unique des deux côtés.

// Les entêtes du jeu AVANT tout entête UE (macros PI/check, cf. SP.Build.cs).
#include "app/bridge_flags.hpp"
#include "fen/core/Constants.hpp"
#include "fen/ephem/BodyOrientation.hpp"
#include "fen/ephem/Ephemeris.hpp"

#include "SPSolarSystem.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace {

using fen::ephem::Body;

struct FBodyDef
{
	Body B;
	Body Parent;             // Sun pour les planètes ; la planète pour les lunes
	const TCHAR* Asset;      // nom d'asset sous /Game/SolarSystem (import GLB)
	FLinearColor Color;      // repli si le GLB n'est pas importé ; couleur du marqueur
	bool bMoon;
	// Période SIDÉRALE vraie en heures (négatif = rétrograde). N'est utilisée QUE
	// comme repli pour un corps sans éléments IAU : l'orientation réelle (axe +
	// obliquité + méridien origine) vient désormais de `ephem/BodyOrientation`
	// (WGCCRE), fonction déterministe de l'époque. Jalon B2 (IAU) : LEVÉ.
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

// ═══ ÉCHELLE RÉELLE : 1 u = 1 cm (unité native UE), AUCUNE compression. ═══
// Le rendu reste caméra-relatif (œil = origine, positions km rebasées en double),
// mais on convertit en cm RÉELS : un objet de 100 m fait 10 000 u (taille NORMALE)
// -> fini la micro-échelle qui cassait Nanite/culling/précision. Les corps
// lointains restent des marqueurs HUD (pas de conflit de z-buffer).
constexpr double KM_PER_M  = 1.0e-3;
constexpr double UU_PER_KM = 1.0e5;      // 1 km = 100 000 cm = 100 000 u
constexpr int32  ORBIT_SAMPLES = 192;
constexpr double ORBIT_REDRAW_DAYS = 2.0;      // dérive d'éphéméride sous le pixel
constexpr int32  FLEET_PER_CAT = 6;
constexpr int32  FLEET_GEO0 = 0, FLEET_MARS0 = 6, FLEET_PROBE0 = 12, FLEET_GEOFLIGHT = 18;
constexpr int32  FLEET_TOTAL = 19;
// Taille ANGULAIRE des marqueurs de scène (fraction de la distance à l'œil) :
// ils gardent une taille écran constante quel que soit le zoom.
constexpr double MARKER_ANG = 0.0022;
// Un corps n'est rendu en GÉOMÉTRIE que si sa distance à l'œil < rayon × ce
// facteur (sinon MARQUEUR HUD). Garde la géométrie dans le domaine où le float
// est précis (~sub-km sur le corps) : au-delà, à l'échelle réelle, un corps à
// ~1e14 u faceterait. ~ correspond à un rayon apparent de quelques pixels.
constexpr double BODY_GEOM_FACTOR = 400.0;
// PLAFOND DE PRÉCISION GPU. UE convertit la matrice monde de CHAQUE primitive en
// format GPU (tuile+offset float) et exige que l'origine reste sous
// UE_DF_FLOAT_MAX_VALUE = (1<<23)/4 - 1 ≈ 2,10e6 u (Core/DoubleFloat.cpp:19).
// À l'échelle réelle (1 u = 1 cm), un composant rebasé sur un corps focalisé
// LOINTAIN (lumière solaire, marqueurs station/flotte à des dizaines d'UA de
// l'œil) dépasse ce plafond de 8 ordres de grandeur -> ensure « precision loss
// while converting matrix to GPU format » + transform corrompu. On BORNE donc
// la position de rendu des COMPOSANTS (pas les lignes d'orbite) à ce rayon, sous
// le plafond avec marge. L'œil étant à l'origine, borner radialement préserve
// EXACTEMENT la position écran (projection invariante par échelle radiale) ;
// seule la profondeur du marqueur change — invisible pour un point/marqueur.
constexpr double RENDER_MAX_UU = 1.0e6;

// ═══ REBASÉ (km) -> RENDU (cm réels) ═══
// Ancienne « compression scaled space » SUPPRIMÉE (décision 2026-07-25 : rendu à
// échelle réelle). On applique juste le facteur d'échelle constant km->cm. La
// profondeur des corps lointains est gérée par le LOD (marqueurs HUD), plus par
// une homothétie. Nom conservé pour limiter la diffusion du changement.
FVector CompressKm(const FVector& RelKm, double* OutFactor = nullptr)
{
	// Échelle réelle CONSTANTE (plus de compression de profondeur) : rebasé (km,
	// relatif à l'œil) -> cm réels. Le facteur est le même pour la position ET le
	// rayon (OutFactor), donc la taille angulaire reste exacte.
	if (OutFactor) *OutFactor = UU_PER_KM;
	return RelKm * UU_PER_KM;
}

// L'éphéméride Standish est sans état : une instance partagée suffit.
const fen::ephem::StandishEphemeris GEph;

// écliptique (droitier, m) -> UE (gaucher, km) : miroir en y. DÉCLARÉ.
FVector EclToUeKm(const fen::Vec3& Meters)
{
	return FVector(Meters.x * KM_PER_M, -Meters.y * KM_PER_M, Meters.z * KM_PER_M);
}
FVector EclToUeKmd(double Xm, double Ym, double Zm)
{
	return FVector(Xm * KM_PER_M, -Ym * KM_PER_M, Zm * KM_PER_M);
}

const FBodyDef* FindDef(Body B)
{
	for (const FBodyDef& D : GBodies) if (D.B == B) return &D;
	return nullptr;
}

// Rayon VRAI du corps, en km (aucune exagération).
double BodyRadiusKm(Body B)
{
	return fen::ephem::body_radius(B) * KM_PER_M;
}

// Position MONDE d'un corps (km, axes UE, héliocentrique). Les lunes sont à leur
// distance RÉELLE de leur planète : à l'échelle vraie, rien n'a besoin d'être
// écarté artificiellement.
FVector BodyWorldKm(const FBodyDef& Def, double EpochTdb)
{
	if (Def.B == Body::Sun) return FVector::ZeroVector;
	const fen::Epoch E{EpochTdb};
	if (Def.bMoon)
	{
		const FVector ParentPos = (Def.Parent == Body::Sun)
			? FVector::ZeroVector
			: EclToUeKm(GEph.state(Def.Parent, Body::Sun, E).r);
		return ParentPos + EclToUeKm(GEph.state(Def.B, Def.Parent, E).r);
	}
	return EclToUeKm(GEph.state(Def.B, Body::Sun, E).r);
}

// Période orbitale (s) autour du parent, tirée de l'état réel (vis-viva) — pas
// d'une table : c'est l'éphéméride qui décide.
double OrbitalPeriodS(const FBodyDef& Def, double EpochTdb)
{
	if (Def.B == Body::Sun) return 0.0;
	const Body Centre = Def.bMoon ? Def.Parent : Body::Sun;
	const fen::ephem::PosVel PV = GEph.state(Def.B, Centre, fen::Epoch{EpochTdb});
	const double Mu = fen::ephem::body_mu(Centre);
	const double R = FMath::Sqrt(PV.r.x * PV.r.x + PV.r.y * PV.r.y + PV.r.z * PV.r.z);
	const double V2 = PV.v.x * PV.v.x + PV.v.y * PV.v.y + PV.v.z * PV.v.z;
	if (R <= 0.0 || Mu <= 0.0) return 0.0;
	const double Denom = 2.0 / R - V2 / Mu;         // 1/a (vis-viva)
	if (Denom <= 0.0) return 0.0;                    // trajectoire non fermée
	const double A = 1.0 / Denom;
	// UE_DOUBLE_TWO_PI et pas fen::cst::TWO_PI : UnrealMathUtility définit TWO_PI
	// en MACRO — toute constante fen homonyme est inutilisable après les includes UE.
	return UE_DOUBLE_TWO_PI * FMath::Sqrt(A * A * A / Mu);
}

// Orientation propre à l'époque = INCLINAISON (obliquité IAU) + ROTATION propre
// (angle du méridien origine W(t)). Le pôle vient de `ephem::spin_axis_ecliptic`
// (repère écliptique J2000), converti en UE par le MÊME miroir y que les
// positions. Le miroir change la chiralité : une rotation directe en écliptique
// devient −W en UE — d'où le signe négatif, cohérent avec l'ancien yaw −θ.
// Fonction PURE de l'époque : déterministe, rejouable. [IAU WGCCRE]
FQuat OrientationAt(const FBodyDef& Def, double EpochTdb)
{
	using namespace fen::ephem;
	if (!has_orientation(Def.B))
	{
		// Repli : aucun élément IAU pour ce corps -> axe = normale écliptique et
		// rotation par la période sidérale (comportement historique, sans obliquité).
		if (Def.SpinH == 0.0) return FQuat::Identity;
		const double Tsid = Def.SpinH * 3600.0;
		const double Theta = UE_DOUBLE_TWO_PI * FMath::Frac(EpochTdb / Tsid);
		return FQuat(FVector::UpVector, -Theta);
	}
	// Pôle nord IAU en écliptique -> UE (miroir y), unitaire.
	const fen::Vec3 Ax = spin_axis_ecliptic(Def.B);
	const FVector AxisUe = FVector(Ax.x, -Ax.y, Ax.z).GetSafeNormal();
	// Inclinaison : amener +Z local (pôle du mesh) sur l'axe réel du corps.
	const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, AxisUe);
	// Rotation propre autour de ce pôle par l'angle du méridien origine W(t). Un
	// taux W négatif (Vénus, Uranus) donne naturellement une rotation rétrograde.
	const double Wdeg = prime_meridian_deg(Def.B, fen::Epoch{EpochTdb});
	const FQuat Spin = FQuat(AxisUe, -FMath::DegreesToRadians(Wdeg));
	return Spin * Tilt;
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
	// Soleil et marqueurs : émissifs (param vectoriel "Color" vérifié dans l'uasset).
	UMaterialInterface* EmissiveMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));

	for (const FBodyDef& Def : GBodies)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(MapActor);
		C->SetupAttachment(MapActor->GetRootComponent());
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);

		const double RadiusUU = BodyRadiusKm(Def.B);   // ÉCHELLE VRAIE
		double MeshRadius = 50.0;               // sphère moteur : 50 u de rayon
		const bool bStar = (Def.B == Body::Sun);
		if (UStaticMesh* Imported = FindImportedMesh(Def.Asset))
		{
			C->SetStaticMesh(Imported);
			MeshRadius = FMath::Max(1.0f, Imported->GetBounds().SphereRadius);
		}
		else if (Sphere)
		{
			C->SetStaticMesh(Sphere);
			if (!bStar && BaseMat)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
				M->SetVectorParameterValue(TEXT("Color"), Def.Color);
				C->SetMaterial(0, M);
			}
		}
		// LE SOLEIL est ÉMISSIF (auto-lumineux), qu'il vienne du mesh importé (dont le
		// matériau est ÉCLAIRÉ -> gris, car la lumière solaire est ailleurs) ou de la
		// sphère moteur. On force donc l'émissif sur TOUTES ses sections. Une étoile
		// ne doit pas dépendre d'un éclairage. [2026-07-26]
		if (bStar && EmissiveMat)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(EmissiveMat, C);
			M->SetVectorParameterValue(TEXT("Color"), Def.Color * 60.0f);
			const int32 NumMat = FMath::Max(1, C->GetNumMaterials());
			for (int32 s = 0; s < NumMat; ++s) C->SetMaterial(s, M);
		}
		C->SetWorldScale3D(FVector(RadiusUU / MeshRadius));
		C->RegisterComponent();
		MapActor->BodyMeshes.Add(C);
		BodyMeshRadius.Add(MeshRadius);   // l'échelle est refaite à chaque frame

		// Les anneaux de Saturne : nœud GLB séparé ("Circle"), attaché en enfant
		// du corps — ils suivent position, échelle et rotation propre.
		if (Def.B == Body::Saturn)
			if (UStaticMesh* Rings = FindImportedMeshNamed(Def.Asset, TEXT("Circle")))
			{
				UStaticMeshComponent* R = NewObject<UStaticMeshComponent>(MapActor);
				R->SetupAttachment(C);
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
	MapActor->SunLight->SetupAttachment(MapActor->GetRootComponent());
	MapActor->SunLight->SetMobility(EComponentMobility::Movable);
	MapActor->SunLight->bUseInverseSquaredFalloff = false;
	MapActor->SunLight->SetAttenuationRadius(6.0e14f);  // 6e9 km en u (échelle réelle) : au-delà de Neptune
	MapActor->SunLight->SetIntensity(12.0f);
	MapActor->SunLight->SetLightFalloffExponent(0.02f);
	MapActor->SunLight->SetCastShadows(false);
	MapActor->SunLight->RegisterComponent();

	// Marqueur du vaisseau en vol (position ESTIMÉE [GDD 7.5]).
	MapActor->VehicleMarker = NewObject<UStaticMeshComponent>(MapActor);
	MapActor->VehicleMarker->SetupAttachment(MapActor->GetRootComponent());
	MapActor->VehicleMarker->SetMobility(EComponentMobility::Movable);
	MapActor->VehicleMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MapActor->VehicleMarker->SetCastShadow(false);
	if (Sphere) MapActor->VehicleMarker->SetStaticMesh(Sphere);
	if (EmissiveMat)
	{
		UMaterialInstanceDynamic* M =
			UMaterialInstanceDynamic::Create(EmissiveMat, MapActor->VehicleMarker);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 1.0f, 0.9f) * 5.0f);
		MapActor->VehicleMarker->SetMaterial(0, M);
	}
	MapActor->VehicleMarker->SetVisibility(false);
	MapActor->VehicleMarker->RegisterComponent();

	// Marqueur de NOVELLUS [GDD v1.2 11.1, 17.3] : la station EST dans le monde
	// (orbite LEO). Bleu, façon losange de la référence (ref_issfocus.png).
	MapActor->StationMarker = NewObject<UStaticMeshComponent>(MapActor);
	MapActor->StationMarker->SetupAttachment(MapActor->GetRootComponent());
	MapActor->StationMarker->SetMobility(EComponentMobility::Movable);
	MapActor->StationMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MapActor->StationMarker->SetCastShadow(false);
	if (Sphere) MapActor->StationMarker->SetStaticMesh(Sphere);
	if (EmissiveMat)
	{
		UMaterialInstanceDynamic* M =
			UMaterialInstanceDynamic::Create(EmissiveMat, MapActor->StationMarker);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.3f, 0.7f, 1.0f) * 4.0f);
		MapActor->StationMarker->SetMaterial(0, M);
	}
	MapActor->StationMarker->SetVisibility(false);
	MapActor->StationMarker->RegisterComponent();

	// Marqueurs de flotte [GDD 8.3] : petits points émissifs à leur position VRAIE.
	const FLinearColor FleetCols[4] = {
		FLinearColor(0.45f, 0.75f, 1.0f) * 3.0f,   // relais GEO : bleu clair
		FLinearColor(1.0f, 0.55f, 0.25f) * 3.0f,   // orbiteurs Mars : orange
		FLinearColor(0.9f, 0.9f, 0.9f) * 3.0f,     // sondes lointaines : blanc
		FLinearColor(0.2f, 1.0f, 0.9f) * 5.0f,     // vol GEO en cours : cyan vif
	};
	for (int32 i = 0; i < FLEET_TOTAL; ++i)
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(MapActor);
		M->SetupAttachment(MapActor->GetRootComponent());
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
		M->SetVisibility(false);
		M->RegisterComponent();
		MapActor->FleetMarkers.Add(M);
	}

	// Caméra carte : posée à l'origine (le monde est rebasé sur l'œil à chaque
	// frame) ; seule sa ROTATION change.
	MapCamera = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (MapCamera && MapCamera->GetCameraComponent())
	{
		UCameraComponent* Cam = MapCamera->GetCameraComponent();
		Cam->SetConstraintAspectRatio(false);
		Cam->SetFieldOfView(45.0f);

		// ═══ L'IMAGE DE LA CARTE ═══
		// EXPOSITION FIGÉE À 1. L'auto-exposition cherche une luminance moyenne :
		// sur un ciel presque noir semé d'étoiles elle ouvrirait à fond et
		// laverait tout (le fond de la référence est un noir profond). Le mode
		// « Manual » ne conviendrait pas non plus : il dérive des réglages
		// d'appareil photo (f/4, 1/60 s, ISO 100 -> facteur ~0,09) et éteindrait
		// les étoiles. On borne donc la luminance de référence à 1 des deux
		// côtés : le rendu passe tel quel, sans correction.
		FPostProcessSettings& PP = Cam->PostProcessSettings;
		// Note : le projet active r.DefaultFeature.AutoExposure.
		// ExtendDefaultLuminanceRange, donc ces deux bornes sont des EV100 et
		// non des luminances — 0 EV100 = aucune correction.
		PP.bOverride_AutoExposureMinBrightness = true;
		PP.AutoExposureMinBrightness = 0.0f;
		PP.bOverride_AutoExposureMaxBrightness = true;
		PP.AutoExposureMaxBrightness = 0.0f;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = 0.0f;
		// BLOOM : le halo du Soleil et l'éclat des marqueurs émissifs. Seuil haut
		// pour que seules les sources vraiment lumineuses débordent — les étoiles
		// du fond doivent rester des points nets.
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.85f;
		PP.bOverride_BloomThreshold = true;
		PP.BloomThreshold = 1.0f;
		// Aucun flou de profondeur ni grain : la lisibilité prime [GDD 6.8].
		PP.bOverride_DepthOfFieldFocalDistance = true;
		PP.DepthOfFieldFocalDistance = 0.0f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.15f;
		// MOTION BLUR COUPÉ. Le monde est rebasé sur l'œil chaque frame : quand la
		// caméra tourne, ce sont les OBJETS que le moteur voit « bouger » (vecteurs
		// de mouvement énormes et faux), pas la caméra. Le motion blur les étale
		// alors en traînées (« les textures bavent »). Une carte façon NASA Eyes
		// n'a de toute façon aucun besoin de flou de mouvement.
		PP.bOverride_MotionBlurAmount = true;
		PP.MotionBlurAmount = 0.0f;
	}

	bBuilt = true;
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
		if (MapCamera) PC->SetViewTargetWithBlend(MapCamera, 0.0f);
		LastOrbitEpoch = -1.0e300;         // force le retracé des orbites
		bFocusPrimed = false;              // recadre sans animation parasite
		SmoothDistKm = -1.0;               // ... et sans vol parasite à l'entrée
	}
	else
	{
		if (ULineBatchComponent* LB =
		        W ? W->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) : nullptr)
			LB->Flush();
		// Ne rendre la caméra QUE si personne ne l'a déjà reprise : la station
		// s'active dans la même frame que la désactivation du décor du menu, et
		// l'ordre des Tick entre subsystems n'est pas garanti.
		if (PreviousViewTarget && PC->GetViewTarget() == MapCamera)
			PC->SetViewTargetWithBlend(PreviousViewTarget, 0.0f);
	}
}

// Le point VISÉ : le corps focalisé, ou le Soleil (vue système).
FVector USPSolarSystemSubsystem::FocusWorldKm(double EpochTdb) const
{
	const int Focus = fen::app::g_render_bridge.focus_body.load();
	// NOVELLUS : pas un corps du catalogue, mais focalisable. Sa position monde =
	// Terre + offset LEO publié par le jeu [GDD v1.2 11.1, 17.3].
	if (Focus == fen::app::FOCUS_STATION)
	{
		const auto& St = fen::app::g_render_bridge.station;
		if (St.valid.load())
		{
			const FBodyDef* Terre = FindDef(Body::EarthBary);
			const FVector TerrePos = Terre ? BodyWorldKm(*Terre, EpochTdb) : FVector::ZeroVector;
			return TerrePos + EclToUeKmd(St.rel_m[0], St.rel_m[1], St.rel_m[2]);
		}
		return FVector::ZeroVector;
	}
	if (Focus < 0 || Focus >= static_cast<int>(Body::COUNT)) return FVector::ZeroVector;
	const FBodyDef* Def = FindDef(static_cast<Body>(Focus));
	return Def ? BodyWorldKm(*Def, EpochTdb) : FVector::ZeroVector;
}

// Charge le modèle ISS EXTÉRIEUR (multi-mesh NANITE) à l'échelle RÉELLE : le mesh
// est déjà en cm réels (~109 m = 10 900 u), donc échelle ~1.0 et Nanite fonctionne
// normalement. Rattaché au MapActor sous ExtRoot ; UpdateScene le positionne
// caméra-relatif, visible de près (LOD).
void USPSolarSystemSubsystem::BuildExteriorStation()
{
	bExtBuilt = true;                     // une seule tentative (succès ou non)
	UWorld* W = GetWorld();
	if (!W || !MapActor) return;

	const FAssetRegistryModule& ARM =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByPath(
		FName(TEXT("/Game/ISS/Exterior/ISS_stationary/StaticMeshes")), Assets, true);

	TArray<UStaticMesh*> Meshes;
	FBox Bounds(ForceInit);
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName()) continue;
		UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset());
		if (!M) continue;
		Meshes.Add(M);
		const FBoxSphereBounds B = M->GetBounds();
		Bounds += FBox(B.Origin - B.BoxExtent, B.Origin + B.BoxExtent);
	}
	if (Meshes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
		       TEXT("[SPSolarSystem] aucun mesh ISS exterieur sous /Game/ISS/Exterior/ISS_stationary/StaticMeshes"));
		return;
	}

	const FVector Size = Bounds.GetSize();
	const double SpanUU = FMath::Max3(Size.X, Size.Y, Size.Z);
	const double EnvM = 109.0;             // envergure RÉELLE de l'ISS (~109 m)
	// envergure réelle -> u (1 u = 1 cm) : 109 m = 10 900 u. Le mesh est déjà en
	// cm réels (span ~10 829 u = 108 m), donc l'échelle vaut ~1.0 (normalisée ici).
	ExtScaleKm = (SpanUU > 1.0) ? ((EnvM * 100.0) / SpanUU) : 1.0;
	ExtCentreUU = Bounds.GetCenter();

	// ExtRoot : composant enfant du MapActor (dont le rendu est déjà PROUVÉ), pas
	// un acteur séparé.
	ExtRoot = NewObject<USceneComponent>(MapActor, TEXT("ExtStationRoot"));
	ExtRoot->SetupAttachment(MapActor->GetRootComponent());
	ExtRoot->SetMobility(EComponentMobility::Movable);
	ExtRoot->RegisterComponent();

	for (UStaticMesh* M : Meshes)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(MapActor);
		C->SetupAttachment(ExtRoot);       // AVANT RegisterComponent (piège #4)
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		// NANITE RÉACTIVÉ : à l'échelle réelle (ISS ~10 900 u) Nanite fonctionne
		// normalement — plus besoin du repli forcé. C'était TOUTE la difficulté du
		// micro-échelle, supprimée à la racine par le rendu à échelle réelle.
		// Pas de contribution Lumen : évite la préparation des champs de distance
		// de centaines de pièces (qui bloque le rendu, très visible en headless).
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		// Matériaux RÉELS conservés (ISS texturée) : éclairée par le Soleil de la
		// carte + une lumière d'appoint locale (ExtLight), comme ref_issfocus.png.
		C->RegisterComponent();
		ExtParts.Add(C);
	}
	ExtRoot->SetVisibility(false, true);   // caché jusqu'à l'approche (propage aux enfants)

	// Lumière d'appoint (pour un futur rendu en matériaux réels), sur le MapActor
	// (repère NON scalé) : UpdateScene la place sur Novellus, allumée avec le modèle.
	if (MapActor)
	{
		ExtLight = NewObject<UPointLightComponent>(MapActor);
		ExtLight->SetupAttachment(MapActor->GetRootComponent());
		ExtLight->SetMobility(EComponentMobility::Movable);
		ExtLight->bUseInverseSquaredFalloff = false;
		ExtLight->SetAttenuationRadius(2.0e5f);   // 2 km en u : couvre les ~109 m avec marge
		ExtLight->SetLightFalloffExponent(0.5f);
		ExtLight->SetIntensity(60.0f);
		ExtLight->SetLightColor(FLinearColor(1.0f, 0.98f, 0.95f));
		ExtLight->SetCastShadows(false);
		ExtLight->SetVisibility(false);
		ExtLight->RegisterComponent();
	}

	UE_LOG(LogTemp, Log,
	       TEXT("[SPSolarSystem] ISS exterieur : %d meshes (Nanite, echelle reelle), span %.1f u -> %.0f m (x%.6g)"),
	       Meshes.Num(), SpanUU, EnvM, ExtScaleKm);
}

// Place TOUT ce qui est monde, rebasé sur l'œil.
void USPSolarSystemSubsystem::UpdateScene(double EpochTdb, const FVector& CamWorldKm)
{
	auto& Bridge = fen::app::g_render_bridge;
	const bool bMoons = Bridge.show_moons.load();
	// R(P) : monde (km) -> rendu (l'œil est l'origine). Position de COMPOSANT :
	// bornée radialement à RENDER_MAX_UU pour rester sous le plafond de précision
	// GPU d'UE (cf. RENDER_MAX_UU). Même direction depuis l'œil -> même position
	// écran ; seule la profondeur change. Les lignes d'orbite ont leur R propre.
	auto R = [&CamWorldKm](const FVector& WorldKm) {
		const FVector P = CompressKm(WorldKm - CamWorldKm);
		const double M = P.Size();
		return (M > RENDER_MAX_UU) ? P * (RENDER_MAX_UU / M) : P;
	};
	// Taille d'un marqueur pour qu'il garde une taille écran constante.
	auto MarkerScale = [](const FVector& Rendered) {
		return FMath::Max(1.0e-6, Rendered.Size() * MARKER_ANG) / 50.0;   // sphère = 50 u
	};

	// --- les corps : GÉOMÉTRIE de PRÈS, sinon MARQUEUR HUD -------------------
	// À l'échelle réelle un corps lointain est à ~1e13-1e14 u, où le float perd
	// des centaines de km -> facettes, corps délavés. On ne rend sa GÉOMÉTRIE
	// que s'il est assez proche pour être GROS (donc précis) ; sinon caché, le
	// HUD le montre au marqueur (projeté en double). [pas de compression]
	for (int32 i = 0; i < NUM_BODIES && i < MapActor->BodyMeshes.Num(); ++i)
	{
		const FBodyDef& Def = GBodies[i];
		UStaticMeshComponent* C = MapActor->BodyMeshes[i];
		const FVector RelKm = BodyWorldKm(Def, EpochTdb) - CamWorldKm;
		const double DistKm = RelKm.Size();
		const bool bGeom = DistKm < BodyRadiusKm(Def.B) * BODY_GEOM_FACTOR;
		// PROPAGE AUX ENFANTS (bPropagateToChildren=true) : sinon les anneaux de
		// Saturne (mesh enfant) restent visibles quand le corps est caché. Et comme
		// Saturne est toujours loin -> jamais affiché -> sa transform reste celle,
		// initiale, de BuildScene (origine + échelle énorme) : l'anneau devient un
		// disque géant centré sur l'œil, vu par la tranche = un trait blanc en
		// grand cercle balayant le ciel. [bug du "trait" traqué le 2026-07-26]
		if ((Def.bMoon && !bMoons) || !bGeom) { C->SetVisibility(false, true); continue; }
		C->SetVisibility(true, true);
		// rotation AVANT la position : SetWorldRotation ne touche pas la
		// translation, et les enfants (anneaux de Saturne) suivent.
		C->SetWorldRotation(OrientationAt(Def, EpochTdb));
		// Le rayon suit la MÊME homothétie que la position : la taille angulaire
		// vue de l'œil reste donc exactement la vraie.
		double Fac = 1.0;
		const FVector P = CompressKm(RelKm, &Fac);
		C->SetWorldLocation(P);
		const double MeshR = BodyMeshRadius.IsValidIndex(i) ? BodyMeshRadius[i] : 50.0;
		C->SetWorldScale3D(FVector(BodyRadiusKm(Def.B) * Fac / MeshR));
	}

	// --- le Soleil éclaire depuis sa position (rebasée) ----------------------
	if (MapActor->SunLight) MapActor->SunLight->SetWorldLocation(R(FVector::ZeroVector));

	// --- vaisseau en vol interplanétaire : position ESTIMÉE [GDD 7.5] --------
	const bool bVehicle = Bridge.vehicle.valid.load();
	if (MapActor->VehicleMarker)
	{
		MapActor->VehicleMarker->SetVisibility(bVehicle);
		if (bVehicle)
		{
			const FVector P = R(EclToUeKmd(Bridge.vehicle.pos_m[0], Bridge.vehicle.pos_m[1],
			                               Bridge.vehicle.pos_m[2]));
			MapActor->VehicleMarker->SetWorldLocation(P);
			MapActor->VehicleMarker->SetWorldScale3D(FVector(MarkerScale(P)));
		}
	}

	// --- NOVELLUS dans le monde [GDD v1.2 11.1, 17.3, 17.4] : LOD PAR TAILLE
	// APPARENTE. De loin, un MARQUEUR (comme la flotte) ; de près, le vrai MODÈLE
	// EXTÉRIEUR à l'échelle réelle (55 m), chargé à la demande. Les deux à la
	// position LEO réelle (Terre + offset publié), en caméra-relatif.
	{
		const auto& St = Bridge.station;
		const bool bStation = St.valid.load();
		const FBodyDef* Terre = FindDef(Body::EarthBary);
		const FVector TerrePos = Terre ? BodyWorldKm(*Terre, EpochTdb) : FVector::ZeroVector;
		const FVector NovWorld = TerrePos + EclToUeKmd(St.rel_m[0], St.rel_m[1], St.rel_m[2]);
		const FVector Pnov = R(NovWorld);
		// bascule marqueur -> modèle quand l'envergure (55 m) dépasse ~quelques px
		// (taille angulaire = envergure / distance de vue).
		const double DistNov = FMath::Max(1.0e-6, (NovWorld - CamWorldKm).Size());
		const bool bModel = bStation && ((St.envergure_m * 0.001) / DistNov > 3.0e-3);

		if (MapActor->StationMarker)
		{
			MapActor->StationMarker->SetVisibility(bStation && !bModel);
			if (bStation && !bModel)
			{
				MapActor->StationMarker->SetWorldLocation(Pnov);
				MapActor->StationMarker->SetWorldScale3D(FVector(MarkerScale(Pnov)));
			}
		}

		// Le modèle est construit tôt (Tick, dès la carte active) pour que ses
		// shaders soient chauds ; ici on ne fait que le MONTRER/cacher (LOD) et le
		// placer, caméra-relatif.
		if (ExtRoot)
		{
			ExtRoot->SetVisibility(bModel, true);   // propage aux pièces
			if (bModel)
			{
				// centre du modèle amené sur Novellus ; échelle réelle (km/u).
				ExtRoot->SetWorldScale3D(FVector(ExtScaleKm));
				ExtRoot->SetWorldLocation(Pnov - ExtCentreUU * ExtScaleKm);
			}
		}
		if (ExtLight)
		{
			ExtLight->SetVisibility(bModel);
			if (bModel) ExtLight->SetWorldLocation(Pnov);
		}
	}

	// --- la flotte en service [GDD 8.3] : position VRAIE de chaque engin ------
	// À l'échelle vraie, plus besoin d'anneaux symboliques ni de rayon plancher :
	// un relais GEO est à 42 164 km de la Terre, et il y EST.
	{
		const auto& F = Bridge.fleet;
		const int32 NCraft =
			FMath::Min(F.n.load(), fen::app::RenderBridge::FleetSnap::MAX);
		const int32 Base[3] = {FLEET_GEO0, FLEET_MARS0, FLEET_PROBE0};
		int32 Used[3] = {0, 0, 0};
		for (int32 i = 0; i < NCraft; ++i)
		{
			const auto& Craft = F.craft[i];
			const int32 Cat = FMath::Clamp(Craft.type, 0, 2);
			if (Used[Cat] >= FLEET_PER_CAT) continue;
			UStaticMeshComponent* M = MapActor->FleetMarkers[Base[Cat] + Used[Cat]++];
			const FBodyDef* Parent =
				(Craft.parent >= 0 && Craft.parent < static_cast<int>(Body::COUNT))
					? FindDef(static_cast<Body>(Craft.parent)) : nullptr;
			const FVector ParentPos =
				Parent ? BodyWorldKm(*Parent, EpochTdb) : FVector::ZeroVector;
			const FVector P = R(ParentPos + EclToUeKmd(Craft.rel_m[0], Craft.rel_m[1],
			                                           Craft.rel_m[2]));
			M->SetVisibility(true);
			M->SetWorldLocation(P);
			M->SetWorldScale3D(FVector(MarkerScale(P)));
		}
		for (int32 Cat = 0; Cat < 3; ++Cat)
			for (int32 k = Used[Cat]; k < FLEET_PER_CAT; ++k)
				MapActor->FleetMarkers[Base[Cat] + k]->SetVisibility(false);

		// vol GEO en cours : la trace publiée est géocentrique (km) — l'estimé
		// est placé à sa VRAIE position autour de la Terre.
		UStaticMeshComponent* GeoM = MapActor->FleetMarkers[FLEET_GEOFLIGHT];
		const bool bGeo = Bridge.geo.valid.load();
		GeoM->SetVisibility(bGeo);
		if (bGeo)
		{
			const FBodyDef* Terre = FindDef(Body::EarthBary);
			const FVector TerrePos = Terre ? BodyWorldKm(*Terre, EpochTdb) : FVector::ZeroVector;
			const FVector P = R(TerrePos + FVector(Bridge.geo.pos_km[0], -Bridge.geo.pos_km[1],
			                                       Bridge.geo.pos_km[2]));
			GeoM->SetWorldLocation(P);
			GeoM->SetWorldScale3D(FVector(MarkerScale(P)));
		}
	}
}

// Échantillonne les orbites via l'ÉPHÉMÉRIDE (la vérité), en km monde absolus.
// Coûteux -> appelé seulement quand l'époque dérive ou que les options changent.
void USPSolarSystemSubsystem::RebuildOrbitCache(double EpochTdb)
{
	OrbitCache.Reset();
	const bool bMoons = fen::app::g_render_bridge.show_moons.load();
	for (const FBodyDef& Def : GBodies)
	{
		if (Def.B == Body::Sun) continue;
		if (Def.bMoon && !bMoons) continue;
		const double T = OrbitalPeriodS(Def, EpochTdb);
		if (T <= 0.0) continue;

		// Trace = la trajectoire RÉELLE échantillonnée sur une période via
		// l'éphéméride (pas une ellipse idéalisée). Lunes : autour de la position
		// COURANTE de leur planète, à leur distance RÉELLE.
		const FBodyDef* Parent = FindDef(Def.Parent);
		const FVector ParentNow =
			(Def.bMoon && Parent) ? BodyWorldKm(*Parent, EpochTdb) : FVector::ZeroVector;

		FOrbitCache Entry;
		Entry.Color = FLinearColor(Def.Color.R, Def.Color.G, Def.Color.B, 0.35f);
		Entry.PointsKm.Reserve(ORBIT_SAMPLES + 1);
		for (int32 k = 0; k <= ORBIT_SAMPLES; ++k)
		{
			const double Tk = EpochTdb + (T * k) / ORBIT_SAMPLES;
			const fen::Vec3 Rel = GEph.state(Def.B, Def.bMoon ? Def.Parent : Body::Sun,
			                                 fen::Epoch{Tk}).r;
			Entry.PointsKm.Add(Def.bMoon ? ParentNow + EclToUeKm(Rel) : EclToUeKm(Rel));
		}
		OrbitCache.Add(MoveTemp(Entry));
	}
}

// Ré-émet toutes les polylignes dans le repère rebasé sur l'œil (chaque frame).
void USPSolarSystemSubsystem::EmitOrbits(const FVector& CamWorldKm, double CamDistKm,
                                         double EpochTdb)
{
	UWorld* W = GetWorld();
	ULineBatchComponent* LB =
		W ? W->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) : nullptr;
	if (!LB) return;
	LB->Flush();

	// Même compression que les corps : les traits passent donc exactement par eux
	// à l'écran (la compression est radiale, elle ne change aucune direction).
	auto R = [&CamWorldKm](const FVector& WorldKm) {
		return CompressKm(WorldKm - CamWorldKm);
	};
	// Épaisseur ANGULAIRE : à l'échelle vraie, une épaisseur fixe en km serait
	// soit invisible (vue système) soit énorme (vue rapprochée). On la lie à la
	// distance de l'œil pour garder ~2 px — dans l'espace COMPRIMÉ, où vivent
	// les traits.
	const double CamDistRender = CamDistKm * UU_PER_KM;   // échelle réelle (cm)
	const float Thick = static_cast<float>(FMath::Max(1.0e-4, CamDistRender * 0.0012));

	// DÉCOR DU MENU : les mêmes orbites, mais en retrait — dans la référence
	// (ref_menu.png) ce sont de simples cercles à la limite du visible.
	const bool bDecor = fen::app::g_render_bridge.menu_backdrop.load() &&
	                    !fen::app::g_render_bridge.carte3d_active.load();
	// FONDU EN VUE RASANTE. Les orbites sont ~coplanaires (écliptique) : vues par la
	// tranche, elles s'écrasent en UNE ligne dure traversant tout l'écran et le corps
	// focalisé. On atténue leur opacité quand l'élévation de l'œil au-dessus du plan
	// tombe vers zéro (façon Eyes on the Solar System). Élévation ~ |sin(pitch)| :
	// pitch = 0 -> œil DANS le plan -> fondu quasi total ; pitch fort -> vue de dessus
	// -> plein. Bande de transition ~1°..9° d'élévation, courbe smoothstep.
	const double Pitch = fen::app::g_render_bridge.cam.pitch.load();
	const double Elev  = FMath::Abs(FMath::Sin(Pitch));
	const double GrazeT = FMath::Clamp((Elev - 0.02) / 0.14, 0.0, 1.0);
	const float  Graze  = static_cast<float>(GrazeT * GrazeT * (3.0 - 2.0 * GrazeT));
	const float Att = (bDecor ? 0.40f : 1.0f) * Graze;

	for (const FOrbitCache& O : OrbitCache)
	{
		const FLinearColor Col(O.Color.R, O.Color.G, O.Color.B, O.Color.A * Att);
		for (int32 k = 1; k < O.PointsKm.Num(); ++k)
			LB->DrawLine(R(O.PointsKm[k - 1]), R(O.PointsKm[k]), Col, SDPG_World, Thick, 0.0f);
	}
	if (bDecor) return;   // pas de trajectoire ni de flotte derrière le menu

	// Vol interplanétaire [GDD 8.3] : le rendu TRACE ce que l'écran publie —
	// trajectoire NOMINALE, corridor d'incertitude 3σ autour de la position
	// ESTIMÉE (échelle VRAIE : aucune inflation), nœuds de manœuvre TCM.
	const auto& V = fen::app::g_render_bridge.vehicle;
	if (V.valid.load() && V.n >= 2)
	{
		const FLinearColor Jaune(1.0f, 0.85f, 0.2f, 0.9f * Graze);
		for (int32 k = 1; k < V.n; ++k)
			LB->DrawLine(R(EclToUeKmd(V.traj_m[k - 1][0], V.traj_m[k - 1][1], V.traj_m[k - 1][2])),
			             R(EclToUeKmd(V.traj_m[k][0], V.traj_m[k][1], V.traj_m[k][2])),
			             Jaune, SDPG_World, Thick * 1.4f, 0.0f);

		// corridor 3σ : cercle dans le plan écliptique autour de l'estimé
		const FVector Centre = R(EclToUeKmd(V.pos_m[0], V.pos_m[1], V.pos_m[2]));
		const double RayonKm = V.corridor_3s_m * KM_PER_M;
		if (RayonKm > 1.0)
		{
			const FLinearColor Orange(1.0f, 0.6f, 0.2f, 0.55f * Graze);
			constexpr int32 SEG = 48;
			FVector Prev = Centre + FVector(RayonKm, 0, 0);
			for (int32 k = 1; k <= SEG; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * k / SEG;
				const FVector P = Centre +
					FVector(RayonKm * FMath::Cos(A), RayonKm * FMath::Sin(A), 0.0);
				LB->DrawLine(Prev, P, Orange, SDPG_World, Thick, 0.0f);
				Prev = P;
			}
		}

		// nœuds de manœuvre : croix (orange = à faire, grise = faite)
		for (int32 k = 0; k < V.n_nodes && k < 2; ++k)
		{
			const FVector N = R(EclToUeKmd(V.nodes_m[k][0], V.nodes_m[k][1], V.nodes_m[k][2]));
			const double S = FMath::Max(1.0, N.Size() * MARKER_ANG * 2.0);
			const FLinearColor Col = V.node_done[k] ? FLinearColor(0.6f, 0.6f, 0.6f, 0.8f)
			                                        : FLinearColor(1.0f, 0.55f, 0.15f, 1.0f);
			LB->DrawLine(N - FVector(S, 0, 0), N + FVector(S, 0, 0), Col, SDPG_World, Thick * 1.6f, 0.0f);
			LB->DrawLine(N - FVector(0, S, 0), N + FVector(0, S, 0), Col, SDPG_World, Thick * 1.6f, 0.0f);
			LB->DrawLine(N - FVector(0, 0, S), N + FVector(0, 0, S), Col, SDPG_World, Thick * 1.6f, 0.0f);
		}
	}

	// Vol GEO [GDD 8.3] : orbite CIBLE (nominale) + trace ESTIMÉE, à l'échelle
	// vraie autour de la Terre — plus besoin d'une scène rapprochée séparée.
	const auto& G = fen::app::g_render_bridge.geo;
	if (G.valid.load())
	{
		const FBodyDef* Terre = FindDef(Body::EarthBary);
		const FVector TerrePos = Terre ? BodyWorldKm(*Terre, EpochTdb) : FVector::ZeroVector;
		auto GeoToWorld = [&TerrePos](double Xkm, double Ykm, double Zkm) {
			return TerrePos + FVector(Xkm, -Ykm, Zkm);
		};
		// anneau VERT = orbite cible du contrat (la référence nominale)
		const double Sma = G.target_sma_km;
		if (Sma > 1.0)
		{
			const FLinearColor Vert(0.35f, 0.95f, 0.45f, 0.8f * Graze);
			constexpr int32 SEG = 96;
			FVector Prev = R(GeoToWorld(Sma, 0.0, 0.0));
			for (int32 k = 1; k <= SEG; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * k / SEG;
				const FVector P = R(GeoToWorld(Sma * FMath::Cos(A), Sma * FMath::Sin(A), 0.0));
				LB->DrawLine(Prev, P, Vert, SDPG_World, Thick, 0.0f);
				Prev = P;
			}
		}
		// trace CYAN = solution de navigation (ESTIMÉE, jamais la vérité)
		if (G.n >= 2)
		{
			const FLinearColor Cyan(0.3f, 0.9f, 1.0f, 0.9f * Graze);
			FVector Prev = R(GeoToWorld(G.traj_km[0][0], G.traj_km[0][1], G.traj_km[0][2]));
			for (int32 k = 1; k < G.n; ++k)
			{
				const FVector P = R(GeoToWorld(G.traj_km[k][0], G.traj_km[k][1], G.traj_km[k][2]));
				LB->DrawLine(Prev, P, Cyan, SDPG_World, Thick, 0.0f);
				Prev = P;
			}
		}
	}
}

// Projette les corps à l'écran pour le HUD (marqueurs, libellés, picking).
// Coordonnées NORMALISÉES : le HUD ne dépend pas de la taille du viewport.
void USPSolarSystemSubsystem::PublishScreen(const FVector& CamWorldKm, const FRotator& CamRot,
                                            double FovDeg)
{
	auto& S = fen::app::g_render_bridge.screen;
	double Aspect = 16.0 / 9.0;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D VP;
		GEngine->GameViewport->GetViewportSize(VP);
		if (VP.X > 1.0 && VP.Y > 1.0) Aspect = VP.X / VP.Y;
	}
	const double TanH = FMath::Tan(FMath::DegreesToRadians(FovDeg) * 0.5);   // FOV horizontal
	const double TanV = TanH / Aspect;
	const double Epoch = fen::app::g_render_bridge.epoch_tdb.load();
	const bool bMoons = fen::app::g_render_bridge.show_moons.load();

	int32 N = 0;
	for (const FBodyDef& Def : GBodies)
	{
		if (N >= fen::app::RenderBridge::ScreenBodies::MAX) break;
		if (Def.bMoon && !bMoons) continue;
		const FVector Rel = BodyWorldKm(Def, Epoch) - CamWorldKm;
		const FVector Local = CamRot.UnrotateVector(Rel);   // X avant, Y droite, Z haut
		auto& It = S.items[N];
		It.body = static_cast<int>(Def.B);
		It.dist_km = Rel.Size();
		if (Local.X <= 1.0)
		{
			It.on_screen = 0; It.nx = It.ny = -1.0f; It.r_norm = 0.0f;
			++N; continue;
		}
		const double Nx = 0.5 + 0.5 * (Local.Y / Local.X) / TanH;
		const double Ny = 0.5 - 0.5 * (Local.Z / Local.X) / TanV;
		It.nx = static_cast<float>(Nx);
		It.ny = static_cast<float>(Ny);
		// rayon apparent en fraction de la LARGEUR d'écran
		It.r_norm = static_cast<float>(0.5 * (BodyRadiusKm(Def.B) / Local.X) / TanH);
		It.on_screen = (Nx > -0.1 && Nx < 1.1 && Ny > -0.1 && Ny < 1.1) ? 1 : 0;
		++N;
	}

	// NOVELLUS dans la liste écran : focalisable/cliquable comme un corps
	// [GDD v1.2 11.1]. Rayon apparent tiré de l'envergure (55 m) -> minuscule de
	// loin, le HUD le désigne alors au marqueur (comme un satellite).
	{
		const auto& St = fen::app::g_render_bridge.station;
		if (St.valid.load() && N < fen::app::RenderBridge::ScreenBodies::MAX)
		{
			const FBodyDef* Terre = FindDef(Body::EarthBary);
			const FVector TerrePos = Terre ? BodyWorldKm(*Terre, Epoch) : FVector::ZeroVector;
			const FVector Rel =
				(TerrePos + EclToUeKmd(St.rel_m[0], St.rel_m[1], St.rel_m[2])) - CamWorldKm;
			const FVector Local = CamRot.UnrotateVector(Rel);
			auto& It = S.items[N];
			It.body = fen::app::FOCUS_STATION;
			It.dist_km = Rel.Size();
			if (Local.X <= 1.0)
			{
				It.on_screen = 0; It.nx = It.ny = -1.0f; It.r_norm = 0.0f;
			}
			else
			{
				const double Nx = 0.5 + 0.5 * (Local.Y / Local.X) / TanH;
				const double Ny = 0.5 - 0.5 * (Local.Z / Local.X) / TanV;
				It.nx = static_cast<float>(Nx);
				It.ny = static_cast<float>(Ny);
				It.r_norm = static_cast<float>(0.5 * ((St.envergure_m * 0.001) / Local.X) / TanH);
				It.on_screen = (Nx > -0.1 && Nx < 1.1 && Ny > -0.1 && Ny < 1.1) ? 1 : 0;
			}
			++N;
		}
	}
	S.n = N;
}

void USPSolarSystemSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	auto& Bridge = fen::app::g_render_bridge;
	// La carte sert AUSSI de décor au menu (ciel étoilé + orbites ténues,
	// cf. ref_menu.png) : elle s'active donc dans les deux cas, la scène Titre
	// n'étant qu'une version en retrait de la même vue.
	const bool bActive = Bridge.carte3d_active.load() || Bridge.menu_backdrop.load();

	if (bActive && !bBuilt) BuildScene();
	// Novellus vu de près : on charge son modèle extérieur DÈS que la carte est
	// active (pas à l'approche), pour que ses shaders soient chauds au moment où
	// le LOD le montre. Reste caché tant qu'on n'est pas assez proche. bBuilt =>
	// MapActor existe (ExtRoot s'y rattache).
	if (bActive && bBuilt && !bExtBuilt) BuildExteriorStation();
	if (bActive != bWasActive) { SetMapActive(bActive); bWasActive = bActive; }
	if (!bActive || !bBuilt) return;

	const double Epoch = Bridge.epoch_tdb.load();

	// ═══ LE VOL VERS UN CORPS [plan §6.4] ═══
	// Cliquer un corps le focalise ET demande un nouveau cadrage. Si l'œil s'y
	// téléportait, on ne comprendrait pas où l'on va : on approche donc la
	// distance de vue en douceur. L'interpolation est LOGARITHMIQUE — du rayon
	// d'une planète à la ceinture de Kuiper il y a neuf ordres de grandeur, et
	// un lissage linéaire passerait presque tout le trajet sans rien changer de
	// visible. Constante de temps unique : un pas de molette paraît immédiat,
	// un changement de focus devient un vol.
	{
		const double Cible = FMath::Max(1.0, Bridge.cam.dist_km.load());
		if (SmoothDistKm <= 0.0) SmoothDistKm = Cible;
		else
		{
			constexpr double TAU = 0.35;                       // s
			const double K = 1.0 - FMath::Exp(-DeltaTime / TAU);
			const double L = FMath::Loge(SmoothDistKm);
			SmoothDistKm = FMath::Exp(L + (FMath::Loge(Cible) - L) * K);
		}
	}

	// --- la caméra : commandée par le HUD, appliquée ici ---------------------
	// Le focus « vole » vers sa cible (façon NASA Eyes) : on lisse le point visé,
	// puis on colle dès que l'écart est négligeable devant la distance de vue —
	// sinon le suivi d'un corps en mouvement traînerait indéfiniment.
	const FVector TargetFocus = FocusWorldKm(Epoch);
	if (!bFocusPrimed) { SmoothFocusKm = TargetFocus; bFocusPrimed = true; }
	else
	{
		const double Gap = FVector::Dist(SmoothFocusKm, TargetFocus);
		if (Gap < SmoothDistKm * 1.0e-4) SmoothFocusKm = TargetFocus;
		else SmoothFocusKm = FMath::VInterpTo(SmoothFocusKm, TargetFocus, DeltaTime, 3.5f);
	}

	const double DistKm = FMath::Max(1.0, SmoothDistKm);
	const double Yaw = Bridge.cam.yaw.load();
	const double Pitch = FMath::Clamp(Bridge.cam.pitch.load(), -1.55, 1.55);
	const double FovDeg = FMath::Clamp(Bridge.cam.fov_deg.load(), 10.0, 100.0);
	const FVector Offset(DistKm * FMath::Cos(Pitch) * FMath::Cos(Yaw),
	                     DistKm * FMath::Cos(Pitch) * FMath::Sin(Yaw),
	                     DistKm * FMath::Sin(Pitch));
	const FVector CamWorldKm = SmoothFocusKm + Offset;
	// la caméra regarde le point visé : depuis l'origine de rendu, c'est −Offset
	const FRotator CamRot = (-Offset).Rotation();

	// PLAN DE CLIPPING PROCHE, ADAPTATIF. À l'échelle réelle (u = cm), la distance
	// de vue en u = DistKm * UU_PER_KM. On accroche le near-clip à ~1/2000 de la
	// distance regardée (précision z-buffer ~1e-4 sur l'objet visé), borné pour
	// rester utilisable du plan vaisseau (cm) au plan système.
	{
		// Plafonné : en vue système il n'y a plus de géométrie proche (les corps
		// lointains sont des marqueurs), donc un near-clip modeste suffit et
		// garantit que le dôme de ciel (adaptatif, >= 1e8 u) n'est jamais clippé.
		const double Near = FMath::Clamp(DistKm * UU_PER_KM / 2000.0, 1.0, 1.0e7);
		if (LastNearClip <= 0.0 || FMath::Abs(Near - LastNearClip) > LastNearClip * 0.15)
		{
			if (IConsoleVariable* CVar =
			        IConsoleManager::Get().FindConsoleVariable(TEXT("r.SetNearClipPlane")))
				CVar->Set(static_cast<float>(Near));
			LastNearClip = Near;
		}
	}

	UpdateScene(Epoch, CamWorldKm);

	if (MapCamera)
	{
		MapCamera->SetActorLocationAndRotation(FVector::ZeroVector, CamRot);
		if (UCameraComponent* Cam = MapCamera->GetCameraComponent())
			Cam->SetFieldOfView(static_cast<float>(FovDeg));

		// RÉ-ASSURE LA CIBLE DE VUE CHAQUE FRAME. SetMapActive ne la pose qu'à la
		// TRANSITION ; or en PIE il n'y a pas de pawn par défaut (DefaultPawnClass =
		// nullptr), et le PlayerController peut reprendre la vue sur un défaut (lui-
		// même : mauvaise rotation/FOV/near). Le rendu se fait alors via une caméra
		// aberrante -> système solaire minuscule et décalé + dôme de ciel plein écran
		// (« nébuleuse »), alors que le HUD, lui, projette bien sur MapCamera (labels
		// au bon endroit). Symptôme visible en PIE, pas en -game (timing différent).
		if (UWorld* W = GetWorld())
			if (APlayerController* PC = W->GetFirstPlayerController())
				if (PC->GetViewTarget() != MapCamera)
				{
					if (!bViewTargetReasserted)
					{
						UE_LOG(LogTemp, Warning,
						       TEXT("[SPSolarSystem] vue rendue via '%s' (pas MapCamera) -> ré-assignation. "
						            "PIE sans pawn : la carte impose sa caméra chaque frame."),
						       *GetNameSafe(PC->GetViewTarget()));
						bViewTargetReasserted = true;
					}
					PC->SetViewTarget(MapCamera);
				}
	}

	// Orbites : l'échantillonnage (éphéméride) est mis en cache et ne se refait
	// qu'en cas de dérive d'époque ou de changement d'option ; l'ÉMISSION, elle,
	// a lieu chaque frame — les traits vivent dans le repère rebasé sur l'œil.
	const bool bMoonsNow = Bridge.show_moons.load();
	if (FMath::Abs(Epoch - LastOrbitEpoch) > ORBIT_REDRAW_DAYS * 86400.0 ||
	    bMoonsNow != bLastShowMoons || OrbitCache.Num() == 0)
	{
		RebuildOrbitCache(Epoch);
		LastOrbitEpoch = Epoch;
		bLastShowMoons = bMoonsNow;
	}
	EmitOrbits(CamWorldKm, DistKm, Epoch);

	PublishScreen(CamWorldKm, CamRot, FovDeg);
}
