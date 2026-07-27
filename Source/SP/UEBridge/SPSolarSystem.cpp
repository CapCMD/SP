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

#include "SPCameraPost.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
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

// ORDRE : chaque lune SUIT son parent. Ce n'est pas cosmétique — la règle de
// séparabilité écran (piège n°41) teste une lune contre l'item déjà publié de son
// parent : il doit donc être calculé avant. `SpinH` d'une lune = sa période
// ORBITALE (rotation synchrone : toutes ces lunes présentent la même face à leur
// planète — c'est un FAIT physique, pas un réglage), négatif si rétrograde.
const FBodyDef GBodies[] = {
	{Body::Sun,      Body::Sun,      TEXT("Sun"),     FLinearColor(1.00f, 0.85f, 0.30f), false,   609.12},
	{Body::Mercury,  Body::Sun,      TEXT("Mercury"), FLinearColor(0.55f, 0.50f, 0.45f), false,  1407.6},
	{Body::Venus,    Body::Sun,      TEXT("Venus"),   FLinearColor(0.90f, 0.75f, 0.50f), false, -5832.5},
	{Body::EarthBary,Body::Sun,      TEXT("Earth"),   FLinearColor(0.25f, 0.45f, 0.90f), false,    23.9345},
	{Body::Moon,     Body::EarthBary,TEXT("Moon"),    FLinearColor(0.62f, 0.62f, 0.62f), true,    655.72},
	{Body::Mars,     Body::Sun,      TEXT("Mars"),    FLinearColor(0.85f, 0.45f, 0.25f), false,    24.6229},
	{Body::Phobos,   Body::Mars,     TEXT("Phobos"),  FLinearColor(0.55f, 0.50f, 0.46f), true,      7.654},
	{Body::Deimos,   Body::Mars,     TEXT("Deimos"),  FLinearColor(0.58f, 0.53f, 0.48f), true,     30.299},
	{Body::Jupiter,  Body::Sun,      TEXT("Jupiter"), FLinearColor(0.80f, 0.65f, 0.50f), false,     9.925},
	{Body::Io,       Body::Jupiter,  TEXT("Io"),    FLinearColor(0.92f, 0.84f, 0.45f), true,     42.459},
	{Body::Europa,   Body::Jupiter,  TEXT("Europa"),  FLinearColor(0.85f, 0.80f, 0.72f), true,     85.228},
	{Body::Ganymede, Body::Jupiter,  TEXT("Ganymede"),FLinearColor(0.68f, 0.63f, 0.58f), true,    171.709},
	{Body::Callisto, Body::Jupiter,  TEXT("Callisto"),FLinearColor(0.48f, 0.44f, 0.40f), true,    400.536},
	{Body::Saturn,   Body::Sun,      TEXT("Saturn"),  FLinearColor(0.85f, 0.75f, 0.55f), false,    10.656},
	{Body::Mimas,    Body::Saturn,   TEXT("Mimas"),   FLinearColor(0.72f, 0.72f, 0.70f), true,     22.618},
	{Body::Enceladus,Body::Saturn,   TEXT("Enceladus"),FLinearColor(0.95f,0.96f, 0.97f), true,     32.885},
	{Body::Tethys,   Body::Saturn,   TEXT("Tethys"),  FLinearColor(0.88f, 0.88f, 0.86f), true,     45.307},
	{Body::Dione,    Body::Saturn,   TEXT("Dione"),   FLinearColor(0.80f, 0.80f, 0.78f), true,     65.686},
	{Body::Rhea,     Body::Saturn,   TEXT("Rhea"),    FLinearColor(0.78f, 0.78f, 0.76f), true,    108.437},
	{Body::Titan,    Body::Saturn,   TEXT("Titan"),   FLinearColor(0.80f, 0.65f, 0.30f), true,    382.690},
	{Body::Iapetus,  Body::Saturn,   TEXT("Iapetus"), FLinearColor(0.62f, 0.58f, 0.52f), true,   1903.924},
	{Body::Uranus,   Body::Sun,      TEXT("Uranus"),  FLinearColor(0.55f, 0.75f, 0.85f), false,   -17.24},
	{Body::Miranda,  Body::Uranus,   TEXT("Miranda"), FLinearColor(0.70f, 0.70f, 0.70f), true,     33.924},
	{Body::Umbriel,  Body::Uranus,   TEXT("Umbriel"), FLinearColor(0.52f, 0.52f, 0.52f), true,     99.460},
	{Body::Titania,  Body::Uranus,   TEXT("Titania"), FLinearColor(0.66f, 0.63f, 0.60f), true,    208.941},
	{Body::Oberon,   Body::Uranus,   TEXT("Oberon"),  FLinearColor(0.60f, 0.56f, 0.53f), true,    323.118},
	{Body::Neptune,  Body::Sun,      TEXT("Neptune"), FLinearColor(0.35f, 0.50f, 0.90f), false,    16.11},
	{Body::Triton,   Body::Neptune,  TEXT("Triton"),  FLinearColor(0.82f, 0.80f, 0.76f), true,   -141.044},
	{Body::Pluto,    Body::Sun,      TEXT("Pluto"),   FLinearColor(0.65f, 0.60f, 0.55f), false,  -153.29},
	{Body::Charon,   Body::Pluto,    TEXT("Charon"),  FLinearColor(0.58f, 0.56f, 0.55f), true,    153.294},
};
constexpr int32 NUM_BODIES = UE_ARRAY_COUNT(GBodies);

// ═══ ÉCHELLE RÉELLE : 1 u = 1 cm (unité native UE), AUCUNE compression. ═══
// Le rendu reste caméra-relatif (œil = origine, positions km rebasées en double),
// mais on convertit en cm RÉELS : un objet de 100 m fait 10 000 u (taille NORMALE)
// -> fini la micro-échelle qui cassait Nanite/culling/précision. Les corps
// lointains restent des marqueurs HUD (pas de conflit de z-buffer).
constexpr double KM_PER_M  = 1.0e-3;
constexpr double UU_PER_KM = 1.0e5;      // 1 km = 100 000 cm = 100 000 u
// ÉCHANTILLONNAGE DES ORBITES. 192 points suffisaient tant qu'une orbite se
// regardait de loin ; vue de près (les lunes d'une planète cadrée), le polygone
// se voyait — segments longs, cercles « pas ronds ». Le coût est payé au CACHE
// (reconstruit rarement) et à l'ÉMISSION (par segment) : 512 reste largement
// sous le budget d'un batch de lignes, et la courbe devient lisse au gros plan.
constexpr int32  ORBIT_SAMPLES = 512;
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
	// LA sphère des corps, générée par `Tools/make_body_sphere.py`. Une seule pour
	// tous : ce qui distingue deux corps est leur matériau et leur rayon, pas leur
	// maillage — une planète est une sphère.
	UStaticMesh* BodySphere =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Game/SP/SM_SP_Body.SM_SP_Body"));
	if (!BodySphere)
		UE_LOG(LogTemp, Warning,
		       TEXT("[SPSolarSystem] /Game/SP/SM_SP_Body absent : lancer Tools/make_body_sphere.py"));
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
		// ═══ PAS DE NANITE SUR LES CORPS ═══
		// Les GLB s'importent en Nanite par défaut. Un corps est une sphère de
		// ~15 000 triangles : Nanite ne lui apporte RIEN (il est fait pour la
		// géométrie dense), et à l'échelle réelle il lui coûte sa correction — le
		// composant est posé à ~1e9 u de l'origine avec un facteur d'échelle de
		// ~1e8, et le culling par clusters de Nanite rend alors une géométrie
		// DÉCHIRÉE (fragments détachés, pans manquants, « croissant sombre »).
		// L'ISS, elle, garde Nanite : 669 meshes réellement denses, et posée près
		// de l'œil. DIAGNOSTIC : constaté en capture sur la Lune et Io, matériaux
		// et normales innocentés d'abord (les corriger n'a rien changé).
		C->bDisallowNanite = true;

		const double RadiusUU = BodyRadiusKm(Def.B);   // ÉCHELLE VRAIE
		double MeshRadius = 50.0;               // sphère moteur : 50 u de rayon
		const bool bStar = (Def.B == Body::Sun);
		// ═══ LES CORPS SONT FAITS PAR NOUS, PLUS IMPORTÉS ═══ (décision utilisateur,
		// 2026-07-27). Une seule sphère générée par UE (`Tools/make_body_sphere.py` :
		// lat-long 128 x 256 = 64 512 tris, UV équirectangulaires, faces sorties par
		// CONSTRUCTION) et un matériau par corps bâti sur les textures du projet
		// (`Tools/make_body_materials.py`). Ce qui disparaît avec les GLB :
		//   . les faces retournées (on voyait l'intérieur de l'hémisphère arrière) ;
		//   . le matériau d'import générique, translucide et deux faces ;
		//   . la COQUILLE DE NUAGES low-poly de la Terre — les nuages sont désormais
		//     une CARTE dans le matériau, il n'y a plus de seconde géométrie ;
		//   . la silhouette polygonale (4x plus de triangles qu'avant tessellation).
		// Le rayon du mesh vaut 100 par construction : le calcul d'échelle du rendu
		// (rayon réel / rayon du mesh) est inchangé.
		if (BodySphere)
		{
			C->SetStaticMesh(BodySphere);
			MeshRadius = FMath::Max(1.0f, BodySphere->GetBounds().SphereRadius);
		}
		else if (Sphere)   // repli : la sphère du moteur, si l'outil n'a pas tourné
		{
			C->SetStaticMesh(Sphere);
			MeshRadius = FMath::Max(1.0f, Sphere->GetBounds().SphereRadius);
		}
		// Le matériau du corps : instance dédiée (MI_SP_<Corps>), dérivée en MID pour
		// que le rendu puisse lui passer la DIRECTION DU SOLEIL à chaque frame — c'est
		// elle qui décide où est la nuit. Le Soleil, lui, hérite d'un matériau NON
		// ÉCLAIRÉ : une étoile ne dépend d'aucune lumière.
		{
			const FString Chemin =
				FString::Printf(TEXT("/Game/SP/Bodies/MI_SP_%s.MI_SP_%s"), Def.Asset, Def.Asset);
			UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, *Chemin);
			if (Mi)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Mi, C);
				C->SetMaterial(0, M);
				BodyMids.Add(M);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[SPSolarSystem] materiau absent : %s"), *Chemin);
				BodyMids.Add(nullptr);
				// Repli lisible plutôt qu'un corps blanc : la couleur de la table.
				if (!bStar && BaseMat)
				{
					UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
					M->SetVectorParameterValue(TEXT("Color"), Def.Color);
					C->SetMaterial(0, M);
				}
				else if (bStar && EmissiveMat)
				{
					UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(EmissiveMat, C);
					M->SetVectorParameterValue(TEXT("Color"), Def.Color * 60.0f);
					C->SetMaterial(0, M);
				}
			}
		}
		C->SetWorldScale3D(FVector(RadiusUU / MeshRadius));
		C->RegisterComponent();
		MapActor->BodyMeshes.Add(C);
		BodyMeshRadius.Add(MeshRadius);   // l'échelle est refaite à chaque frame

		// ═══ LES ANNEAUX DE SATURNE, FAITS PAR NOUS AUSSI ═══ (2026-07-27)
		// Ils venaient du nœud « Circle » du GLB et NE RENDAIENT PLUS (défaut
		// antérieur au chantier des lunes, constaté en capture). Même remède que
		// pour les corps : `Tools/make_ring_mesh.py` génère une couronne aux rayons
		// RÉELS (bord interne C 74 500 km, bord externe A 136 780 km, rapportés au
		// rayon équatorial de Saturne = 100 dans le repère du mesh parent) avec des
		// UV RADIALES — U = rayon normalisé, ce qu'attend le ruban
		// `8k_saturn_ring_alpha.png`, dont l'alpha porte les divisions.
		// Enfant du corps : il hérite position, échelle et ORIENTATION, donc les
		// anneaux restent dans le plan équatorial incliné à 26,7° [IAU].
		if (Def.B == Body::Saturn)
		{
			UStaticMesh* Rings =
				LoadObject<UStaticMesh>(nullptr, TEXT("/Game/SP/SM_SP_Ring.SM_SP_Ring"));
			if (!Rings)
				UE_LOG(LogTemp, Warning,
				       TEXT("[SPSolarSystem] /Game/SP/SM_SP_Ring absent : lancer Tools/make_ring_mesh.py"));
			if (Rings)
			{
				UStaticMeshComponent* R = NewObject<UStaticMeshComponent>(MapActor);
				R->SetupAttachment(C);
				R->SetMobility(EComponentMobility::Movable);
				R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				R->SetCastShadow(false);
				R->bDisallowNanite = true;
				R->SetStaticMesh(Rings);
				if (UMaterialInterface* RM = LoadObject<UMaterialInterface>(
						nullptr, TEXT("/Game/SP/Bodies/M_SP_Ring.M_SP_Ring")))
					R->SetMaterial(0, RM);
				R->RegisterComponent();
			}
		}
	}

	// ═══ LE SOLEIL EST UNE LUMIÈRE DIRECTIONNELLE ═══ (2026-07-27)
	// C'était une PointLight qu'on plaçait à 1e12 u pour obtenir des rayons quasi
	// parallèles. Mais 1e12 u, c'est SIX ordres de grandeur au-dessus du plafond de
	// précision GPU (RENDER_MAX_UU) : la transformation de la lumière sortait
	// corrompue et l'éclairage se cassait en BANDES sur l'hémisphère éclairé —
	// pans noirs et morceaux détachés, qu'on prenait pour de la géométrie déchirée
	// (constaté sur la Terre, la Lune et Io).
	// Une lumière DIRECTIONNELLE n'a pas de position, seulement une direction :
	// elle est insensible par construction à ce plafond. Et c'est le modèle JUSTE —
	// à ces distances le Soleil est une source à l'infini (0,53° vus de la Terre),
	// ce que le document annonçait déjà comme « plus juste » sans l'avoir fait.
	// Disparaissent avec elle : le rayon d'atténuation, l'exposant de décroissance
	// et l'erreur d'angle déclarée du placement à 1e12 u.
	MapActor->SunLight = NewObject<UDirectionalLightComponent>(MapActor);
	MapActor->SunLight->SetupAttachment(MapActor->GetRootComponent());
	MapActor->SunLight->SetMobility(EComponentMobility::Movable);
	MapActor->SunLight->SetIntensity(3.2f);            // lux ; calé par capture
	MapActor->SunLight->SetCastShadows(false);         // coût ; occultation = canaux (piège n°35)
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
		// L'image du monde unique — le MÊME réglage que la caméra de bord, sans
		// quoi la reprise du vol [M] sauterait en exposition (cf. SPCameraPost.h).
		SPCameraPost::Appliquer(Cam->PostProcessSettings);
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
	// ═══ UNE SEULE HOMOTHÉTIE POUR TOUS LES CORPS ═══
	// Borner CHAQUE corps séparément à RENDER_MAX_UU (première tentative du
	// 2026-07-27) remet tous les corps lointains à la MÊME distance de l'œil : la
	// Lune et la Terre, cadrées ensemble, atterrissaient sur la même sphère et
	// s'INTERPÉNÉTRAIENT (dôme pâle en travers de la Terre, vu en capture). Une
	// homothétie de centre l'œil et de facteur UNIQUE conserve, elle, à la fois
	// les tailles angulaires ET l'ordre des profondeurs — c'est la seule
	// contraction qui ne ment sur rien. On la calibre sur le corps le plus
	// éloigné effectivement rendu en géométrie.
	double DistMaxGeomUU = 0.0;
	for (int32 i = 0; i < NUM_BODIES && i < MapActor->BodyMeshes.Num(); ++i)
	{
		const FBodyDef& Def = GBodies[i];
		const double DistKm = (BodyWorldKm(Def, EpochTdb) - CamWorldKm).Size();
		if (DistKm < BodyRadiusKm(Def.B) * BODY_GEOM_FACTOR)
			DistMaxGeomUU = FMath::Max(DistMaxGeomUU, DistKm * UU_PER_KM);
	}
	const double HomoK = (DistMaxGeomUU > RENDER_MAX_UU)
		? RENDER_MAX_UU / DistMaxGeomUU : 1.0;

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
		// La GÉOMÉTRIE d'une lune obéit au même critère que celle d'une planète :
		// assez proche pour être grosse, donc précise. Rien de plus — plus de
		// drapeau d'affichage (cf. bridge_flags.hpp).
		if (!bGeom) { C->SetVisibility(false, true); continue; }
		C->SetVisibility(true, true);
		// rotation AVANT la position : SetWorldRotation ne touche pas la
		// translation, et les enfants (anneaux de Saturne) suivent.
		C->SetWorldRotation(OrientationAt(Def, EpochTdb));
		// Le rayon suit la MÊME homothétie que la position : la taille angulaire
		// vue de l'œil reste donc exactement la vraie.
		double Fac = 1.0;
		FVector P = CompressKm(RelKm, &Fac);
		// ═══ LE PLAFOND DE PRÉCISION GPU S'APPLIQUE AUSSI AUX CORPS ═══
		// Les marqueurs et les lumières passaient par le borneur `R()` ; les CORPS,
		// eux, étaient posés à leur distance de rendu brute. Or cadrer un corps le
		// place à ~1e9 u de l'œil (Terre : 38 268 km = 3,8e9 u), soit ~1000 fois
		// au-dessus de RENDER_MAX_UU : la matrice monde passe en format GPU avec
		// une perte de précision et la géométrie sort DÉCHIRÉE — pans manquants,
		// fragments détachés, arcs concentriques. Constaté en capture sur la Terre,
		// la Lune et Io le 2026-07-27 ; ce n'est PAS propre aux lunes, elles n'ont
		// fait que le révéler (la capture qui « validait » la Terre lui est
		// antérieure et n'avait pas été refaite — même mécanique de vérification
		// périmée que le terminateur, piège n°29).
		// LA BORNE EST UNE HOMOTHÉTIE DE CENTRE L'ŒIL, de facteur COMMUN à tous les
		// corps (voir HomoK ci-dessus) : on contracte la position ET le rayon du
		// même facteur. La projection étant invariante par changement d'échelle
		// radial autour de l'œil, position écran et taille angulaire sont
		// EXACTEMENT conservées ; le facteur étant unique, l'ordre des profondeurs
		// l'est aussi. On ne déplace rien : on ramène l'échelle dans le domaine où
		// le GPU est juste.
		P *= HomoK;
		Fac *= HomoK;
		C->SetWorldLocation(P);

		// ═══ OÙ EST LA NUIT ═══ — la direction du Soleil, passée au matériau.
		// Le Soleil est à l'origine du monde : la lumière qui frappe ce corps voyage
		// donc dans la direction de sa propre position monde. C'est EXACT et propre
		// à chaque corps (mieux que la lumière directionnelle unique, qui prend une
		// seule direction pour toute la scène). Le matériau en tire le masque de
		// nuit et y allume la carte des lumières de villes.
		if (BodyMids.IsValidIndex(i) && BodyMids[i])
		{
			const FVector SunDir = BodyWorldKm(Def, EpochTdb).GetSafeNormal();
			if (!SunDir.IsNearlyZero())
				BodyMids[i]->SetVectorParameterValue(
					TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0f));
		}
		const double MeshR = BodyMeshRadius.IsValidIndex(i) ? BodyMeshRadius[i] : 50.0;
		C->SetWorldScale3D(FVector(BodyRadiusKm(Def.B) * Fac / MeshR));
	}

	// --- le Soleil éclaire depuis sa position (rebasée) ----------------------
	// PIÈGE PAYÉ (2026-07-26) : cette position passait par `R`, donc était BORNÉE à
	// RENDER_MAX_UU. Ce plafond vaut 1e6 u, soit 10 km à l'échelle réelle (1 u =
	// 1 cm) — alors que le corps regardé est à des milliers de km, donc des
	// milliards d'u. La lumière se retrouvait ~4000 fois plus PRÈS que la planète,
	// pratiquement sur l'œil : l'hémisphère visible était intégralement éclairé et
	// LE TERMINATEUR DISPARAISSAIT. Régression silencieuse du passage à l'échelle
	// réelle (avant, à 1 u = 1 km, le plafond valait 1e6 km et la direction restait
	// bonne) : la capture qui « validait » le jour/nuit lui est ANTÉRIEURE — son HUD
	// dit encore « ECHELLE VRAIE 1 u = 1 km ».
	//
	// SUITE (2026-07-27) : le placement à 1e12 u était lui-même hors du domaine de
	// précision du GPU. La lumière est maintenant DIRECTIONNELLE (voir BuildScene) :
	// il n'y a plus de position à poser, seulement une DIRECTION — celle qui va du
	// Soleil vers ce qu'on regarde. Aucune distance n'entre plus en jeu, donc aucun
	// plafond ne peut plus la corrompre.
	// APPROXIMATION DÉCLARÉE [GDD 6.8] : une seule direction éclaire toute la scène,
	// prise du Soleil vers l'ŒIL. Deux corps très écartés recevraient en toute
	// rigueur des directions différentes ; à la distance où l'on rend de la
	// géométrie (un corps cadré, éventuellement ses lunes), l'écart est négligeable.
	if (MapActor->SunLight)
	{
		// CamWorldKm est la position de l'œil, le Soleil est à l'origine du monde :
		// la lumière voyage donc du Soleil vers l'œil, direction CamWorldKm.
		const FVector Dir = CamWorldKm.GetSafeNormal();
		if (!Dir.IsNearlyZero())
			MapActor->SunLight->SetWorldRotation(Dir.Rotation());
	}

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
	// APPARENTE, en TROIS crans. De loin, un MARQUEUR (comme la flotte) ; de près,
	// le vrai MODÈLE EXTÉRIEUR à l'échelle réelle (109 m) ; dans l'enveloppe, la
	// géométrie INTÉRIEURE (portée par SPStation). Les trois à la position LEO
	// réelle (Terre + offset publié), en caméra-relatif.
	{
		const auto& St = Bridge.station;
		const bool bStation = St.valid.load();
		const FBodyDef* Terre = FindDef(Body::EarthBary);
		const FVector TerrePos = Terre ? BodyWorldKm(*Terre, EpochTdb) : FVector::ZeroVector;
		const FVector NovWorld = TerrePos + EclToUeKmd(St.rel_m[0], St.rel_m[1], St.rel_m[2]);
		const FVector Pnov = R(NovWorld);
		// bascule marqueur -> modèle quand l'envergure (109 m) dépasse ~quelques px
		// (taille angulaire = envergure / distance de vue).
		const double DistNov = FMath::Max(1.0e-6, (NovWorld - CamWorldKm).Size());
		// COEXISTENCE (incr. 3c-3) : l'œil est DANS l'enveloppe de la station, et
		// c'est la géométrie INTÉRIEURE qui rend (SPStation la pose sur ce même
		// Pnov). Le modèle extérieur s'efface alors : deux modèles du même objet ne
		// peuvent pas s'interpénétrer à l'écran. La bascule tombe au franchissement
		// de la coque, là où elle est le moins visible.
		const bool bCoexiste = Bridge.interieur_coexiste.load();
		const bool bModel = bStation && !bCoexiste &&
		                    ((St.envergure_m * 0.001) / DistNov > 3.0e-3);

		// Position de rendu publiée à SPStation : le rebasage caméra-relatif vit
		// ICI, on ne le duplique pas de l'autre côté.
		NovellusRenderUU = Pnov;
		bNovellusRenderValid = bStation;

		// Le marqueur ne sert QUE de loin : ni pendant la coexistence (on serait
		// dans une sphère émissive collée à l'œil), ni quand le modèle rend.
		const bool bMarqueur = bStation && !bModel && !bCoexiste;
		if (MapActor->StationMarker)
		{
			MapActor->StationMarker->SetVisibility(bMarqueur);
			if (bMarqueur)
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
	for (const FBodyDef& Def : GBodies)
	{
		if (Def.B == Body::Sun) continue;
		const double T = OrbitalPeriodS(Def, EpochTdb);
		if (T <= 0.0) continue;

		// Trace = la trajectoire RÉELLE échantillonnée sur une période via
		// l'éphéméride (pas une ellipse idéalisée). Pour une LUNE on garde la trace
		// RELATIVE à son parent : c'est l'émission qui la recentre chaque frame.
		FOrbitCache Entry;
		Entry.Color = FLinearColor(Def.Color.R, Def.Color.G, Def.Color.B, 0.35f);
		Entry.bMoon = Def.bMoon;
		Entry.Body = static_cast<int>(Def.B);
		Entry.ParentBody = static_cast<int>(Def.Parent);
		Entry.PointsKm.Reserve(ORBIT_SAMPLES + 1);
		for (int32 k = 0; k <= ORBIT_SAMPLES; ++k)
		{
			const double Tk = EpochTdb + (T * k) / ORBIT_SAMPLES;
			const fen::Vec3 Rel = GEph.state(Def.B, Def.bMoon ? Def.Parent : Body::Sun,
			                                 fen::Epoch{Tk}).r;
			const FVector P = EclToUeKm(Rel);
			Entry.PointsKm.Add(P);
			if (Def.bMoon) Entry.RadiusKm = FMath::Max(Entry.RadiusKm, P.Size());
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
	// TOUS LES TRACÉS SONT DES LIGNES FINES. Une épaisseur en unités MONDE fait
	// dessiner un quad face caméra par segment, et les jointures se voient (cf. le
	// bloc des orbites plus bas). Épaisseur 0 = chemin « ligne fine » du batcher :
	// un pixel, continu, à toute distance. Les tracés de mission (trajectoire,
	// corridor, nœuds) suivent la même règle que les orbites — une seule doctrine
	// de trait pour toute la carte.
	const float Thick = 0.0f;
	(void)CamDistRender;

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

	const int Survol = fen::app::g_render_bridge.hover_body.load();
	const int Focus  = fen::app::g_render_bridge.focus_body.load();
	for (const FOrbitCache& O : OrbitCache)
	{
		// Une orbite de lune est RELATIVE : on la recentre sur la position COURANTE
		// de sa planète, sinon elle dériverait entre deux reconstructions du cache.
		FVector Centre = FVector::ZeroVector;
		if (O.bMoon)
		{
			const FBodyDef* Parent = FindDef(static_cast<Body>(O.ParentBody));
			if (!Parent) continue;
			Centre = BodyWorldKm(*Parent, EpochTdb);
			// LOD : sous la taille d'un marqueur, l'orbite est un POINT — des
			// segments dégénérés qui empâtent la planète. On la saute.
			const double DistKm = (Centre - CamWorldKm).Size();
			if (DistKm > 1.0 && (O.RadiusKm / DistKm) < MARKER_ANG * 2.0) continue;
		}

		// ÉPAISSEUR PROPRE À CHAQUE ORBITE. Elle était tirée de la distance de l'œil
		// au POINT VISÉ, la même pour toutes : une orbite de lune, dix mille fois
		// plus proche que la distance de cadrage d'un plan système, recevait donc un
		// trait calibré pour l'autre échelle — d'où des anneaux hachés, « pas
		// alignés ». On la tire maintenant de la distance de l'ORBITE elle-même :
		// chaque trajectoire garde la même épaisseur À L'ÉCRAN, quelle que soit son
		// échelle. C'est la même doctrine que `MarkerScale` pour les marqueurs.
		// ═══ TRAIT FIN, PAS UN RUBAN ═══ (2026-07-27, sur retour d'essai)
		// L'épaisseur était donnée en unités MONDE : `DrawLine` construit alors un
		// QUAD face caméra par segment, et les quads consécutifs ne se raccordent
		// pas — d'où la chaîne de tuiles visible en jeu, prise pour un défaut
		// d'alignement des corps. Plus l'épaisseur montait, plus les jointures se
		// voyaient : les « corrections » d'épaisseur aggravaient le mal.
		// Épaisseur 0 fait passer le batcher sur son chemin de LIGNES FINES : un
		// trait d'un pixel, continu, antialiasé, indépendant de la distance et de
		// la résolution. C'est exactement ce que fait Eyes on the Solar System.
		const float ThickO = 0.0f;

		// MISE EN AVANT : l'orbite du corps survolé ou focalisé est plus franche.
		// C'est ce qui rend le survol lisible sans rien écrire à l'écran.
		const bool bVedette = (O.Body == Survol) || (O.Body == Focus);

		// ═══ PAS DE TRAÎNÉE ═══
		// Une version précédente allumait la portion d'orbite que le corps venait
		// de parcourir et l'éteignait en remontant le temps. À l'essai c'était
		// franchement laid : sur un trait d'un pixel, le dégradé se lit en PALIERS
		// d'opacité, pas en fondu, et la trajectoire semblait se déliter. La
		// référence, elle, trace une ligne UNIFORME — et elle a raison : une
		// trajectoire est un lieu géométrique, pas un événement ; ce qui doit
		// attirer l'œil est le CORPS et sa désignation, pas un artifice sur sa
		// courbe. Retiré. Seule subsiste la mise en avant au survol.
		const float A = static_cast<float>(FMath::Clamp(
			O.Color.A * Att * (bVedette ? 2.4 : 1.0), 0.0, 1.0));
		if (A <= 0.004f) continue;
		const FLinearColor Col(O.Color.R, O.Color.G, O.Color.B, A);
		const int32 N = O.PointsKm.Num();
		for (int32 k = 1; k < N; ++k)
			LB->DrawLine(R(Centre + O.PointsKm[k - 1]), R(Centre + O.PointsKm[k]),
			             Col, SDPG_World, ThickO, 0.0f);
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

	// ═══ NON SÉPARABLE DE SON PARENT = PAS DÉSIGNABLE ═══ (piège n°41)
	// Vue du système, une lune tombe sur le MÊME pixel que sa planète : les
	// libellés s'impriment l'un sur l'autre et le picking tire au sort. On ne
	// déclare donc une lune à l'écran que si elle se DÉTACHE de son parent d'au
	// moins la taille d'un marqueur. C'est la règle déjà payée pour Novellus, ici
	// généralisée — et c'est elle qui REMPLACE le défunt drapeau `show_moons` :
	// ce n'est plus une option, c'est du LOD, donc ça ne peut pas rester éteint.
	constexpr double SEP_MIN_NORM = 0.013;   // ~17 px de large sur 1280
	auto SeparableDuParent = [&](const fen::app::RenderBridge::ScreenBodies& Sc,
	                             int32 Count, int ParentBody, double Nx, double Ny) {
		for (int32 i = 0; i < Count; ++i)
		{
			if (Sc.items[i].body != ParentBody) continue;
			if (!Sc.items[i].on_screen) return true;   // parent hors champ : rien à confondre
			const double dx = Nx - Sc.items[i].nx;
			// Écran non carré : l'écart vertical se ramène en fraction de LARGEUR,
			// l'unité dans laquelle SEP_MIN_NORM est exprimé.
			const double dy = (Ny - Sc.items[i].ny) / Aspect;
			return dx * dx + dy * dy >= SEP_MIN_NORM * SEP_MIN_NORM;
		}
		return true;   // parent non publié
	};

	int32 N = 0;
	for (const FBodyDef& Def : GBodies)
	{
		if (N >= fen::app::RenderBridge::ScreenBodies::MAX) break;
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
		if (It.on_screen && Def.bMoon &&
		    !SeparableDuParent(S, N, static_cast<int>(Def.Parent), Nx, Ny))
			It.on_screen = 0;
		++N;
	}

	// NOVELLUS dans la liste écran : focalisable/cliquable comme un corps
	// [GDD v1.2 11.1]. Rayon apparent tiré de l'envergure (109 m) -> minuscule de
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

				// Novellus orbite à 418 km de la Terre : au plan système, les deux
				// tombent sur le MÊME pixel (« DAYRTHLUS », vu en capture). Elle
				// passe par la MÊME règle que les lunes — c'est le même problème, il
				// ne doit pas en exister deux versions qui divergeront.
				if (It.on_screen &&
				    !SeparableDuParent(S, N, static_cast<int>(Body::EarthBary), Nx, Ny))
					It.on_screen = 0;
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
	//
	// PLANCHER : 1 mm. À l'échelle réelle, le zoom descend jusqu'à l'INTÉRIEUR de
	// Novellus (l'amarrage du handoff est à ~20 m) — un plancher à 1 km, hérité de
	// l'avant-échelle-réelle, aurait bloqué la caméra à 50 fois trop loin.
	constexpr double DIST_MIN_KM = 1.0e-6;
	// VOL SCRIPTÉ (`Session::vol_cam`) : la pose publiée EST déjà une trajectoire
	// lissée. La re-lisser la retarderait, et le vol [M] n'arriverait pas sur l'œil
	// du pawn — la coupure du handoff reviendrait, de plusieurs mètres.
	const bool bVolScripte = Bridge.cam.vol_camera.load();
	{
		const double Cible = FMath::Max(DIST_MIN_KM, Bridge.cam.dist_km.load());
		if (bVolScripte || SmoothDistKm <= 0.0) SmoothDistKm = Cible;
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
	const int FocusId = Bridge.focus_body.load();
	const FVector TargetFocus = FocusWorldKm(Epoch);
	if (!bFocusPrimed || bVolScripte) { SmoothFocusKm = TargetFocus; bFocusPrimed = true; }
	else
	{
		// SUIVI SANS RETARD D'UNE CIBLE EN MOUVEMENT [GDD 14.2 : le temps coule].
		// On ADVECTE d'abord le point lissé du déplacement PROPRE de la cible, puis
		// on n'amortit que l'écart restant — c'est-à-dire uniquement celui d'un
		// CHANGEMENT de focus, la seule chose que ce lissage doit adoucir.
		// Sans cela, un amortisseur de premier ordre garde un retard permanent
		// v/vitesse ≈ 29,8/3,5 ≈ 8,5 km sur la Terre : invisible sur une planète
		// cadrée à 38 000 km, mais Novellus cadrée à 1 km sortait de l'écran.
		// L'advection ne s'applique QUE si la cible est la même qu'à la frame
		// précédente : sur un changement de focus, l'écart EST le vol à lisser.
		if (FocusId == LastFocusId) SmoothFocusKm += TargetFocus - LastTargetFocusKm;
		const double Gap = FVector::Dist(SmoothFocusKm, TargetFocus);
		if (Gap < SmoothDistKm * 1.0e-4) SmoothFocusKm = TargetFocus;
		else SmoothFocusKm = FMath::VInterpTo(SmoothFocusKm, TargetFocus, DeltaTime, 3.5f);
	}
	LastFocusId = FocusId;
	LastTargetFocusKm = TargetFocus;

	const double DistKm = FMath::Max(DIST_MIN_KM, SmoothDistKm);
	// ORIENTATION : cible publiée par l'entrée, suivie en douceur (voir le .h).
	const double YawCible = Bridge.cam.yaw.load();
	const double PitchCible = FMath::Clamp(Bridge.cam.pitch.load(), -1.55, 1.55);
	if (!bOrientPrimed || Bridge.cam.vol_camera.load())
	{
		SmoothYaw = YawCible;
		SmoothPitch = PitchCible;
		bOrientPrimed = true;
	}
	else
	{
		// Le yaw se rattrape par le PLUS COURT CHEMIN : sans repli dans ±π, passer
		// par 0 faisait faire un tour complet à la caméra.
		double d = YawCible - SmoothYaw;
		while (d >  UE_DOUBLE_PI) d -= UE_DOUBLE_TWO_PI;
		while (d < -UE_DOUBLE_PI) d += UE_DOUBLE_TWO_PI;
		const double a = 1.0 - FMath::Exp(-DeltaTime / 0.09);   // τ = 90 ms : vif, pas mou
		SmoothYaw += d * a;
		SmoothPitch += (PitchCible - SmoothPitch) * a;
	}
	const double Yaw = SmoothYaw;
	const double Pitch = FMath::Clamp(SmoothPitch, -1.55, 1.55);
	double FovDeg = FMath::Clamp(Bridge.cam.fov_deg.load(), 10.0, 100.0);
	const FVector Offset(DistKm * FMath::Cos(Pitch) * FMath::Cos(Yaw),
	                     DistKm * FMath::Cos(Pitch) * FMath::Sin(Yaw),
	                     DistKm * FMath::Sin(Pitch));
	const FVector CamWorldKm = SmoothFocusKm + Offset;
	// la caméra regarde le point visé : depuis l'origine de rendu, c'est −Offset.
	FRotator CamRot = (-Offset).Rotation();

	// HANDOFF VERS L'AMBULATION (incr. 3c-3) [GDD v1.2 17.4] : sur la dernière
	// portion du vol [M], l'ORIENTATION et le CHAMP DE VISION glissent de ceux de
	// la carte vers ceux DU PAWN (publiés par SPStation dans `station_out`). À
	// l'arrivée les deux caméras sont CONFONDUES — position (Session::pose_bord),
	// orientation et champ — donc la reprise en 1re personne ne se voit pas.
	//
	// Le champ compte autant que le reste : la carte cadre à 45°, la caméra de
	// bord à 90°, soit un saut de grossissement de tan(45°)/tan(22,5°) = 2,4.
	// Vérifié par capture : sans cette convergence, `-sphandoff` rendait la même
	// cloison que `-spscene=iss`, mais 2,4 fois plus grosse.
	// L'interpolation est en TANGENTE de demi-champ, la grandeur linéaire à
	// l'écran — interpoler les degrés ferait « respirer » le zoom en milieu de
	// course.
	{
		const double Melange = FMath::Clamp(Bridge.cam.look_to_bord.load(), 0.0, 1.0);
		if (Melange > 0.0 && Bridge.station_out.ready.load())
		{
			const FRotator LookBord(Bridge.station_out.pitch.load(),
			                        Bridge.station_out.yaw.load(), 0.0);
			// FMath::Lerp(FRotator,...) normalise le delta : passage par le plus
			// court chemin, jamais un tour complet.
			CamRot = FMath::Lerp(CamRot, LookBord, static_cast<float>(Melange));

			const double FovBord =
				FMath::Clamp(static_cast<double>(Bridge.station_out.fov_deg.load()), 10.0, 160.0);
			const double TanA = FMath::Tan(FMath::DegreesToRadians(FovDeg) * 0.5);
			const double TanB = FMath::Tan(FMath::DegreesToRadians(FovBord) * 0.5);
			const double Tan = TanA + (TanB - TanA) * Melange;
			FovDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan(Tan));
		}
	}

	// PLAN DE CLIPPING PROCHE : celui du MOTEUR (`UEngine::NearClipPlane` = 10 u,
	// soit 10 cm), le même de bout en bout du zoom.
	//
	// Il y avait ici un near-clip « adaptatif » (~1/2000 de la distance de vue)
	// posé via `IConsoleManager::FindConsoleVariable("r.SetNearClipPlane")`.
	// Ce code N'A JAMAIS RIEN FAIT : `r.SetNearClipPlane` est un FAutoConsoleCommand
	// (UnrealEngine.cpp), pas une variable ; `FindConsoleVariable` appelle
	// `AsVariable()` sur l'objet trouvé et rend donc nullptr. Tout ce qui a été
	// vérifié en capture l'a été avec le plan par défaut — voir piège n°28.
	// On garde ce défaut, et c'est le bon choix : la profondeur d'UE5 est en
	// reversed-Z flottant (précision maximale PRÈS de l'œil, où sont justement les
	// seuls objets géométriques — les corps lointains sont des marqueurs HUD), et
	// surtout l'intérieur ambulable rend avec CE plan : un plan adaptatif rendrait
	// le handoff visible, puisque les deux côtés de la reprise ne clipperaient pas
	// au même endroit.

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
	if (FMath::Abs(Epoch - LastOrbitEpoch) > ORBIT_REDRAW_DAYS * 86400.0 ||
	    OrbitCache.Num() == 0)
	{
		RebuildOrbitCache(Epoch);
		LastOrbitEpoch = Epoch;
	}
	EmitOrbits(CamWorldKm, DistKm, Epoch);

	PublishScreen(CamWorldKm, CamRot, FovDeg);
}
