// SPGameSubsystem.h — LE PROPRIÉTAIRE DE LA PARTIE, côté UE.
//
// Depuis le passage en rendu total UE5, ce subsystem remplace ce que faisait
// l'overlay ImGui : il POSSÈDE `fen::app::Session` (la partie en cours), la
// fait avancer d'une frame, monte le HUD Slate natif dans le viewport et confie
// l'entrée à ASPPlayerController. Le monde 3D (station, carte, ciel) lit ensuite
// le pont — doctrine inchangée, sens unique.
//
// Ordre d'une frame : Session::tick (état + publication du pont) -> subsystems
// de monde (station / carte / ciel) -> HUD (lecture seule).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SPGameSubsystem.generated.h"

class SSPHud;
struct FSPSessionHolder;   // pimpl : `fen::app::Session` est du C++ pur

UCLASS()
class SP_API USPGameSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// Déclarés hors-ligne : `FSPSessionHolder` est incomplet ici (pimpl), donc
	// le destructeur de TUniquePtr ne peut pas être généré dans cet entête.
	USPGameSubsystem();
	virtual ~USPGameSubsystem() override;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

private:
	void ArmerCapture();          // -spcapture : ouvre d'emblée la scène demandée
	void AppliquerAffichage();    // résolution / plein écran demandés par les réglages

	// Pointeur NU, volontairement : le code généré par UHT instancierait le
	// destructeur d'un TUniquePtr sur un type incomplet. Créé dans
	// OnWorldBeginPlay, détruit dans Deinitialize — durée de vie du monde.
	FSPSessionHolder* Holder = nullptr;
	TSharedPtr<SSPHud> Hud;
	bool bCaptureArmed = false;
	bool bCaptureContractAccepted = false;   // capture CONTROLE : accepte un contrat une fois
};
