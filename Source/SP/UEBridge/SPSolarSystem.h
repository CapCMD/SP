// SPSolarSystem.h — LA CARTE DU SYSTÈME SOLAIRE : l'écran principal du jeu.
// Portage UE 5.8 de render/app/solar_system_map.cpp (référence Vulkan `spr`),
// en technos natives UE — la carte N'EST PAS un onglet, elle EST le jeu.
//
// Doctrine héritée de spr/bridge/RenderSnapshot.hpp :
//   - le rendu ne RECALCULE rien : les positions sortent de l'éphéméride
//     Standish (astro_core, la vérité), à l'époque publiée par le jeu via
//     fen::app::g_render_bridge (sens unique jeu -> rendu) ;
//   - ÉCHELLE VRAIE (1 u = 1 km) : rayons et distances réels, aucune
//     exagération. Le rendu est RELATIF À LA CAMÉRA (floating origin) : les
//     positions restent en double jusqu'à la soustraction, et seul l'écart
//     œil->corps devient un float. Détail et justification dans le .cpp.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SPSolarSystem.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class ACameraActor;

// L'acteur porteur : un composant de mesh par corps + la lumière du Soleil.
// Entièrement construit en C++ au runtime — aucun Blueprint requis.
UCLASS(NotBlueprintable)
class SP_API ASPSolarSystemMapActor : public AActor
{
	GENERATED_BODY()

public:
	ASPSolarSystemMapActor();

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BodyMeshes;
	UPROPERTY() TObjectPtr<UPointLightComponent> SunLight;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> VehicleMarker;
	// Flotte en service [GDD 8.3] : un marqueur par engin, à sa position VRAIE
	// (l'échelle vraie rend inutiles les anneaux symboliques d'avant).
	// Layout figé : [0..5] relais GEO, [6..11] orbiteurs Mars, [12..17] sondes
	// lointaines, [18] vol GEO en cours.
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FleetMarkers;
};

UCLASS()
class SP_API USPSolarSystemSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

private:
	void BuildScene();                    // meshes GLB (ou sphères) + lumière + caméra
	void SetMapActive(bool bActive);      // bascule caméra + visibilité
	// Le point visé par la caméra (corps focalisé, ou le Soleil), en km.
	FVector FocusWorldKm(double EpochTdb) const;
	// Rebase + place tout ce qui est monde ; publie la projection écran.
	void UpdateScene(double EpochTdb, const FVector& CamWorldKm);
	// Les orbites vivent dans le repère rebasé sur l'œil : elles doivent être
	// ré-émises à chaque frame. On sépare donc le COÛTEUX (échantillonner
	// l'éphéméride : mis en cache, en km monde absolus) du BON MARCHÉ (soustraire
	// l'œil et tracer).
	void RebuildOrbitCache(double EpochTdb);
	void EmitOrbits(const FVector& CamWorldKm, double CamDistKm, double EpochTdb);
	void PublishScreen(const FVector& CamWorldKm, const FRotator& CamRot, double FovDeg);

	UPROPERTY() TObjectPtr<ASPSolarSystemMapActor> MapActor;
	UPROPERTY() TObjectPtr<ACameraActor> MapCamera;
	UPROPERTY() TObjectPtr<AActor> PreviousViewTarget;

	// Suivi lissé du point visé (le focus « vole » vers sa cible, façon NASA Eyes).
	FVector SmoothFocusKm = FVector::ZeroVector;
	bool    bFocusPrimed = false;
	// Distance de vue LISSÉE. Le contrôleur publie une distance CIBLE ; l'œil
	// s'en approche en douceur, ce qui donne le « vol » vers un corps quand on
	// clique dessus — et rend la molette fluide sans la rendre molle.
	double  SmoothDistKm = -1.0;

	// Une polyligne MONDE (km absolus) par corps, échantillonnée sur une période.
	struct FOrbitCache { FLinearColor Color; TArray<FVector> PointsKm; };
	TArray<FOrbitCache> OrbitCache;

	// Rayon de la sphère englobante du mesh de chaque corps : l'échelle est
	// recalculée à chaque frame (elle suit la compression de profondeur).
	TArray<double> BodyMeshRadius;
	double LastNearClip = -1.0;

	double LastOrbitEpoch = -1.0e300;
	bool bBuilt = false;
	bool bWasActive = false;
	bool bLastShowMoons = false;
};
