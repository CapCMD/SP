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
#include "app/postes.hpp"

#include "SPStation.h"

#include "SPCameraPost.h"
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
	// Apesanteur : pas de gravité, on glisse et on s'arrête doucement.
	Movement->MaxSpeed = 220.0f;        // ~2,2 m/s : une traversée de module posée
	Movement->Acceleration = 900.0f;
	Movement->Deceleration = 1400.0f;
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

	for (UStaticMesh* M : Meshes)
	{
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

	// Le joueur, posé dans NOVELLUS.
	const FVector Spawn = StationToWorld(NOVELLUS_M[0], NOVELLUS_M[1], NOVELLUS_M[2]);
	Yaw = -FMath::RadiansToDegrees(NOVELLUS_YAW_RAD);   // miroir y -> yaw opposé
	Pitch = FMath::RadiansToDegrees(NOVELLUS_PITCH_RAD);
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
	       TEXT("[SPStation] %d meshes, span %.1f m -> x%.4f, spawn Novellus (%.2f %.2f %.2f) m"),
	       Meshes.Num(), SpanUU / UU_PER_M, Scale,
	       NOVELLUS_M[0], NOVELLUS_M[1], NOVELLUS_M[2]);
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
		Out.eye_m[0] = static_cast<float>(NOVELLUS_M[0]);
		Out.eye_m[1] = static_cast<float>(NOVELLUS_M[1]);
		Out.eye_m[2] = static_cast<float>(NOVELLUS_M[2]);
		Out.yaw = static_cast<float>(Yaw);
		Out.pitch = static_cast<float>(Pitch);
		Out.fov_deg = (Pawn && Pawn->Camera) ? Pawn->Camera->FieldOfView : 90.0f;
		Out.ready = (Pawn != nullptr);
	}
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
	if (!bControl && Pawn && Pawn->Movement)
	{
		// On lâche les commandes : pas de dérive résiduelle pendant la transition
		// (le repère du pawn doit être stable, cf. AppliquerDecalage).
		Pawn->Movement->StopMovementImmediately();
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
void USPStationSubsystem::AppliquerDecalage(bool bCoexiste)
{
	FVector Cible = FVector::ZeroVector;
	if (bCoexiste)
	{
		// Le rebasage caméra-relatif et l'éphéméride vivent dans la carte : on le
		// LUI demande, on ne recalcule rien ici [doctrine du pont].
		if (UWorld* W = GetWorld())
			if (USPSolarSystemSubsystem* Map = W->GetSubsystem<USPSolarSystemSubsystem>())
			{
				FVector Pnov;
				if (Map->GetNovellusRenderUU(Pnov)) Cible = Pnov;
			}
	}
	if (Cible.Equals(Decalage, 0.01)) return;      // 0,1 mm : rien à bouger

	const bool bEtaitCanonique = Decalage.IsNearlyZero();
	const bool bSeraCanonique = Cible.IsNearlyZero();
	// On quitte le repère canonique : mémoriser la pose VIVANTE du pawn, pour y
	// revenir exactement (le joueur ne repart pas au point d'apparition).
	if (bEtaitCanonique && !bSeraCanonique && Pawn) CanonPawnLoc = Pawn->GetActorLocation();
	Decalage = Cible;

	if (StationActor)
	{
		// Collision coupée hors du repère canonique : déplacer 310 corps de
		// collision complexes à chaque frame remuerait la scène physique pour rien
		// — le pawn ne se déplace pas pendant la transition. Une seule bascule par
		// transition (la cible, elle, change à chaque frame).
		if (bEtaitCanonique != bSeraCanonique)
			StationActor->SetActorEnableCollision(bSeraCanonique);
		StationActor->SetActorLocation(CanonStationLoc + Cible);
	}
	if (LightsHolder) LightsHolder->SetActorLocation(CanonLightsLoc + Cible);
	if (Pawn)
	{
		Pawn->SetActorLocation(CanonPawnLoc + Cible, /*bSweep=*/false, nullptr,
		                       ETeleportType::TeleportPhysics);
	}
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

	AppliquerDecalage(bCoexiste);

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

	// --- déplacement : apesanteur, dans le repère du regard ------------------
	const float Fwd = FMath::Clamp(In.fwd.load(), -1.0f, 1.0f);
	const float Right = FMath::Clamp(In.right.load(), -1.0f, 1.0f);
	const float Up = FMath::Clamp(In.up.load(), -1.0f, 1.0f);
	const float Boost = In.boost.load() ? 3.0f : 1.0f;
	if (Fwd != 0.0f || Right != 0.0f || Up != 0.0f)
	{
		const FVector Dir = Look.RotateVector(FVector(Fwd, Right, 0.0)) +
		                    FVector(0.0, 0.0, Up);
		Pawn->AddMovementInput(Dir.GetSafeNormal(), Boost);
	}

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
