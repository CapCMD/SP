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

#include "SPStation.h"

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

// Plus grande dimension visée, en mètres (valeur du jeu de référence).
constexpr double STATION_SPAN_M = 55.0;
constexpr double UU_PER_M = 100.0;

// POINT D'APPARITION : le module NOVELLUS, QG du joueur — position relevée dans
// le jeu de référence (`novellus_pos`, repère station en mètres) et cap associé.
constexpr double NOVELLUS_M[3] = {19.68, -3.67, -1.10};
constexpr double NOVELLUS_YAW_RAD = 3.19;
constexpr double NOVELLUS_PITCH_RAD = -0.03;

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
	// station (mètres) se lit alors directement en ×100 dans le monde.
	StationActor->SetActorScale3D(FVector(Scale));
	StationActor->SetActorLocation(-Centre * Scale);

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
		L->RegisterComponent();
	}

	UE_LOG(LogTemp, Log,
	       TEXT("[SPStation] %d meshes, span %.1f m -> x%.4f, spawn Novellus (%.2f %.2f %.2f) m"),
	       Meshes.Num(), SpanUU / UU_PER_M, Scale,
	       NOVELLUS_M[0], NOVELLUS_M[1], NOVELLUS_M[2]);
	bBuilt = true;
}

void USPStationSubsystem::SetStationActive(bool bActive)
{
	if (StationActor) StationActor->SetActorHiddenInGame(!bActive);
	for (UPointLightComponent* L : StationLights) if (L) L->SetVisibility(bActive);
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	if (bActive)
	{
		PreviousViewTarget = PC->GetViewTarget();
		if (Pawn) PC->SetViewTargetWithBlend(Pawn, 0.0f);
	}
	else if (PreviousViewTarget)
	{
		PC->SetViewTargetWithBlend(PreviousViewTarget, 0.0f);
	}
	fen::app::g_render_bridge.station_out.ready = bActive && Pawn != nullptr;
}

void USPStationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	auto& Bridge = fen::app::g_render_bridge;
	// La station se rend quand la caméra est au plan BORD du Monde (ex-scène
	// Station) : même monde que le plan système, seul le cadrage change
	// (`carte3d_active` = Cadrage::Systeme).
	const bool bActive =
		Bridge.scene.load() == static_cast<int>(fen::app::SceneJeu::Monde) &&
		!Bridge.carte3d_active.load();

	if (bActive && !bBuilt) BuildScene();
	if (bActive != bWasActive) { SetStationActive(bActive); bWasActive = bActive; }
	if (!bActive || !Pawn) return;

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
