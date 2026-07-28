// SPSky.cpp — le fond étoilé. Voir l'entête pour la géométrie et l'approximation.

// Entête jeu AVANT tout entête UE (macros PI/check).
#include "app/bridge_flags.hpp"

#include "SPSky.h"

#include "SPSolarSystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/LineBatchComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif

namespace
{
// Rayon de la voûte : au-delà de tout ce que la scène rend, très en deçà de la
// limite du float (le plan lointain d'UE est infini en Z inversé).
constexpr double SKY_RADIUS_UU = 1.0e7;
constexpr double ENGINE_SPHERE_RADIUS = 50.0;   // /Engine/BasicShapes/Sphere

// Intensité de la carte du ciel. PLEINE (2026-07-27) : elle est enfin servie en
// 8192x4096 (voir ChargerCarteCiel), donc c'est du VRAI détail — la rabaisser
// reviendrait à jeter ce qu'on vient de récupérer. Elle avait été mise à 0,5
// quand on croyait n'avoir qu'une image floue à cacher.
constexpr float SKY_DOME_GAIN = 1.0f;

// ═══ LE CHAMP D'ÉTOILES PONCTUELLES ═══ (portage de spr `make_starfield`)
// Distribution de la référence : beaucoup de faibles, peu de brillantes
// (puissance 3,2 sur la magnitude), très rares grosses (puissance 7 sur la
// taille), teinte par « température » froide->chaude.
constexpr int32  STAR_COUNT = 6000;
constexpr int32  STAR_SEED  = 0x5EED1701;
// Rayon de pose (unités de rendu). Entre la GÉOMÉTRIE — que SPSolarSystem borne
// à 1e6 u par une homothétie de centre l'œil — et la VOÛTE (1e7 u) : les étoiles
// sont donc occultées par les corps, et devant la Voie lactée. Aucune parallaxe
// possible : le rendu est caméra-relatif, l'œil est TOUJOURS à l'origine, donc
// la direction d'une étoile ne dépend jamais de la position du joueur.
constexpr double STAR_RADIUS_UU = 5.0e6;
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
// Une carte du ciel : asset importé si présent, sinon décodage du JPEG livré
// avec le jeu de référence. ImageWrapper est déjà une dépendance du module.
UTexture2D* USPSkySubsystem::ChargerCarteCiel(const TCHAR* CheminAsset, const TCHAR* Fichier)
{
	if (UTexture2D* Asset = LoadObject<UTexture2D>(nullptr, CheminAsset))
	{
#if WITH_EDITOR
		// ═══ LA CAUSE DU CIEL « PIXELLISÉ » ═══ (trouvée au log, 2026-07-27)
		// UE compile ses textures en TÂCHE DE FOND. Tant que ce n'est pas fini,
		// `UTexture2D::IsDefaultTexture()` est vrai et l'asset RÉPOND PAR UNE
		// PASTILLE 32x32 — taille, ressource, tout. Or la voûte est construite à la
		// toute première frame : le matériau capturait donc une carte du ciel de
		// 32 pixels de large étalée sur 360°, soit ~11 degrés par texel. Aucun
		// réglage de filtrage, de compression ou de maillage ne pouvait y changer
		// quoi que ce soit, et l'argument géométrique (8K contre le champ de vision)
		// était juste... mais hors sujet : ce n'était pas 8192 px qui s'affichaient,
		// c'était 32. Preuve au log : « asset 32x32, 14 mips » — QUATORZE mips,
		// c'est-à-dire la chaîne d'un 8192x4096 : la donnée était bien là, seule la
		// ressource GPU ne l'était pas encore.
		// On attend donc la fin de la compilation AVANT de donner la texture au
		// matériau. Hors éditeur, les textures sont cuites : rien à attendre.
		FTextureCompilingManager::Get().FinishCompilation({Asset});
#endif
		// Le ciel est visible en permanence et de partout : il ne doit jamais voir
		// son mip réduit par le streaming (le dôme a une échelle de 1e5 et une
		// densité d'UV que l'estimateur de streaming n'a aucune chance d'évaluer).
		if (!Asset->NeverStream)
		{
			UE_LOG(LogTemp, Warning,
			       TEXT("[SPSky] %s etait streamable : forcee residente. "
			            "Relancer Tools/make_sky.py pour le regler dans l'asset."),
			       *Asset->GetName());
			Asset->NeverStream = true;
			Asset->UpdateResource();
		}
		const FTextureResource* Res = Asset->GetResource();
		UE_LOG(LogTemp, Log,
		       TEXT("[SPSky] %s : asset %dx%d, %d mips, format %d, LODBias %d, groupe %d "
		            "-> ressource %dx%d"),
		       *Asset->GetName(), Asset->GetSizeX(), Asset->GetSizeY(),
		       Asset->GetPlatformData() ? Asset->GetPlatformData()->Mips.Num() : 0,
		       static_cast<int>(Asset->GetPixelFormat()), Asset->GetCachedLODBias(),
		       static_cast<int>(Asset->LODGroup),
		       Res ? Res->GetSizeX() : 0, Res ? Res->GetSizeY() : 0);
		return Asset;
	}

	const FString Chemin = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Space Program/assets/textures"), Fichier);
	TArray<uint8> Brut;
	if (!FFileHelper::LoadFileToArray(Brut, *Chemin))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SPSky] carte du ciel introuvable : %s"), *Chemin);
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
	UE_LOG(LogTemp, Log, TEXT("[SPSky] %s decode a la volee : %d x %d"), Fichier, W, H);
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
		// UNE SEULE CARTE : `8k_stars_milky_way.jpg`. Le dépôt en livre une seconde,
		// `8k_stars.jpg`, mais c'est LE MÊME champ d'étoiles sans la nébulosité
		// (vérifié pixel par pixel) : l'ajouter ne montrait rien de plus — les
		// étoiles vives sont déjà proches de la saturation, et le tonemapper écrase
		// ce qu'on empile au-dessus. Essai fait, mesuré, retiré.
		if (UTexture2D* Tex = ChargerCarteCiel(TEXT("/Game/SP/T_Starfield.T_Starfield"),
		                                       TEXT("8k_stars_milky_way.jpg")))
			M->SetTextureParameterValue(TEXT("Texture"), Tex);
		// Paramètre du matériau de projet (M_SP_Starfield) ; sans effet sur le
		// matériau de repli du moteur, qui n'en a pas.
		M->SetScalarParameterValue(TEXT("Intensity"), SKY_DOME_GAIN);
		SkyActor->Dome->SetMaterial(0, M);
	}
	SkyActor->Dome->RegisterComponent();
	// La voûte est hors norme (rayon 1e8) : on trace ses bornes une fois, c'est
	// le seul moyen simple de distinguer « pas construite » de « pas visible ».
	UE_LOG(LogTemp, Log, TEXT("[SPSky] voute construite : rayon %.0f u, materiau %s"),
	       SkyActor->Dome->Bounds.SphereRadius, *Base->GetName());

	BuildStars();
}

// ---------------------------------------------------------------------------
// LES ÉTOILES. Portage direct de `make_starfield` (spr/scene/RenderScene.cpp) :
// directions uniformes sur la sphère céleste, magnitude en loi de puissance,
// teinte par température. Posées une seule fois : rien ne les fait bouger.
//
// POURQUOI LE RVB PORTE LA LUMINOSITÉ ET PAS L'ALPHA. Les points du batcher sont
// dessinés dans la passe de base avec le nuanceur `SE_BLEND_Opaque` : le pixel
// rendu vaut `Source.rgb`, l'alpha n'est pas mélangé (Engine/Private/
// BatchedElements.cpp). On prémultiplie donc la teinte par la brillance — le
// fond étant noir, le résultat est celui du mélange additif de la référence. Les
// valeurs peuvent dépasser 1 : l'exposition est figée à 1 et le seuil de bloom
// vaut 1 (SPCameraPost), donc seules les plus brillantes débordent légèrement,
// exactement l'effet voulu.
void USPSkySubsystem::BuildStars()
{
	if (!SkyActor) return;
	SkyActor->Stars = NewObject<ULineBatchComponent>(SkyActor);
	SkyActor->Stars->SetupAttachment(SkyActor->GetRootComponent());
	SkyActor->Stars->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyActor->Stars->SetCastShadow(false);
	SkyActor->Stars->bAffectDynamicIndirectLighting = false;
	SkyActor->Stars->bAffectDistanceFieldLighting = false;
	SkyActor->Stars->bVisibleInRayTracing = false;
	SkyActor->Stars->RegisterComponent();

	StarTable.Reserve(STAR_COUNT);
	FRandomStream Rng(STAR_SEED);
	for (int32 i = 0; i < STAR_COUNT; ++i)
	{
		// direction uniforme sur la sphère (z uniforme : c'est la loi d'Archimède)
		const double Z = 2.0 * Rng.GetFraction() - 1.0;
		const double Phi = UE_DOUBLE_TWO_PI * Rng.GetFraction();
		const double Rxy = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));

		const double M = Rng.GetFraction();
		const double Brillance = 0.22 + 1.70 * FMath::Pow(M, 3.2);
		const double Temp = Rng.GetFraction();
		// froide (orangée) -> chaude (bleu-blanc), teintes de la référence
		const FLinearColor Froide(1.00f, 0.80f, 0.55f);
		const FLinearColor Chaude(0.75f, 0.83f, 1.00f);
		const FLinearColor Teinte = FMath::Lerp(Froide, Chaude, static_cast<float>(Temp));

		FStar S;
		S.Dir = FVector(Rxy * FMath::Cos(Phi), Rxy * FMath::Sin(Phi), Z);
		S.Col = FLinearColor(Teinte.R * Brillance, Teinte.G * Brillance, Teinte.B * Brillance, 1.0f);
		S.Px  = static_cast<float>(1.0 + 2.0 * FMath::Pow(M, 7.0));   // 1 px, rarement 3
		StarTable.Add(S);
	}
	UE_LOG(LogTemp, Log, TEXT("[SPSky] champ de %d etoiles tire (graine 0x%X), rayon %.3g u"),
	       StarTable.Num(), STAR_SEED, STAR_RADIUS_UU);
	// Pose immédiate au cadrage de la carte : le Tick affine dès qu'une caméra
	// existe, mais le ciel ne doit pas dépendre de l'existence d'un contrôleur.
	EmitStars(45.0f, FQuat::Identity);
}

// ---------------------------------------------------------------------------
// ═══ « TAILLE DE POINT » DU BATCHER ≠ PIXELS ═══ (mesuré en capture, 2026-07-27)
// `DrawPointElements` construit le quad ainsi :
//     demi-côté monde = CameraX * Size / largeurViewport * w
// avec CameraX NORMALISÉ. Après projection le côté vaut donc `Size * P00` pixels,
// où P00 = 1/tan(champ/2) — la taille n'est constante en pixels QUE pour un champ
// de 90°. À 45° (le cadrage de la carte) P00 = 2,414 : les étoiles sortaient deux
// fois et demie trop grosses, en carrés bien visibles. On divise donc la taille
// voulue par P00, et on la repose quand le champ change assez pour que ça se voie
// (le vol [M] fait glisser le champ de 45° à 90°). Le champ est lu sur le
// gestionnaire de caméra : c'est la seule valeur qui vaut pour les DEUX vues.
void USPSkySubsystem::EmitStars(float FovDeg, const FQuat& Rot)
{
	if (!SkyActor || !SkyActor->Stars || StarTable.Num() == 0) return;
	const float Fov = FMath::Clamp(FovDeg, 5.0f, 170.0f);
	const double P00 = 1.0 / FMath::Tan(FMath::DegreesToRadians(Fov) * 0.5);
	LastStarFov = Fov;
	LastStarRot = Rot;

	SkyActor->Stars->Flush();
	for (const FStar& S : StarTable)
		SkyActor->Stars->DrawPoint(Rot.RotateVector(S.Dir) * STAR_RADIUS_UU, S.Col,
		                           static_cast<float>(S.Px / P00), SDPG_World, 0.0f);
}

void USPSkySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bBuilt) { BuildSky(); return; }
	// [BISECT] mise à jour adaptative du dôme retirée temporairement.

	// ═══ LE CIEL SUIT LE REPÈRE DE RENDU ═══ (2026-07-27)
	// Le monde rend AUSSI à bord, et il y rend dans le repère de la STATION
	// (`GetRenderRot`) : le ciel doit y passer comme tout le reste, sinon il serait
	// solidaire du hublot alors que Novellus fait un tour sur elle-même par orbite
	// — et la reprise en 1re personne ferait basculer les étoiles d'un coup.
	// La VOÛTE suit l'acteur (une rotation) ; les POINTS doivent être réémis
	// (le batcher ignore la matrice du composant, cf. le .h).
	FQuat Rot = FQuat::Identity;
	if (const UWorld* W = GetWorld())
		if (const USPSolarSystemSubsystem* Map = W->GetSubsystem<USPSolarSystemSubsystem>())
			Rot = Map->GetRenderRot();
	if (SkyActor) SkyActor->SetActorRotation(Rot);

	// Le champ de vision décide de la taille des points (voir EmitStars). On ne
	// repose le champ d'étoiles que quand il a bougé de plus de 2 % : un vol [M]
	// le fait glisser de 45° à 90°, une carte immobile ne le touche jamais.
	// Même logique pour le repère : un SEUIL ANGULAIRE, parce que 6 000 points
	// réémis à chaque frame ne se justifient pas pour 0,001° d'écart. 0,03° est
	// sous le pixel à 45° de champ sur 1 280 px ; au temps réel la station y met
	// une demi-seconde, donc le ciel glisse sans qu'on voie de saut.
	if (StarTable.Num() > 0)
		if (const UWorld* W = GetWorld())
			if (const APlayerController* PC = W->GetFirstPlayerController())
				if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
				{
					const float Fov = Cam->GetFOVAngle();
					const bool bChamp =
						Fov > 1.0f && FMath::Abs(Fov - LastStarFov) > 0.02f * LastStarFov;
					const bool bRepere = !Rot.Equals(LastStarRot, 2.6e-4);   // ~0,03°
					if (bChamp || bRepere) EmitStars(Fov > 1.0f ? Fov : LastStarFov, Rot);
				}

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
