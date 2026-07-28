// SPStation.cpp — L'intérieur de l'ISS : la scène d'ACCUEIL du jeu.
//
// ÉCHELLE : 1 u = 1 cm (échelle UE standard). La station est mise à l'échelle
// pour faire 55 m dans sa plus grande dimension — la valeur du jeu de référence
// (`target_span = 55.0f`), qui correspond au tronçon pressurisé réel.
//
// REPÈRE STATION (celui de la référence) : X = axe du couloir, Z = haut, en
// MÈTRES, origine au centre du modèle. Le point d'apparition Novellus en vient
// directement. Passage au monde UE : ×100 (m -> cm), avec miroir en y comme
// partout ailleurs dans ce projet (glTF droitier -> UE gaucher).
//
// L'ENTRÉE vient du HUD (l'overlay Slate capte tout le clavier et la souris,
// cf. SPSolarSystem.cpp) : le pawn applique `RenderBridge::station_in` et
// republie sa position dans `station_out`.

// Les entêtes du jeu AVANT tout entête UE (macros PI/check, cf. SP.Build.cs).
#include "app/bridge_flags.hpp"
#include "app/impesanteur.hpp"
#include "app/postes.hpp"

#include "SPStation.h"

#include "SPCameraPost.h"
#include "SPCapture.h"
#include "SPSolarSystem.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"

namespace {

// Le modèle : ~310 StaticMesh dans un repère COMMUN (transformations cuites).
const TCHAR* ISS_INTERIOR_PATH = TEXT("/Game/ISS/Interior/ISS_Internal/StaticMeshes");

constexpr double UU_PER_M = 100.0;

// Le gabarit de la station et le point d'apparition NOVELLUS vivent en C++ pur
// (app/postes.hpp) : la SESSION en a besoin pour calculer la pose d'amarrage du
// handoff [GDD v1.2 17.4]. Un seul chiffre, une seule source.
constexpr double STATION_SPAN_M = fen::app::STATION_ENVERGURE_M;
constexpr const double* NOVELLUS_M = fen::app::NOVELLUS_OEIL_M;
constexpr double NOVELLUS_YAW_RAD = fen::app::NOVELLUS_YAW_RAD;
constexpr double NOVELLUS_PITCH_RAD = fen::app::NOVELLUS_PITCH_RAD;

// repère station (m, droitier) -> monde UE (cm, gaucher) : miroir en y.
FVector StationToWorld(double Xm, double Ym, double Zm)
{
	return FVector(Xm * UU_PER_M, -Ym * UU_PER_M, Zm * UU_PER_M);
}

} // namespace

// ---------------------------------------------------------------------------
ASPStationActor::ASPStationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ASPStationPawn::ASPStationPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	// Gabarit d'un astronaute en combinaison, à l'aise dans un module de 2,1 m
	// de rayon : 80 cm de haut, 30 cm de rayon.
	Capsule->InitCapsuleSize(30.0f, 80.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = Capsule;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Capsule);
	Camera->bUsePawnControlRotation = false;
	// L'IMAGE DU MONDE UNIQUE : le même post-traitement que la caméra du plan
	// système. Un seul monde, une seule exposition — sans quoi la reprise du vol
	// [M] sauterait en luminance (cf. SPCameraPost.h).
	SPCameraPost::Appliquer(Camera->PostProcessSettings);

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->UpdatedComponent = Capsule;
	// ═══ IL N'INTÈGRE PLUS RIEN ═══ (2026-07-27)
	// Son Tick est COUPÉ. `UFloatingPawnMovement` est un composant de caméra libre :
	// (1) il ne fait rien si le pawn n'est pas possédé — et il ne l'est jamais, ici
	// l'entrée vient du HUD par le pont ; (2) il annule la vitesse dès qu'on lâche
	// la touche (`GetClampedToMaxSize(MaxSpeed * entrée)`), donc il NE SAIT PAS
	// laisser dériver. Aucun de ses trois paramètres ne corrige cela.
	// Il reste ici pour ses UTILITAIRES — `SafeMoveUpdatedComponent`,
	// `SlideAlongSurface`, la résolution de pénétration — qu'on ne veut surtout pas
	// réécrire. La dynamique est dans `app/impesanteur.hpp` (C++ pur, sous oracle)
	// et son application dans `USPStationSubsystem::AvancerEnImpesanteur`.
	Movement->PrimaryComponentTick.bCanEverTick = false;
	Movement->PrimaryComponentTick.bStartWithTickEnabled = false;
}

// ---------------------------------------------------------------------------
bool USPStationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer)) return false;
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

void USPStationSubsystem::Deinitialize()
{
	fen::app::g_render_bridge.station_out.ready = false;
	Super::Deinitialize();
}

TStatId USPStationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USPStationSubsystem, STATGROUP_Tickables);
}

void USPStationSubsystem::BuildScene()
{
	UWorld* W = GetWorld();
	if (!W) return;

	// --- rassembler les meshes du modèle ------------------------------------
	const FAssetRegistryModule& ARM =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByPath(FName(ISS_INTERIOR_PATH), Assets, true);

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
		UE_LOG(LogTemp, Error,
		       TEXT("[SPStation] aucun mesh sous %s — lancer Tools/import_iss.py"),
		       ISS_INTERIOR_PATH);
		bBuilt = true;   // ne pas retenter chaque frame
		return;
	}

	// Échelle : la plus grande dimension du modèle devient STATION_SPAN_M.
	const FVector Size = Bounds.GetSize();
	const double SpanUU = FMath::Max3(Size.X, Size.Y, Size.Z);
	const double Scale = (SpanUU > 1.0) ? (STATION_SPAN_M * UU_PER_M / SpanUU) : 1.0;
	const FVector Centre = Bounds.GetCenter();

	StationActor = W->SpawnActor<ASPStationActor>();
	// Le centre du modèle est amené à l'origine du monde : un point du repère
	// station (mètres) se lit alors directement en ×100 dans le monde. C'est le
	// repère CANONIQUE — celui où le pawn marche et où vit la collision.
	StationActor->SetActorScale3D(FVector(Scale));
	CanonStationLoc = -Centre * Scale;
	StationActor->SetActorLocation(CanonStationLoc);

	int32 NbOuverts = 0;
	for (UStaticMesh* M : Meshes)
	{
		// ═══ LA CUPOLA EST OUVERTE, ET C'EST LA CONDITION POUR VOIR DEHORS ═══
		// (2026-07-27) Le modèle porte `Cupola_Int_Glass` (les sept vitres) et
		// `Cupola_WC_01..07` (les VOLETS, window covers). Mesuré par
		// `Tools/diag_iss_cupola.py` : le matériau du verre est en **BLEND_OPAQUE**
		// — la vitre est un mur. Aucun réglage d'instance ne peut y remédier : le
		// mode de fusion est une propriété du matériau COMPILÉ, pas un paramètre.
		// On ne rend donc ni les vitres ni les volets. Ce n'est pas un contournement
		// mais l'état NOMINAL de la cupola en observation : volets ouverts, et une
		// vitre propre ne se voit pas. APPROXIMATION DÉCLARÉE [GDD 6.8] : pas de
		// reflet ni de réfraction sur la vitre — ils viendront avec un vrai matériau
		// translucide, qui est un travail d'asset (Tools/), pas de code.
		// La collision, elle, DISPARAÎT avec le mesh : on ne peut donc pas s'appuyer
		// sur la vitre. C'est cohérent avec ce qu'on montre, et sans conséquence —
		// la cabine de la cupola, elle, reste solide.
		const FString Nom = M->GetName();
		if (Nom.StartsWith(TEXT("Cupola_Int_Glass")) || Nom.StartsWith(TEXT("Cupola_WC_")))
		{
			++NbOuverts;
			continue;
		}
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(StationActor);
		C->SetupAttachment(StationActor->GetRootComponent());
		// Movable, PAS Static : un composant créé à l'exécution avec une
		// mobilité statique ne s'enregistre pas correctement dans la scène de
		// rendu (rien ne s'affiche). Constaté en capture.
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(M);
		// Collision RÉELLE : les meshes sont passés en UseComplexAsSimple par
		// Tools/iss_collision.py — le joueur bute donc sur la vraie géométrie.
		C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		C->SetCollisionProfileName(TEXT("BlockAll"));
		C->SetCastShadow(false);
		// ═══ LA COQUE OCCULTE LE SOLEIL — exprimé par CANAL D'ÉCLAIRAGE ═══
		// Dans le monde unique, l'intérieur et le plan système coexistent (incr.
		// 3c-3) ; or le Soleil de la carte est un PointLight sans décroissance et de
		// rayon 6e14 u, et RIEN ne porte d'ombre ici (SetCastShadow(false) partout,
		// pour le coût). Il éclairait donc l'intérieur À TRAVERS la coque : mesuré
		// +18/255 de luminance sur la capture du handoff, plus un gradient — soit
		// exactement une coupure d'éclairage à la reprise.
		// L'occultation par la coque est un FAIT physique : on l'exprime en mettant
		// l'intérieur (et ses seules lampes) sur le canal 1, hors d'atteinte des
		// lumières du plan système. APPROXIMATION DÉCLARÉE [GDD 6.8] : une vraie
		// occultation viendrait d'ombres portées, pas d'un canal.
		C->SetLightingChannels(false, true, false);
		C->RegisterComponent();
		StationActor->Parts.Add(C);
	}

	// ÉCLAIRAGE DE CABINE. Les néons du modèle sont émissifs, mais l'émissif
	// n'éclaire pas les autres surfaces sans GI : sans lampes, la station est
	// NOIRE (constaté en capture). On pose donc des plafonniers le long du
	// couloir, plus une lampe portée par le joueur pour que la zone regardée
	// soit toujours lisible. Blanc froid, comme à bord.
	{
		AActor* Holder = W->SpawnActor<AActor>();
		Holder->SetRootComponent(NewObject<USceneComponent>(Holder, TEXT("Root")));
		LightsHolder = Holder;
		CanonLightsLoc = Holder->GetActorLocation();
		// le couloir principal court le long de X, sur ~55 m
		for (int32 i = 0; i < 12; ++i)
		{
			const double Xm = -26.0 + i * 4.5;
			UPointLightComponent* L = NewObject<UPointLightComponent>(Holder);
			L->SetupAttachment(Holder->GetRootComponent());
			L->SetMobility(EComponentMobility::Movable);
			L->bUseInverseSquaredFalloff = false;
			L->SetAttenuationRadius(700.0f);        // 7 m
			L->SetLightFalloffExponent(1.4f);
			L->SetIntensity(9.0f);
			L->SetLightColor(FLinearColor(0.92f, 0.95f, 1.00f));
			L->SetCastShadows(false);
			// Canal 1 : les plafonniers éclairent la station, pas la Terre qui passe
			// derrière le hublot (cf. le canal des meshes plus haut).
			L->SetLightingChannels(false, true, false);
			L->SetWorldLocation(StationToWorld(Xm, 0.0, 0.6));
			L->RegisterComponent();
			StationLights.Add(L);
		}
	}

	// Le joueur, posé dans NOVELLUS — sauf si une capture demande un autre poste
	// d'observation (`-spoeil`, cf. SPCapture.h) : la cupola est à 38 m de là.
	double OeilM[3] = {NOVELLUS_M[0], NOVELLUS_M[1], NOVELLUS_M[2]};
	Yaw = -FMath::RadiansToDegrees(NOVELLUS_YAW_RAD);   // miroir y -> yaw opposé
	Pitch = FMath::RadiansToDegrees(NOVELLUS_PITCH_RAD);
	{
		double CapYaw = 0.0, CapPitch = 0.0;
		if (SPCapture::RequestedOeil(OeilM, CapYaw, CapPitch)) { Yaw = CapYaw; Pitch = CapPitch; }
	}
	const FVector Spawn = StationToWorld(OeilM[0], OeilM[1], OeilM[2]);
	Pawn = W->SpawnActor<ASPStationPawn>(Spawn, FRotator(Pitch, Yaw, 0.0));
	// lampe portée : garantit que ce qu'on regarde est lisible, partout.
	if (Pawn)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(Pawn);
		L->SetupAttachment(Pawn->GetRootComponent());
		L->SetMobility(EComponentMobility::Movable);
		L->bUseInverseSquaredFalloff = false;
		L->SetAttenuationRadius(900.0f);
		L->SetLightFalloffExponent(1.2f);
		L->SetIntensity(7.0f);
		L->SetLightColor(FLinearColor(0.95f, 0.97f, 1.00f));
		L->SetCastShadows(false);
		L->SetLightingChannels(false, true, false);   // canal de la station
		L->RegisterComponent();
	}

	CanonPawnLoc = Spawn;

	UE_LOG(LogTemp, Log,
	       TEXT("[SPStation] %d meshes (%d ecartes : cupola ouverte), span %.1f m -> x%.4f, "
	            "oeil (%.2f %.2f %.2f) m cap %.0f/%.0f"),
	       Meshes.Num(), NbOuverts, SpanUU / UU_PER_M, Scale,
	       OeilM[0], OeilM[1], OeilM[2], Yaw, Pitch);
	bBuilt = true;

	// L'ŒIL PUBLIÉ EST VALIDE DÈS QUE LA SCÈNE EXISTE — indépendamment de qui
	// tient la caméra. `Session::pose_bord` s'en sert pour ramener le vol [M]
	// EXACTEMENT là où le joueur a quitté le bord (le lier aux commandes ferait
	// retomber la caméra sur le point d'apparition à chaque retour), et
	// SPSolarSystem s'en sert pour aligner l'orientation à la reprise. On le publie
	// donc d'emblée : sans cela, un premier retour depuis le plan système
	// mélangerait le regard vers un cap nul (`-spscene=map`, par exemple).
	{
		auto& Out = fen::app::g_render_bridge.station_out;
		Out.eye_m[0] = static_cast<float>(OeilM[0]);
		Out.eye_m[1] = static_cast<float>(OeilM[1]);
		Out.eye_m[2] = static_cast<float>(OeilM[2]);
		Out.yaw = static_cast<float>(Yaw);
		Out.pitch = static_cast<float>(Pitch);
		Out.fov_deg = (Pawn && Pawn->Camera) ? Pawn->Camera->FieldOfView : 90.0f;
		Out.ready = (Pawn != nullptr);
	}

	VerifierPostes();
}

// ═══ LES POSTES SONT-ILS DANS DU VIDE ? ═══ (2026-07-27)
// Leurs positions vivent en C++ pur (`app/postes.hpp`) et sont MESURÉES sur le
// modèle (`Tools/diag_iss_modules.py`) — mais mesurées sur des BOÎTES, pas sur la
// géométrie. Un poste posé de quelques dizaines de centimètres dans une cloison
// serait inatteignable, et le seul symptôme serait une invite « [E] OUVRIR » qui
// ne s'allume jamais : un silence, c'est-à-dire le pire des retours.
//
// UE, lui, possède la vraie géométrie et sa collision. Il VÉRIFIE donc la donnée
// au lieu de lui faire confiance — même doctrine que `BuildScene` vérifiant que
// la table des corps couvre bien les deux régimes d'orientation. Le test est un
// balayage de la capsule du joueur : si elle ne tient pas là, personne n'y tient.
void USPStationSubsystem::VerifierPostes()
{
	UWorld* W = GetWorld();
	if (!W || !StationActor) return;
	const auto& Posts = fen::app::g_render_bridge.posts;
	const int32 N = FMath::Min(Posts.n.load(), fen::app::RenderBridge::PostSnap::MAX);
	int32 NbBloques = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const auto& It = Posts.items[i];
		const FVector P = StationToWorld(It.x, It.y, It.z);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SPPosteLibre), /*bTraceComplex=*/true);
		if (Pawn) Params.AddIgnoredActor(Pawn);
		// La capsule du pawn (30 cm de rayon, 80 cm de demi-hauteur, cf. son ctor).
		const bool bBloque = W->OverlapBlockingTestByChannel(
			P, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(30.0f, 80.0f), Params);
		if (bBloque)
		{
			++NbBloques;
			int32 NbDef = 0;
			const fen::app::PosteDef* Defs = fen::app::postes_def(NbDef);
			UE_LOG(LogTemp, Warning,
			       TEXT("[SPStation] poste %d (%hs, %hs) a (%.2f %.2f %.2f) m est DANS la "
			            "geometrie : injoignable. Corriger app/postes.hpp "
			            "(mesures : Tools/diag_iss_modules.py)."),
			       i, (i < NbDef ? Defs[i].label : "?"), (i < NbDef ? Defs[i].sub : "?"),
			       It.x, It.y, It.z);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[SPStation] postes : %d publies, %d bloques"), N, NbBloques);
}

// La géométrie rend-elle ? Vrai aux commandes, et aussi pendant la COEXISTENCE
// (l'œil du plan système est dans l'enveloppe, incr. 3c-3).
void USPStationSubsystem::SetStationVisible(bool bVisible)
{
	if (StationActor) StationActor->SetActorHiddenInGame(!bVisible);
	for (UPointLightComponent* L : StationLights) if (L) L->SetVisibility(bVisible);
}

// Le joueur est-il aux commandes en première personne ? C'est la SEULE condition
// qui donne la caméra au pawn — pendant la coexistence, la carte garde la vue.
void USPStationSubsystem::SetStationInControl(bool bControl)
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!bControl && Pawn)
	{
		// On lâche les commandes : pas de dérive résiduelle pendant la transition
		// (le repère du pawn doit être stable, cf. AppliquerPose). C'est le SEUL
		// endroit où la vitesse est annulée sans qu'on s'agrippe — et c'en est un
		// bon : le joueur quitte la première personne, il n'y a plus de corps qui
		// dérive, seulement une caméra qu'on emmène ailleurs.
		Vitesse = FVector::ZeroVector;
		if (Pawn->Movement) Pawn->Movement->Velocity = FVector::ZeroVector;
	}
	if (!PC) return;
	if (bControl)
	{
		PreviousViewTarget = PC->GetViewTarget();
		if (Pawn) PC->SetViewTargetWithBlend(Pawn, 0.0f);
	}
	else if (PreviousViewTarget)
	{
		PC->SetViewTargetWithBlend(PreviousViewTarget, 0.0f);
	}
}

// ---------------------------------------------------------------------------
// HANDOFF (incr. 3c-3) : pendant la coexistence, la station est REBASÉE sur la
// position de rendu de Novellus — le point exact où la carte plaçait le modèle
// extérieur. L'œil du plan système, qui est à l'origine du rendu, se retrouve
// donc DANS la station, à la place même de l'œil du pawn en fin de vol.
// Hors coexistence, retour au repère canonique, à l'unité près.
//
// ═══ ET L'ATTITUDE, DEPUIS QUE LA STATION TOURNE ═══ (2026-07-27)
// Novellus tient une attitude LVLH : cupola au nadir, un tour par orbite dans
// l'inertiel. Le MODÈLE EXTÉRIEUR la porte déjà. Si l'intérieur ne la portait
// pas, la bascule de LOD au franchissement de la coque — là où l'un s'efface et
// l'autre apparaît — ferait PIVOTER la station à l'écran, en plein milieu du vol
// [M] : exactement la coupure que ce mécanisme existe pour supprimer.
//
// Elle n'est appliquée QUE hors du repère canonique, et ce n'est pas une demi-
// mesure : à bord, le rendu se fait DANS le repère de la station (c'est là que le
// joueur marche et que vit la collision des 310 corps), et le plan système n'y
// rend pas — il n'y a donc aucun objet extérieur par rapport auquel une
// orientation absolue voudrait dire quelque chose. Le handoff reste invisible
// parce que la CAMÉRA compose la même rotation (`Session::pose_bord` pour la
// position, `SPSolarSystem` pour le regard) : caméra et géométrie subissent la
// même rotation rigide, donc l'image ne change pas.
void USPStationSubsystem::AppliquerPose(bool bCoexiste)
{
	FVector Cible = FVector::ZeroVector;
	FQuat CibleRot = FQuat::Identity;
	if (bCoexiste)
	{
		// Le rebasage caméra-relatif, l'éphéméride et l'attitude vivent dans la
		// carte : on le LUI demande, on ne recalcule rien ici [doctrine du pont].
		if (UWorld* W = GetWorld())
			if (USPSolarSystemSubsystem* Map = W->GetSubsystem<USPSolarSystemSubsystem>())
			{
				FVector Pnov;
				if (Map->GetNovellusRenderUU(Pnov))
				{
					Cible = Pnov;
					CibleRot = Map->GetNovellusAttitude();
				}
			}
	}
	// 0,1 mm et 0,01° : en deçà, rien à bouger. Le seuil d'angle compte autant que
	// celui de position — l'attitude dérive de 0,065°/s au temps réel, donc sans
	// lui on relancerait un déplacement d'acteur à chaque frame pour rien.
	if (Cible.Equals(Decalage, 0.01) && CibleRot.Equals(Attitude, 1.0e-4)) return;

	const bool bEtaitCanonique = Decalage.IsNearlyZero();
	const bool bSeraCanonique = Cible.IsNearlyZero();
	// On quitte le repère canonique : mémoriser la pose VIVANTE du pawn, pour y
	// revenir exactement (le joueur ne repart pas au point d'apparition).
	if (bEtaitCanonique && !bSeraCanonique && Pawn) CanonPawnLoc = Pawn->GetActorLocation();
	Decalage = Cible;
	Attitude = CibleRot;

	// Le repère canonique tout entier subit la rotation, PUIS la translation : un
	// point p du modèle va en `Cible + Attitude·p`. C'est en particulier vrai du
	// décalage qui amène le centre du modèle à l'origine (`CanonStationLoc`) —
	// l'oublier ferait décrire à la station un petit cercle parasite, du rayon de
	// son décentrage, à mesure qu'elle tourne sur elle-même.
	if (StationActor)
	{
		// Collision coupée hors du repère canonique : déplacer 310 corps de
		// collision complexes à chaque frame remuerait la scène physique pour rien
		// — le pawn ne se déplace pas pendant la transition. Une seule bascule par
		// transition (la cible, elle, change à chaque frame).
		if (bEtaitCanonique != bSeraCanonique)
			StationActor->SetActorEnableCollision(bSeraCanonique);
		StationActor->SetActorRotation(Attitude);
		StationActor->SetActorLocation(Cible + Attitude.RotateVector(CanonStationLoc));
	}
	if (LightsHolder)
	{
		LightsHolder->SetActorRotation(Attitude);
		LightsHolder->SetActorLocation(Cible + Attitude.RotateVector(CanonLightsLoc));
	}
	if (Pawn)
	{
		Pawn->SetActorLocation(Cible + Attitude.RotateVector(CanonPawnLoc),
		                       /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	}
}

// ═══ SE DÉPLACER EN IMPESANTEUR ═══ (2026-07-27)
//
// La LOI est en C++ pur et sous oracle (`app/impesanteur.hpp`) : on dérive, on
// n'accélère qu'en poussant sur quelque chose, on s'arrête en s'agrippant. Ce qui
// reste ici est ce qu'UE seul peut faire — MESURER le contact et POUSSER la
// capsule à travers la géométrie.
//
// LE COMPOSANT DE MOUVEMENT N'INTÈGRE PLUS RIEN (son Tick est coupé dans le
// constructeur du pawn) : il ne sert que de porte d'entrée vers
// `SafeMoveUpdatedComponent` et `SlideAlongSurface`, c'est-à-dire vers le balayage,
// le glissement et la résolution de pénétration du moteur — tout ce qu'on ne veut
// SURTOUT pas réécrire. On lui garde sa `Velocity` à jour pour que le reste du
// moteur (et un futur `GetVelocity()`) voie la vérité.
void USPStationSubsystem::AvancerEnImpesanteur(float DeltaTime, const FRotator& Look)
{
	UWorld* W = GetWorld();
	if (!W || !Pawn || !Pawn->Movement || !Pawn->Capsule || DeltaTime <= 0.0f) return;
	auto& In = fen::app::g_render_bridge.station_in;

	// --- l'entrée, dans le repère du REGARD -----------------------------------
	// « haut/bas » reste l'axe Z du repère station : en impesanteur il n'y a pas de
	// verticale, mais un module EN A UNE — sol et plafond y sont peints, les racks
	// y sont orientés, et l'équipage s'y aligne. C'est le « local vertical » de la
	// station, pas une gravité.
	const double Fwd = FMath::Clamp(In.fwd.load(), -1.0f, 1.0f);
	const double Right = FMath::Clamp(In.right.load(), -1.0f, 1.0f);
	const double Up = FMath::Clamp(In.up.load(), -1.0f, 1.0f);
	const FVector DirUE = Look.RotateVector(FVector(Fwd, Right, 0.0)) + FVector(0, 0, Up);

	// --- Y A-T-IL DE QUOI POUSSER ? -------------------------------------------
	// La question que la loi pose, et à laquelle seule la géométrie répond : une
	// paroi est-elle À PORTÉE DE BRAS ? On balaie la capsule du joueur GONFLÉE de
	// la longueur d'un bras ; si elle touche, il y a un point d'appui.
	// C'est volontairement généreux : dans un module de 4 m on est presque toujours
	// à portée de quelque chose — et c'est la réalité, l'ISS est tapissée de mains
	// courantes. Le régime « volume libre » est l'exception qu'on sent quand on
	// dérive au milieu d'un nœud, pas la règle.
	constexpr float PORTEE_BRAS_UU = 65.0f;      // 65 cm : un bras tendu
	const FVector Pos = Pawn->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SPAppuiParoi), /*bTraceComplex=*/true);
	Params.AddIgnoredActor(Pawn);
	const bool bAppui = W->OverlapBlockingTestByChannel(
		Pos, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(30.0f + PORTEE_BRAS_UU, 80.0f + PORTEE_BRAS_UU),
		Params);

	// --- la loi (SI : le pont et le rendu sont en cm, le cœur en mètres) -------
	constexpr double UU_PAR_M = UU_PER_M;
	const fen::Vec3 V0{Vitesse.X / UU_PAR_M, Vitesse.Y / UU_PAR_M, Vitesse.Z / UU_PAR_M};
	const fen::Vec3 D{DirUE.X, DirUE.Y, DirUE.Z};
	const fen::Vec3 V1 = fen::app::avancer_vitesse(
		V0, D, bAppui, In.agrippe.load(), DeltaTime);
	Vitesse = FVector(V1.x * UU_PAR_M, V1.y * UU_PAR_M, V1.z * UU_PAR_M);

	// --- pousser la capsule, et encaisser ce qu'elle heurte -------------------
	const FVector Delta = Vitesse * DeltaTime;
	if (Delta.IsNearlyZero(1.0e-6)) { Pawn->Movement->Velocity = Vitesse; return; }

	const FVector Avant = Pawn->GetActorLocation();
	FHitResult Hit(1.0f);
	Pawn->Movement->SafeMoveUpdatedComponent(Delta, Pawn->GetActorQuat(), true, Hit);
	if (Hit.IsValidBlockingHit())
		Pawn->Movement->SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, true);

	// ═══ CE QUE LA GÉOMÉTRIE A CONCÉDÉ FAIT FOI ═══ (piège payé deux fois)
	// La vitesse ne s'absorbe PAS à partir du résultat de collision rapporté. Deux
	// tentatives, deux échecs mesurés — le joueur s'arrêtait bien contre le plafond
	// (le moteur bloquait) mais gardait 0,59 m/s indéfiniment : une vitesse fantôme,
	// invisible tant qu'on pousse, et qui repart d'un bond dès qu'on s'écarte.
	//   . `IsValidBlockingHit()` vaut `bBlockingHit && !bStartPenetrating` : à fleur
	//     de paroi, le balayage part en pénétration et le prédicat devient faux ;
	//   . `bBlockingHit` seul ne suffit pas non plus — `SafeMoveUpdatedComponent`
	//     RÉESSAIE le déplacement après avoir résolu la pénétration et ÉCRASE
	//     `OutHit` avec le résultat du second essai (MovementComponent.cpp).
	// La réponse ne se lit donc pas dans le rapport de contact : elle se lit dans le
	// DÉPLACEMENT OBTENU. Si la géométrie a retenu quelque chose, ce qu'elle a laissé
	// passer EST la vitesse — bloqué net donne zéro, effleuré donne la tangentielle,
	// et les coins et contacts multiples se règlent sans cas particulier.
	// Le seuil (0,2 %) laisse le vol libre BIT POUR BIT intact : sans lui, la
	// division par dt réinjecterait une erreur d'arrondi à chaque frame, et une
	// dérive qui perd un milliardième par frame n'est plus une dérive.
	const FVector Parcouru = Pawn->GetActorLocation() - Avant;
	if (Parcouru.SizeSquared() < Delta.SizeSquared() * 0.998)
		Vitesse = Parcouru / DeltaTime;
	Pawn->Movement->Velocity = Vitesse;
}

void USPStationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	auto& Bridge = fen::app::g_render_bridge;
	const bool bMonde =
		Bridge.scene.load() == static_cast<int>(fen::app::SceneJeu::Monde);
	// LA MAIN : la caméra est au plan BORD du Monde — même monde que le plan
	// système, seul le cadrage change (`carte3d_active` = Cadrage::Systeme).
	const bool bMain = bMonde && !Bridge.carte3d_active.load();
	// LA COEXISTENCE (incr. 3c-3) : l'œil du plan système est DANS l'enveloppe de
	// la station. La géométrie intérieure rend, à la position réelle de Novellus,
	// AVANT que la main ne passe — c'est ce qui supprime la dernière coupure.
	const bool bCoexiste = bMonde && !bMain && Bridge.interieur_coexiste.load();
	const bool bRendu = bMain || bCoexiste;

	if (bRendu && !bBuilt) BuildScene();
	if (bRendu != bWasVisible) { SetStationVisible(bRendu); bWasVisible = bRendu; }
	if (bMain != bWasInControl) { SetStationInControl(bMain); bWasInControl = bMain; }
	if (!bBuilt || !Pawn) return;

	AppliquerPose(bCoexiste);

	if (!bMain) return;   // pas aux commandes : ni entrée, ni republication

	// --- regard : le HUD publie le delta souris, on le consomme --------------
	auto& In = Bridge.station_in;
	const float Dx = In.look_dx.exchange(0.0f);
	const float Dy = In.look_dy.exchange(0.0f);
	Yaw += Dx * 0.12;
	Pitch = FMath::Clamp(Pitch - Dy * 0.12, -87.0, 87.0);
	const FRotator Look(Pitch, Yaw, 0.0);
	Pawn->SetActorRotation(Look);
	if (Pawn->Camera) Pawn->Camera->SetWorldRotation(Look);

	// --- déplacement : IMPESANTEUR (voir app/impesanteur.hpp) -----------------
	AvancerEnImpesanteur(DeltaTime, Look);

	// --- publication de l'état (le HUD affiche, et sauvegarde la position) ---
	auto& Out = Bridge.station_out;
	const FVector P = Pawn->GetActorLocation();
	Out.eye_m[0] = static_cast<float>(P.X / UU_PER_M);
	Out.eye_m[1] = static_cast<float>(-P.Y / UU_PER_M);   // retour au repère station
	Out.eye_m[2] = static_cast<float>(P.Z / UU_PER_M);
	Out.yaw = static_cast<float>(Yaw);
	Out.pitch = static_cast<float>(Pitch);
	Out.ready = true;

	// --- poste à portée : c'est UE qui mesure, le HUD qui affiche l'invite ---
	int32 Near = -1;
	double Best = TNumericLimits<double>::Max();
	const int32 NPosts = FMath::Min(Bridge.posts.n.load(),
	                                fen::app::RenderBridge::PostSnap::MAX);
	for (int32 i = 0; i < NPosts; ++i)
	{
		const auto& It = Bridge.posts.items[i];
		const FVector Q = StationToWorld(It.x, It.y, It.z);
		const double D = FVector::Dist(P, Q);
		if (D <= It.radius_m * UU_PER_M && D < Best) { Best = D; Near = i; }
	}
	Out.near_post = Near;
}
