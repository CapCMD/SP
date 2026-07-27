// SPHud.h — LE HUD, 100 % NATIF UE5 (Slate). Remplace SPImGuiOverlay.
//
// Plus une seule ligne d'ImGui : tout ce que le joueur lit est peint par Slate
// (FSlateDrawElement) ou composé de vrais widgets Slate (le menu). Conséquences
// voulues, et c'est tout l'intérêt du passage :
//   . le HUD ne capte PLUS l'entrée — les couches d'information sont
//     `HitTestInvisible`, donc la souris et le clavier vont au monde UE
//     (cf. SPPlayerController) ; seul le MENU est interactif ;
//   . plus de rectangle plein écran opaque : le monde 3D est TOUJOURS visible
//     derrière le HUD, y compris sous le menu (fond étoilé + orbites ténues,
//     exactement comme docs/reference_solar_system_map/ref_menu.png).
//
// Le HUD ne calcule RIEN : il lit `fen::app::Session` (l'état de la partie) et
// `fen::app::g_render_bridge` (ce que le monde UE publie en retour : projection
// écran des corps, poste à portée). Doctrine du pont inchangée.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

namespace fen::app { struct Session; }   // C++ pur : jamais inclus depuis un .h UE

// ---------------------------------------------------------------------------
// LA COUCHE D'INFORMATION (carte + station). Entièrement peinte à la main :
// marqueurs des corps, libellés, barre de temps, invites de la station. Ne
// reçoit AUCUN événement (HitTestInvisible) — l'entrée appartient au monde.
class SSPWorldHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSPWorldHud) : _Session(nullptr) {}
		SLATE_ARGUMENT(fen::app::Session*, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	                      const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	                      int32 LayerId, const FWidgetStyle& InWidgetStyle,
	                      bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1280.0, 720.0); }

private:
	// Une passe de peinture par scène. `S` = échelle (hauteur / 720) : toutes les
	// cotes sont données dans le repère de la capture de référence.
	int32 PaintCarte(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) const;
	int32 PaintStation(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) const;
	int32 PaintPoste(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, int Poste) const;

	fen::app::Session* Session = nullptr;
};

// ---------------------------------------------------------------------------
// LE MENU (scène Titre) — de VRAIS widgets Slate : le clavier et la souris y
// fonctionnent nativement, sans passer par le pont.
// Format : docs/reference_solar_system_map/ref_menu.png.
class SSPMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSPMenu) : _Session(nullptr) {}
		SLATE_ARGUMENT(fen::app::Session*, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// L'échelle du menu se lit sur la géométrie ALLOUÉE, pas sur la taille en
	// pixels du viewport : Slate travaille en unités de mise en page, que le
	// facteur DPI de l'écran divise déjà. Confondre les deux rapetissait tout
	// d'un tiers sur un écran à 150 %.
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	                  const float InDeltaTime) override;

	// Le panneau change de contenu sans changer de scène (le fond reste le ciel).
	enum class EPage : uint8 { Racine, Nouvelle, Reprendre };

private:
	TSharedRef<SWidget> BuildPanel();
	void SetPage(EPage P);

	FReply OnNouvellePartie();
	FReply OnReprendre();
	FReply OnQuitter();
	FReply OnValiderNouvelle();
	FReply OnChargerSelection();
	FReply OnRetour();

	fen::app::Session* Session = nullptr;
	EPage Page = EPage::Racine;
	TSharedPtr<class SBox> PanelHost;
	TSharedPtr<class SEditableTextBox> NomBox;
	int32 ModeChoix = 0;                 // 0 = Normal (assistant), 1 = Pro
	float Echelle = 1.0f;                // hauteur allouée / 720
};

// ---------------------------------------------------------------------------
// LES MODALES (faillite, réglages) — posées PAR-DESSUS la scène, qui continue
// de vivre derrière. Interactives : ce sont des décisions, pas de l'affichage.
class SSPModal : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSPModal) : _Session(nullptr) {}
		SLATE_ARGUMENT(fen::app::Session*, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	                  const float InDeltaTime) override;

private:
	TSharedRef<SWidget> BuildContenu();
	void Rebuild();

	fen::app::Session* Session = nullptr;
	TSharedPtr<class SBox> Host;
	int32 ModalAffichee = -1;            // dernière modale construite
	float Echelle = 1.0f;
};

// ---------------------------------------------------------------------------
// UN POSTE DE TRAVAIL OUVERT (scène Station, touche E). INTERACTIF : c'est ici
// que le cœur C++ (arbre techno, catalogue de missions, fiabilité, flotte)
// remonte au joueur — les postes ne sont plus de simples fiches peintes. Chaque
// poste lit `Session::jeu` et la couche ARES (`jeu.ares.etat` : GameState).
// Format holographique de docs/reference_solar_system_map/ref_poste.png.
class SSPPoste : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSPPoste) : _Session(nullptr) {}
		SLATE_ARGUMENT(fen::app::Session*, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	                  const float InDeltaTime) override;

private:
	void Rebuild();
	TSharedRef<SWidget> BuildContenu(int Poste);
	// Un poste par méthode : chacun lit son coin du modèle, en lecture seule ou
	// avec ses actions propres (l'AGENCE lance des recherches, la PLANIFICATION
	// accepte des contrats).
	TSharedRef<SWidget> BuildAgence();          // arbre techno + file de recherche
	TSharedRef<SWidget> BuildPlanification();   // catalogue de missions + mails
	TSharedRef<SWidget> BuildControle();        // boucle de mission [GDD 4.1]
	TSharedRef<SWidget> BuildAnalyse();         // base de fiabilité
	TSharedRef<SWidget> BuildOperations();      // flotte en service
	TSharedRef<SWidget> BuildConception();      // atelier d'assemblage
	TSharedRef<SWidget> BuildInfo(int Poste);   // fiche lecture seule (autres postes)

	fen::app::Session* Session = nullptr;
	TSharedPtr<class SBox> Host;
	int32 PosteAffiche = -2;             // dernier poste construit (-2 = jamais)
	float Echelle = 1.0f;
};

// ---------------------------------------------------------------------------
// LE BANDEAU DU TEMPS — EN HAUT À DROITE, PARTOUT DANS LE MONDE.
//
// Le temps est la ressource la plus continue du jeu : il coule, il se PAIE
// [GDD 13.2, 14.2], et il doit donc être lisible et pilotable de partout — au plan
// système comme à bord, poste ouvert compris. Le réglage « officiel » reste au
// poste AGENCE (avec son chiffrage), et la barre de temps de la carte reste
// l'indicateur de la référence ; ce bandeau est la surface UNIVERSELLE.
//
// Ce n'est PAS le « curseur de temps » que [GDD 14] interdit : cinq CRANS discrets
// (`fen::game::TimeRate`), les mêmes que le poste et que les touches [P]/[1-5],
// qui passent par le système temporel de l'agence. Aucun accès à une date
// arbitraire.
//
// INTERACTIF SANS VOLER LE MONDE (piège n°6) : le bandeau et ses conteneurs sont
// `SelfHitTestInvisible` — seuls ses BOUTONS reçoivent la souris ; le reste de
// l'écran continue d'aller au monde 3D (orbite, zoom, picking).
class SSPTemps : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSPTemps) : _Session(nullptr) {}
		SLATE_ARGUMENT(fen::app::Session*, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	                  const float InDeltaTime) override;

private:
	fen::app::Session* Session = nullptr;
	float Echelle = 1.0f;                // hauteur allouée / 720 (piège n°19)
};

// ---------------------------------------------------------------------------
// LA RACINE : empile la couche d'information et le menu, et n'en montre que ce
// que la scène courante demande.
class SSPHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSPHud) : _Session(nullptr) {}
		SLATE_ARGUMENT(fen::app::Session*, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Le menu doit recevoir le focus clavier quand il apparaît.
	TSharedPtr<SSPMenu> GetMenu() const { return Menu; }

private:
	EVisibility MenuVisibility() const;
	EVisibility WorldVisibility() const;
	EVisibility ModalVisibility() const;
	EVisibility PosteVisibility() const;
	EVisibility TempsVisibility() const;

	fen::app::Session* Session = nullptr;
	TSharedPtr<SSPMenu> Menu;
	TSharedPtr<SSPWorldHud> WorldHud;
	TSharedPtr<SSPModal> Modale;
	TSharedPtr<SSPPoste> Poste;
	TSharedPtr<SSPTemps> Temps;
};
