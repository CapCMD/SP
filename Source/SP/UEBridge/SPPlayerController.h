// SPPlayerController.h — L'ENTRÉE, 100 % NATIVE UE5.
//
// C'ÉTAIT LE PIÈGE N°6 : l'overlay ImGui renvoyait `FReply::Handled()` sur tout
// le clavier et toute la souris, si bien que le monde UE ne recevait AUCUNE
// entrée ; caméra et ambulation devaient être commandées par le HUD. Le HUD
// natif est `HitTestInvisible` : l'entrée arrive ici, dans le pipeline UE, et
// ce contrôleur la traduit en commandes sur le pont.
//
// Le pont reste la frontière : ce fichier ÉCRIT dans `RenderBridge` (commandes
// de caméra, commandes d'ambulation, focus) et lit ce que le monde publie en
// retour (projection écran, poste à portée). Aucun recalcul de physique ici.
//
// Modes d'entrée par scène :
//   Titre   — UI seule, curseur visible : le menu Slate est le seul interactif.
//   Station — jeu seul, curseur CAPTURÉ : regard à la souris, ZQSD/WASD.
//   Carte   — jeu + UI, curseur visible : molette = zoom, glisser = orbite,
//             clic = focus (le picking se fait sur la projection publiée).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "SPPlayerController.generated.h"

namespace fen::app { struct Session; }   // C++ pur : jamais inclus depuis un .h UE

UCLASS()
class SP_API ASPPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASPPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	// Le subsystem de jeu injecte la partie en cours (il en est propriétaire).
	void SetSession(fen::app::Session* InSession) { Session = InSession; }

private:
	void AppliquerModeEntree();          // curseur / capture selon la scène
	void TickStation(float DeltaTime);   // ambulation première personne
	void TickCarte(float DeltaTime);     // caméra orbitale + picking
	int  CorpsSousCurseur() const;       // picking sur la projection publiée

	fen::app::Session* Session = nullptr;
	int32 SceneAppliquee = -1;           // dernière scène pour laquelle le mode a été posé
	bool  bGlisse = false;               // glissement caméra commencé (carte)
	FVector2D PosClicBas = FVector2D::ZeroVector;
	// Anti-répétition : une touche ne doit agir qu'au FRONT descendant.
	bool bPrecM = false, bPrecE = false, bPrecF5 = false, bPrecEsc = false;
};

// Le mode de jeu n'existe que pour imposer NOTRE contrôleur : sans lui, UE
// instancie un APlayerController nu et l'entrée native n'atteint personne.
// Déclaré dans Config/DefaultEngine.ini (GlobalDefaultGameMode).
UCLASS()
class SP_API ASPGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASPGameMode();
};
