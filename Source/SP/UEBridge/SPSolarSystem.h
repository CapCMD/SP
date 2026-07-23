// SPSolarSystem.h — la carte 3D du système solaire, côté monde UE.
// Portage de render/app/solar_system_map.cpp (RenderCore Vulkan) vers UE 5.8.
//
// Doctrine héritée de spr/bridge/RenderSnapshot.hpp :
//   - le rendu ne RECALCULE rien : les positions sortent de l'éphéméride
//     Standish (astro_core, la vérité), à l'époque publiée par le jeu via
//     fen::app::g_render_bridge (sens unique jeu -> rendu) ;
//   - grande échelle : positions en DOUBLE de bout en bout (UE5 LWC : FVector
//     est double) ; l'échelle CARTE est déclarée dans le .cpp, jamais cachée.
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
	// Flotte en service [GDD 8.3] : anneaux SYMBOLIQUES (déclaré dans le HUD).
	// Layout figé : [0..5] relais GEO, [6..11] orbiteurs Mars, [12..17] sondes
	// lointaines, [18] vol GEO en cours.
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FleetMarkers;
	// Vue rapprochée Terre (vol GEO, 1 u = 100 m) — scène géocentrique déportée.
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CloseEarth;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CloseMarker;
	UPROPERTY() TObjectPtr<UPointLightComponent> CloseLight;
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
	void UpdatePositions(double EpochTdb);
	void RedrawOrbits(double EpochTdb);   // polylignes échantillonnées via l'éphéméride
	void SetMapActive(bool bActive);      // bascule caméra + visibilité

	UPROPERTY() TObjectPtr<ASPSolarSystemMapActor> MapActor;
	UPROPERTY() TObjectPtr<ACameraActor> MapCamera;
	UPROPERTY() TObjectPtr<AActor> PreviousViewTarget;

	double LastOrbitEpoch = -1.0e300;
	int32 LastVehicleGen = -1;
	int32 LastGeoGen = -1;
	FVector LastCorridorCenter = FVector(1.0e18, 0.0, 0.0);
	FVector LastGeoCenter = FVector(1.0e18, 0.0, 0.0);
	bool bBuilt = false;
	bool bWasActive = false;
	bool bLastShowMoons = false;
};
