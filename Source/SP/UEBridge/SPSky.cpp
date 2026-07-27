// SPSky.cpp — le fond étoilé. Voir l'entête pour la géométrie et l'approximation.

// Entête jeu AVANT tout entête UE (macros PI/check).
#include "app/bridge_flags.hpp"

#include "SPSky.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"

namespace
{
// Rayon de la voûte : au-delà de tout ce que la scène rend, très en deçà de la
// limite du float (le plan lointain d'UE est infini en Z inversé).
constexpr double SKY_RADIUS_UU = 1.0e7;
constexpr double ENGINE_SPHERE_RADIUS = 50.0;   // /Engine/BasicShapes/Sphere
} // namespace

ASPSkyActor::ASPSkyActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool USPSkySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer)) return false;
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

TStatId USPSkySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USPSkySubsystem, STATGROUP_Tickables);
}

// ---------------------------------------------------------------------------
// La texture : asset importé si présent, sinon décodage du JPEG livré avec le
// jeu de référence. ImageWrapper est déjà une dépendance du module (capture).
UTexture2D* USPSkySubsystem::ChargerTextureEtoiles()
{
	if (UTexture2D* Asset = LoadObject<UTexture2D>(nullptr, TEXT("/Game/SP/T_Starfield.T_Starfield")))
		return Asset;

	const FString Chemin = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Space Program/assets/textures/8k_stars_milky_way.jpg"));
	TArray<uint8> Brut;
	if (!FFileHelper::LoadFileToArray(Brut, *Chemin))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SPSky] texture d'etoiles introuvable : %s"), *Chemin);
		return nullptr;
	}

	IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(EImageFormat::JPEG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Brut.GetData(), Brut.Num())) return nullptr;

	TArray64<uint8> Pixels;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Pixels)) return nullptr;

	const int32 W = Wrapper->GetWidth(), H = Wrapper->GetHeight();
	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex) return nullptr;
	Tex->SRGB = true;
	Tex->Filter = TF_Bilinear;
	Tex->AddToRoot();                       // la voûte vit toute la session
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num());
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	UE_LOG(LogTemp, Log, TEXT("[SPSky] ciel decode a la volee : %d x %d"), W, H);
	return Tex;
}

// ---------------------------------------------------------------------------
void USPSkySubsystem::BuildSky()
{
	UWorld* World = GetWorld();
	if (!World) return;
	bBuilt = true;                     // une seule tentative, réussie ou non

	// ═══ LA VOÛTE PREND LA SPHÈRE DENSE DU PROJET ═══ (2026-07-27)
	// Elle utilisait la sphère du MOTEUR : ~700 triangles. Sur une carte
	// ÉQUIRECTANGULAIRE c'est ruineux — l'interpolation d'UV est linéaire par
	// triangle alors que la projection ne l'est pas, si bien que les dérivées
	// d'UV par pixel explosent et qu'UE choisit un mip grossier : le ciel sortait
	// FLOU alors que la texture est bien en 8K, mip 0, jamais streamée (vérifié).
	// `SM_SP_Body` (64 512 triangles, UV lat-long exactes) supprime la cause.
	double RayonMesh = 50.0;                     // sphère du moteur
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/SP/SM_SP_Body.SM_SP_Body"));
	if (Sphere) RayonMesh = 100.0;               // notre sphère : rayon 100
	else Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Sphere) return;

	// Matériau : celui du projet s'il a été créé (deux faces, réglable), sinon
	// l'émissif texturé du moteur — non éclairé, paramètre « Texture ».
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SP/M_SP_Starfield.M_SP_Starfield"));
	if (!Base)
		Base = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial"));
	if (!Base) return;

	SkyActor = World->SpawnActor<ASPSkyActor>();
	if (!SkyActor) return;

	SkyActor->Dome = NewObject<UStaticMeshComponent>(SkyActor);
	SkyActor->Dome->SetupAttachment(SkyActor->GetRootComponent());   // AVANT Register
	SkyActor->Dome->SetMobility(EComponentMobility::Movable);
	SkyActor->Dome->SetStaticMesh(Sphere);
	SkyActor->Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyActor->Dome->SetCastShadow(false);
	SkyActor->Dome->bReceivesDecals = false;
	// LA VOÛTE N'ÉCLAIRE RIEN. Le projet tourne sous Lumen (et le lancer de
	// rayons est actif) : une sphère ÉMISSIVE de 1e7 u entourant la scène est
	// vue par la GI comme une source d'aire géante — l'intérieur de l'ISS
	// ressortait entièrement cramé. Le ciel est un DÉCOR, pas un éclairage.
	SkyActor->Dome->bAffectDynamicIndirectLighting = false;
	SkyActor->Dome->bAffectDistanceFieldLighting = false;
	SkyActor->Dome->bAffectIndirectLightingWhileHidden = false;
	SkyActor->Dome->bVisibleInRayTracing = false;
	// L'ÉCHELLE NÉGATIVE retourne la sphère : on en voit l'intérieur même avec
	// un matériau à une seule face.
	SkyActor->Dome->SetWorldScale3D(FVector(-SKY_RADIUS_UU / RayonMesh));

	if (UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, SkyActor->Dome))
	{
		if (UTexture2D* Tex = ChargerTextureEtoiles())
			M->SetTextureParameterValue(TEXT("Texture"), Tex);
		SkyActor->Dome->SetMaterial(0, M);
	}
	SkyActor->Dome->RegisterComponent();
	// La voûte est hors norme (rayon 1e8) : on trace ses bornes une fois, c'est
	// le seul moyen simple de distinguer « pas construite » de « pas visible ».
	UE_LOG(LogTemp, Log, TEXT("[SPSky] voute construite : rayon %.0f u, materiau %s"),
	       SkyActor->Dome->Bounds.SphereRadius, *Base->GetName());
}

void USPSkySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bBuilt) { BuildSky(); return; }
	// [BISECT] mise à jour adaptative du dôme retirée temporairement.

	// DIAGNOSTIC (une fois, après quelques frames) : « construite » ne veut pas
	// dire « rendue ». WasRecentlyRendered tranche entre un problème de scène
	// (jamais soumise au renderer) et un problème de matériau (soumise, noire).
	if (++DiagTick == 120 && SkyActor && SkyActor->Dome)
	{
		UE_LOG(LogTemp, Log,
		       TEXT("[SPSky] diag : visible=%d enregistre=%d rendu_recemment=%d echelle=%s"),
		       SkyActor->Dome->IsVisible() ? 1 : 0,
		       SkyActor->Dome->IsRegistered() ? 1 : 0,
		       SkyActor->Dome->WasRecentlyRendered(1.0f) ? 1 : 0,
		       *SkyActor->Dome->GetComponentScale().ToString());
	}
}
