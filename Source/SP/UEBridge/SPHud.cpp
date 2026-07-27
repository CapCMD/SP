// SPHud.cpp — LE HUD NATIF UE5. Zéro ImGui.
//
// Toutes les cotes de ce fichier sont données dans le repère des captures de
// référence (1280 x 720) et multipliées par S = hauteur / 720 : le HUD suit la
// résolution sans qu'aucun chiffre ne devienne « magique ».
// Références : docs/reference_solar_system_map/ref_menu.png (menu),
// ref_systeme.png (carte), ref_iss.png (station), ref_poste.png (poste).

// Les entêtes du jeu AVANT tout entête UE (macros PI/check, cf. SP.Build.cs).
#include "app/session.hpp"
#include "app/postes.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/ephem/Ephemeris.hpp"
#include "fen/vehicle/PartsCatalog.hpp"

#include "SPHud.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SPHud"

namespace
{
using fen::app::g_render_bridge;

// --- palette (valeurs relevées AU PIXEL sur les captures de référence) ------
// ATTENTION : les teintes Slate sont LINÉAIRES, l'écriture finale repasse en
// sRGB. Donner directement 0,04 pour un panneau presque noir le rendait gris
// moyen. Les couleurs sont donc saisies telles qu'on les MESURE (sRGB, 0-255)
// et converties une fois pour toutes.
FLinearColor SRGB(uint8 R, uint8 G, uint8 B, float A = 1.0f)
{
	FLinearColor C = FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
	C.A = A;
	return C;
}

const FLinearColor ColTexte       = SRGB(184, 196, 208);
const FLinearColor ColTexteFaible = SRGB(124, 132, 142);
const FLinearColor ColTitre       = SRGB(238, 242, 247);
const FLinearColor ColAccentBleu  = SRGB( 47, 127, 216);
const FLinearColor ColVert        = SRGB( 52, 201,  75);
const FLinearColor ColPanneau     = SRGB( 10,  15,  24, 0.86f);
const FLinearColor ColBordure     = SRGB( 30,  39,  51);
const FLinearColor ColBouton      = SRGB( 17,  26,  36, 0.92f);
const FLinearColor ColBoutonSurvol= SRGB( 27,  40,  54, 0.96f);

// La police du jeu de référence est une MONOSPACE technique, très espacée.
// UE fournit DroidSansMono sous le nom de fonte « Mono ».
FSlateFontInfo Mono(float Size, int32 Tracking = 0)
{
	FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle("Mono", FMath::Max(1, FMath::RoundToInt(Size)));
	F.LetterSpacing = Tracking;
	return F;
}

FVector2D Mesure(const FString& Txt, const FSlateFontInfo& F)
{
	return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Txt, F);
}

const FSlateBrush* Plein() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }

void Texte(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& Txt,
           const FSlateFontInfo& F, const FVector2D& Pos, const FLinearColor& Col)
{
	FSlateDrawElement::MakeText(
		Out, Layer,
		G.ToPaintGeometry(FVector2f(1.0f, 1.0f), FSlateLayoutTransform(FVector2f(Pos))),
		Txt, F, ESlateDrawEffect::None, Col);
}

// Texte avec ombre portée. L'intérieur de l'ISS est presque BLANC : le HUD y
// disparaissait purement et simplement (ImGui masquait le problème avec ses
// fonds semi-opaques). Un liseré sombre décalé d'un pixel suffit, et laisse la
// facture de la référence intacte.
void TexteOmbre(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& Txt,
                const FSlateFontInfo& F, const FVector2D& Pos, const FLinearColor& Col, float S)
{
	const FLinearColor Ombre(0.0f, 0.0f, 0.0f, 0.55f);
	FSlateDrawElement::MakeText(
		Out, Layer,
		G.ToPaintGeometry(FVector2f(1.0f, 1.0f),
		                  FSlateLayoutTransform(FVector2f(Pos + FVector2D(S, S)))),
		Txt, F, ESlateDrawEffect::None, Ombre);
	FSlateDrawElement::MakeText(
		Out, Layer + 1,
		G.ToPaintGeometry(FVector2f(1.0f, 1.0f), FSlateLayoutTransform(FVector2f(Pos))),
		Txt, F, ESlateDrawEffect::None, Col);
}

void Boite(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Pos,
          const FVector2D& Size, const FLinearColor& Col)
{
	FSlateDrawElement::MakeBox(
		Out, Layer,
		G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos))),
		Plein(), ESlateDrawEffect::None, Col);
}

void Ligne(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& A,
           const FVector2D& B, const FLinearColor& Col, float Epaisseur)
{
	TArray<FVector2D> Pts = {A, B};
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None,
	                             Col, true, Epaisseur);
}

// Cercle en polyligne : Slate n'a pas de primitive de cercle, et les marqueurs
// de la référence font 5 à 7 px — 20 segments suffisent largement.
void Cercle(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& C,
            float R, const FLinearColor& Col, float Epaisseur, bool bPlein = false)
{
	// Le nombre de segments suit le RAYON : 20 suffisent pour un marqueur de
	// 5 px, mais donnaient un polygone bien visible autour d'une planète qui
	// remplit l'écran.
	const int32 SEG = FMath::Clamp(FMath::RoundToInt(R * 1.2f), 20, 128);
	TArray<FVector2D> Pts;
	Pts.Reserve(SEG + 1);
	for (int32 k = 0; k <= SEG; ++k)
	{
		const double A = 2.0 * PI * k / SEG;
		Pts.Add(C + FVector2D(R * FMath::Cos(A), R * FMath::Sin(A)));
	}
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None,
	                             Col, true, Epaisseur);
}

// Disque plein. Slate ne remplit pas les polygones : on passe par une brosse
// à coins arrondis, dont le rayon d'arrondi est borné à la moitié de la boîte —
// une boîte carrée devient donc un cercle net et antialiasé. (Un cercle tracé
// avec une grosse épaisseur donnait une étoile : les segments se recouvrent.)
void Disque(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& C,
            float R, const FLinearColor& Col)
{
	// statique : FSlateDrawElement conserve la brosse le temps du rendu.
	static const FSlateRoundedBoxBrush Rond(FLinearColor::White, 1000.0f);
	FSlateDrawElement::MakeBox(
		Out, Layer,
		G.ToPaintGeometry(FVector2f(2 * R, 2 * R),
		                  FSlateLayoutTransform(FVector2f(C - FVector2D(R, R)))),
		&Rond, ESlateDrawEffect::None, Col);
}

// Cadre creux (4 traits) : moins cher qu'une bordure de brosse et net au pixel.
void Cadre(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& P,
           const FVector2D& S, const FLinearColor& Col, float E)
{
	Boite(Out, Layer, G, P, FVector2D(S.X, E), Col);
	Boite(Out, Layer, G, FVector2D(P.X, P.Y + S.Y - E), FVector2D(S.X, E), Col);
	Boite(Out, Layer, G, P, FVector2D(E, S.Y), Col);
	Boite(Out, Layer, G, FVector2D(P.X + S.X - E, P.Y), FVector2D(E, S.Y), Col);
}

// --- LE TEMPS DE JEU, tel que la référence l'affiche ------------------------
// « JUL 24, 2026 » + « 11:14:37 AM ». L'époque est en secondes TDB depuis J2000
// (2000-01-01T12:00) : la conversion passe par le calendrier grégorien exact
// d'astro_core (civil_from_days), pas par une approximation.
void DateHeure(double EpochTdb, FString& OutDate, FString& OutHeure)
{
	static const TCHAR* MOIS[12] = {TEXT("JAN"), TEXT("FEB"), TEXT("MAR"), TEXT("APR"),
	                                TEXT("MAY"), TEXT("JUN"), TEXT("JUL"), TEXT("AUG"),
	                                TEXT("SEP"), TEXT("OCT"), TEXT("NOV"), TEXT("DEC")};
	const double T = EpochTdb + 43200.0;                       // depuis 2000-01-01T00:00
	const double Jours = FMath::FloorToDouble(T / 86400.0);
	const double Secs = T - Jours * 86400.0;
	const fen::CivilDate C =
		fen::civil_from_days(fen::DAYS_1970_TO_20000101 + static_cast<long long>(Jours));
	const int32 H24 = FMath::Clamp(static_cast<int32>(Secs / 3600.0), 0, 23);
	const int32 M = FMath::Clamp(static_cast<int32>(FMath::Fmod(Secs, 3600.0) / 60.0), 0, 59);
	const int32 Sec = FMath::Clamp(static_cast<int32>(FMath::Fmod(Secs, 60.0)), 0, 59);
	const int32 H12 = (H24 % 12 == 0) ? 12 : (H24 % 12);
	OutDate = FString::Printf(TEXT("%s %d, %d"), MOIS[FMath::Clamp<int32>(C.m - 1, 0, 11)],
	                          static_cast<int32>(C.d), C.y);
	OutHeure = FString::Printf(TEXT("%02d:%02d:%02d %s"), H12, M, Sec,
	                           H24 < 12 ? TEXT("AM") : TEXT("PM"));
}

FSlateBrush Brosse(const FLinearColor& Col)
{
	FSlateBrush B = *Plein();
	B.TintColor = FSlateColor(Col);
	return B;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// LA COUCHE D'INFORMATION
// ═══════════════════════════════════════════════════════════════════════════
void SSPWorldHud::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	// Le HUD n'est QUE de l'information : il ne doit jamais voler un clic au
	// monde 3D (c'est exactement ce que faisait l'overlay ImGui — piège n°6).
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SSPWorldHud::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& CullRect,
                           FSlateWindowElementList& Out, int32 LayerId,
                           const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!Session) return LayerId;
	// Un seul Monde : le CADRAGE décide quel HUD peindre (système = ex-carte,
	// bord = ex-station). Titre : le menu peint, pas cette couche.
	if (Session->scene != fen::app::SceneJeu::Monde) return LayerId;
	// EN TRANSIT (vol de caméra [M], incr. 3c-3) : aucun HUD. Les deux plans se
	// relaient au milieu du vol ; peindre le fil d'Ariane et la barre de temps
	// par-dessus une caméra qui plonge dans la station referait exactement la
	// coupure qu'on vient de supprimer.
	if (Session->vol_cam.actif) return LayerId;
	return (Session->cadrage == fen::app::Cadrage::Systeme)
		? PaintCarte(G, Out, LayerId + 1)
		: PaintStation(G, Out, LayerId + 1);
}

// ---------------------------------------------------------------------------
// LA CARTE — HUD MINIMAL, format ref_systeme.png :
//   « < SYSTEME SOLAIRE » en haut-gauche, et rien d'autre que les marqueurs des
//   corps. AUCUN panneau latéral (le gros panneau ImGui est supprimé).
//   La barre de temps du bas et le témoin « LIVE » de la référence ont été
//   RETIRÉS le 2026-07-27 : le bandeau permanent les répétait (voir plus bas).
int32 SSPWorldHud::PaintCarte(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) const
{
	const FVector2D Taille = G.GetLocalSize();
	const float W = static_cast<float>(Taille.X), H = static_cast<float>(Taille.Y);
	const float S = H / 720.0f;
	auto& B = g_render_bridge;

	// --- marqueurs et libellés des corps -------------------------------------
	// À l'échelle VRAIE une planète est sous-pixellique de loin : le monde UE
	// publie sa projection écran, le HUD prend le relais [doctrine du pont].
	{
		const FSlateFontInfo FLabel = Mono(10.0f * S, 70);
		const int32 N = FMath::Min(B.screen.n.load(), fen::app::RenderBridge::ScreenBodies::MAX);
		const int Focus = B.focus_body.load();
		const int Survol = B.hover_body.load();
		for (int32 i = 0; i < N; ++i)
		{
			const auto& It = B.screen.items[i];
			if (!It.on_screen) continue;
			const FVector2D P(It.nx * W, It.ny * H);
			const float RPx = It.r_norm * W;               // rayon apparent RÉEL
			const bool bFocus = (Focus == It.body);
			const bool bSurvol = (Survol == It.body);

			// Marqueur : cercle constant tant que le corps est plus petit que lui ;
			// au-delà, le corps se voit tout seul et le marqueur s'efface.
			const float RMarq = 5.5f * S;
			const FLinearColor Col = bFocus  ? SRGB(255, 220, 120)
			                       : bSurvol ? SRGB(217, 237, 255)
			                                 : SRGB(158, 184, 219);
			if (RPx < RMarq)
			{
				Cercle(Out, Layer, G, P, RMarq, Col, 1.2f * S);
				Boite(Out, Layer, G, P - FVector2D(1.0 * S, 1.0 * S),
				     FVector2D(2.0 * S, 2.0 * S), Col);
			}
			else if ((bFocus || bSurvol) && RPx < H * 0.14f)
			{
				// Le cerne ne sert qu'à DÉSIGNER un corps qu'on distingue mal.
				// Dès qu'il occupe une bonne part de l'écran, il se désigne tout
				// seul : un anneau autour d'une planète pleine page fait faux.
				Cercle(Out, Layer, G, P, RPx + 6.0f * S, Col * FLinearColor(1, 1, 1, 0.7f), 1.4f * S);
			}

			// NOVELLUS n'est pas un corps du catalogue : nom en dur (pas de
			// body_name hors enum).
			const FString Nom = (It.body == fen::app::FOCUS_STATION)
			                        ? FString(TEXT("NOVELLUS"))
			                        : FString(fen::ephem::body_name((fen::ephem::Body)It.body)).ToUpper();
			const FVector2D Sz = Mesure(Nom, FLabel);
			Texte(Out, Layer + 1, G, Nom,
			      FLabel, FVector2D(P.X + FMath::Max(RPx, RMarq) + 7.0f * S, P.Y - Sz.Y * 0.5),
			      bFocus ? SRGB(255, 237, 189) : ColTexte);
		}
	}

	// --- NOVELLUS focalisé : gros plan + « [M] ENTRER » [réf. ref_issfocus.png] --
	// Le vol de caméra [M] fait entrer à bord depuis la vue système : ici on
	// ANNONCE l'affordance quand la station est cadrée. C'est une affordance de
	// GROS PLAN : elle ne se peint que si la station se DISTINGUE, au même critère
	// que le LOD du modèle extérieur (envergure / distance de vue). Sans ce garde,
	// le titre s'affichait plein écran alors que Novellus est sous-pixellique.
	{
		const double DistVueKm = B.cam.dist_km.load();
		const double EnvKm = B.station.envergure_m * 0.001;
		const bool bGrosPlan = B.focus_body.load() == fen::app::FOCUS_STATION &&
		                       DistVueKm > 0.0 && (EnvKm / DistVueKm) > 3.0e-3;
		if (bGrosPlan)
		{
			const float CX = W * 0.5f;
			const FString Titre  = TEXT("NOVELLUS");
			const FString Sous   = TEXT("ORBITE TERRESTRE BASSE  -  418 km");
			const FString Entrer = TEXT("[ M ]  ENTRER");
			const FSlateFontInfo FTitre = Mono(15.0f * S, 200);
			const FSlateFontInfo FSous  = Mono(10.0f * S, 90);
			const FSlateFontInfo FEnt   = Mono(13.0f * S, 160);
			const FVector2D SzT = Mesure(Titre, FTitre);
			const FVector2D SzS = Mesure(Sous, FSous);
			const FVector2D SzE = Mesure(Entrer, FEnt);
			Texte(Out, Layer + 3, G, Titre,  FTitre, FVector2D(CX - SzT.X * 0.5, 548.0 * S), ColTexte);
			Texte(Out, Layer + 3, G, Sous,   FSous,  FVector2D(CX - SzS.X * 0.5, 572.0 * S), ColTexteFaible);
			Texte(Out, Layer + 3, G, Entrer, FEnt,   FVector2D(CX - SzE.X * 0.5, 598.0 * S), ColVert);
		}
	}

	// --- « < SYSTEME SOLAIRE » : le fil d'Ariane (haut-gauche) ---------------
	Texte(Out, Layer + 2, G, TEXT("<  SYSTEME SOLAIRE"), Mono(10.0f * S, 80),
	      FVector2D(34.0 * S, 26.0 * S), ColTexte);

	// --- PLUS DE BARRE DE TEMPS NI DE « LIVE » EN BAS (décision du 2026-07-27) --
	// La référence `ref_systeme.png` porte une barre de temps en bas-centre
	// (date | cadence | heure + rail) et un témoin « LIVE » en bas-gauche. Les
	// deux ont été retirés : depuis que le BANDEAU DU TEMPS (`SSPTemps`,
	// haut-droite) est permanent et présent dans TOUT le Monde, ils disaient la
	// même chose une seconde fois, sur ce seul écran — et le bandeau en dit
	// davantage (les crans sont cliquables, le plafond de mission y est visible
	// [GDD 14.3]). « LIVE » suivait le même sort : l'horloge du bandeau EST
	// l'état courant, l'annoncer une seconde fois n'apprend rien.
	// DIVERGENCE ASSUMÉE d'avec la référence, à ne pas « recorriger » plus tard :
	// la référence n'avait pas de bandeau permanent, elle avait besoin de cette
	// barre. Le HUD de la carte reste MINIMAL — c'est même l'esprit du format.

	// --- approximations DÉCLARÉES [GDD 6.8] ----------------------------------
	// Le HUD est minimal, mais une approximation non déclarée reste interdite :
	// une ligne, discrète, en bas à droite.
	{
		const FSlateFontInfo FNote = Mono(8.0f * S, 30);
		const FString Note = TEXT("ECHELLE REELLE 1 u = 1 cm  .  orientation = IAU WGCCRE (obliquite reelle)");
		// La mesure de Slate ignore l'interlettrage : on le rajoute à la main,
		// sinon la note déborde par la droite.
		const double Sz = Mesure(Note, FNote).X + Note.Len() * FNote.LetterSpacing * FNote.Size / 1000.0;
		Texte(Out, Layer + 2, G, Note, FNote, FVector2D(W - Sz - 18.0 * S, 700.0 * S),
		      SRGB(102, 115, 133, 0.75f));
	}

	return Layer + 4;
}

// ---------------------------------------------------------------------------
// LA STATION — première personne à bord, format ref_iss.png.
int32 SSPWorldHud::PaintStation(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) const
{
	const FVector2D Taille = G.GetLocalSize();
	const float W = static_cast<float>(Taille.X), H = static_cast<float>(Taille.Y);
	const float S = H / 720.0f;
	auto& B = g_render_bridge;
	const fen::app::Jeu& Jeu = Session->jeu;

	const FSlateFontInfo FBarre = Mono(9.0f * S, 50);
	// L'entrée est NATIVE depuis le passage en rendu total UE5 : la souris est
	// capturée, on ne « glisse » plus pour regarder.
	// L'état du temps n'est PAS répété ici : c'est le rôle du bandeau permanent en
	// haut à droite (`SSPTemps`), présent aux deux cadrages.
	const FString Haut = FString::Printf(
		TEXT("%s   |   ZQSD/WASD se deplacer   |   SOURIS : regarder   |   "
		     "E : poste   |   M : carte   |   F5 : sauvegarder"),
		*FString(Jeu.agence.nom.c_str()));
	TexteOmbre(Out, Layer, G, Haut, FBarre, FVector2D(18.0 * S, 14.0 * S), SRGB(226, 234, 243), S);
	TexteOmbre(Out, Layer, G, TEXT("CARTE  [ M ]"), Mono(10.0f * S, 80),
	           FVector2D(18.0 * S, H - 30.0f * S), SRGB(226, 234, 243), S);

	// --- invite contextuelle : UE mesure la proximité, le HUD affiche ---------
	int NPoste = 0;
	const fen::app::PosteDef* P = fen::app::postes_def(NPoste);
	const int Proche = B.station_out.near_post.load();
	if (Session->poste_ouvert < 0 && Proche >= 0 && Proche < NPoste)
	{
		const FSlateFontInfo FInv = Mono(11.0f * S, 70);
		const FString Inv = FString::Printf(TEXT("[ E ]  OUVRIR  --  %s"),
		                                    *FString(P[Proche].label));
		const FVector2D Sz = Mesure(Inv, FInv);
		const FVector2D C(W * 0.5 - Sz.X * 0.5, H * 0.78);
		const FVector2D Pad(16.0 * S, 8.0 * S);
		const FLinearColor Accent =
			SRGB(P[Proche].accent.r, P[Proche].accent.g, P[Proche].accent.b);
		Boite(Out, Layer, G, C - Pad, Sz + Pad * 2.0, SRGB(10, 14, 20, 0.80f));
		Cadre(Out, Layer + 1, G, C - Pad, Sz + Pad * 2.0, Accent, 1.5f * S);
		Texte(Out, Layer + 2, G, Inv, FInv, C, SRGB(235, 245, 255));
	}

	if (!B.station_out.ready.load())
	{
		const FSlateFontInfo FC = Mono(11.0f * S, 70);
		const FString T = TEXT("Chargement de la station...");
		const FVector2D Sz = Mesure(T, FC);
		TexteOmbre(Out, Layer + 2, G, T, FC, FVector2D(W * 0.5 - Sz.X * 0.5, H * 0.5), SRGB(226, 234, 243), S);
	}

	// Le POSTE OUVERT n'est plus peint ici : c'est le widget interactif SSPPoste
	// (empilé par SSPHud) qui le rend, pour que le joueur puisse cliquer dedans
	// (arbre, catalogue...). PaintPoste ne sert plus qu'à la capture headless de
	// référence, invoquée à part.
	return Layer + 3;
}

// ---------------------------------------------------------------------------
// LE PANNEAU D'UN POSTE — holographique, liseré d'accent, format ref_poste.png.
// Contenu VIVANT tiré du modèle de jeu (lecture seule), poste par poste.
int32 SSPWorldHud::PaintPoste(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer,
                              int Poste) const
{
	int N = 0;
	const fen::app::PosteDef* Defs = fen::app::postes_def(N);
	if (Poste < 0 || Poste >= N) return Layer;
	const fen::app::PosteDef& D = Defs[Poste];

	const FVector2D Taille = G.GetLocalSize();
	const float W = static_cast<float>(Taille.X), H = static_cast<float>(Taille.Y);
	const float S = H / 720.0f;
	const FVector2D Sz(FMath::Min(600.0f * S, W * 0.55f), FMath::Min(300.0f * S, H * 0.48f));
	const FVector2D P((W - Sz.X) * 0.5, (H - Sz.Y) * 0.5);
	const FLinearColor Accent = SRGB(D.accent.r, D.accent.g, D.accent.b);

	Boite(Out, Layer, G, P, Sz, SRGB(15, 20, 33, 0.94f));
	Cadre(Out, Layer + 1, G, P, Sz, Accent, 2.0f * S);

	const FSlateFontInfo FTitre = Mono(19.0f * S, 120);
	const FSlateFontInfo FSub = Mono(10.0f * S, 90);
	const FSlateFontInfo FKV = Mono(12.0f * S, 40);
	float Y = P.Y + 16.0f * S;
	Texte(Out, Layer + 2, G, FString(D.label), FTitre, FVector2D(P.X + 20.0 * S, Y), Accent);
	{
		const FString EnLigne = TEXT("* EN LIGNE");
		const FVector2D SzE = Mesure(EnLigne, FSub);
		Texte(Out, Layer + 2, G, EnLigne, FSub,
		      FVector2D(P.X + Sz.X - SzE.X - 20.0 * S, Y + 6.0 * S),
		      SRGB(140, 217, 242));
	}
	Y += 26.0f * S;
	Texte(Out, Layer + 2, G, FString(D.sub), FSub, FVector2D(P.X + 20.0 * S, Y), ColTexteFaible);
	Y += 22.0f * S;
	Boite(Out, Layer + 2, G, FVector2D(P.X + 20.0 * S, Y), FVector2D(Sz.X - 40.0 * S, 1.0 * S),
	     SRGB(64, 77, 97, 0.9f));
	Y += 12.0f * S;

	// une ligne clé -> valeur, valeur alignée à droite
	auto KV = [&](const FString& K, const FString& V) {
		Texte(Out, Layer + 2, G, K, FKV, FVector2D(P.X + 20.0 * S, Y), ColTexte);
		const FVector2D SzV = Mesure(V, FKV);
		Texte(Out, Layer + 2, G, V, FKV, FVector2D(P.X + Sz.X - 20.0 * S - SzV.X, Y), ColTitre);
		Y += 20.0f * S;
	};
	auto Barre = [&](float Frac, const FString& Legende) {
		Y += 6.0f * S;
		const FVector2D BP(P.X + 20.0 * S, Y);
		const FVector2D BS(Sz.X - 40.0 * S, 6.0 * S);
		Boite(Out, Layer + 2, G, BP, BS, SRGB(31, 38, 51));
		Boite(Out, Layer + 3, G, BP, FVector2D(BS.X * FMath::Clamp(Frac, 0.0f, 1.0f), BS.Y), Accent);
		Y += BS.Y + 4.0f * S;
		Texte(Out, Layer + 2, G, Legende, FSub, FVector2D(P.X + 20.0 * S, Y), ColTexteFaible);
		Y += 16.0f * S;
	};

	const fen::app::Jeu& J = Session->jeu;
	const fen::app::Agence& A = J.agence;
	const int Vols = A.reussites + A.echecs;
	const FString Id(D.id);
	if (Id == TEXT("agence"))
	{
		KV(TEXT("PROGRAMME"), FString(A.nom.c_str()));
		KV(TEXT("TRESORERIE"), FString::Printf(TEXT("%.1f M$"), A.tresorerie));
		KV(TEXT("CALENDRIER"), FString::Printf(TEXT("T+%.0f mois"), A.mois));
		KV(TEXT("MODE D'AIDE"), A.mode == fen::app::ModeAide::Pro ? TEXT("PRO") : TEXT("NORMAL"));
		Barre(static_cast<float>(A.confiance), TEXT("Confiance ARES"));
	}
	else if (Id == TEXT("analyse"))
	{
		KV(TEXT("VOLS BOUCLES"), FString::Printf(TEXT("%d"), Vols));
		KV(TEXT("REUSSITES"), FString::Printf(TEXT("%d"), A.reussites));
		KV(TEXT("ECHECS"), FString::Printf(TEXT("%d"), A.echecs));
		KV(TEXT("DONNEES"), FString::Printf(TEXT("%.1f Gbit"), J.donnees_gbit));
		KV(TEXT("ECHANTILLONS"), FString::Printf(TEXT("%.1f kg"), J.echantillons_kg));
		Barre(Vols > 0 ? static_cast<float>(A.reussites) / Vols : 0.0f, TEXT("Taux de reussite"));
	}
	else if (Id == TEXT("operations"))
	{
		KV(TEXT("RELAIS GEO"), FString::Printf(TEXT("%d"), J.relais_geo));
		KV(TEXT("ORBITEURS MARS"), FString::Printf(TEXT("%d"), J.orbiteurs_mars));
		KV(TEXT("SONDES LOINT."), FString::Printf(TEXT("%d"), J.sondes_lointaines));
		KV(TEXT("REVENU FLOTTE"), FString::Printf(TEXT("%.1f Gbit/mo"), J.revenu_mensuel_gbit()));
		KV(TEXT("ORBITE"), TEXT("LEO 418 km"));
	}
	else if (Id == TEXT("planification"))
	{
		if (const fen::app::Contrat* C = J.actif())
		{
			KV(TEXT("CONTRAT"), FString(C->titre.c_str()));
			KV(TEXT("CLIENT"), FString(C->client.c_str()));
			KV(TEXT("PRIME"), FString::Printf(TEXT("%.0f M$"), C->prime_succes));
		}
		else
		{
			KV(TEXT("APPELS D'OFFRE"),
			   FString::Printf(TEXT("%d offres"), static_cast<int32>(J.contrats.size())));
			if (!J.contrats.empty())
				KV(TEXT("PROCHAIN"), FString(J.contrats.front().titre.c_str()));
		}
	}
	else if (Id == TEXT("vigie"))
	{
		KV(TEXT("MODULE"), TEXT("VIGIE"));
		KV(TEXT("STATION"), TEXT("NOVELLUS"));
		KV(TEXT("ORDINATEUR"), TEXT("EN LIGNE"));
		KV(TEXT("CARTE"), TEXT("SYSTEME SOLAIRE [M]"));
	}
	else
	{
		Texte(Out, Layer + 2, G, TEXT("Poste en cours de portage."), FKV,
		      FVector2D(P.X + 20.0 * S, Y), ColTexteFaible);
	}

	// pied de panneau
	const float YPied = P.Y + Sz.Y - 26.0f * S;
	Boite(Out, Layer + 2, G, FVector2D(P.X + 20.0 * S, YPied - 8.0 * S),
	     FVector2D(Sz.X - 40.0 * S, 1.0 * S), SRGB(64, 77, 97, 0.9f));
	Texte(Out, Layer + 2, G, TEXT("[ ECHAP ]  FERMER"), FSub,
	      FVector2D(P.X + 20.0 * S, YPied), ColTexteFaible);
	return Layer + 4;
}

// ═══════════════════════════════════════════════════════════════════════════
// LE MENU (scène Titre) — de VRAIS widgets Slate, format ref_menu.png.
// Le fond n'est PAS peint : c'est le monde UE (ciel étoilé + orbites ténues)
// qui reste visible derrière, comme dans la référence.
// ═══════════════════════════════════════════════════════════════════════════
void SSPMenu::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;

	// Le menu est dessiné aux cotes de la référence (1280 x 720) et mis à
	// l'échelle du viewport : une seule série de chiffres pour toutes les
	// résolutions.
	ChildSlot
	[
		SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return Echelle; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 110, 0, 0).HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Titre", "SPACE PROGRAM"))
				.Font(Mono(34.0f, 280))
				.ColorAndOpacity(FSlateColor(ColTitre))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0).HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(468.0f).HeightOverride(2.0f)
				[
					SNew(SBorder).BorderImage(Plein()).BorderBackgroundColor(FSlateColor(ColAccentBleu))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0).HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SousTitre", "AGENCE SPATIALE  -  QG A BORD DE L'ISS"))
				.Font(Mono(11.0f, 150))
				.ColorAndOpacity(FSlateColor(ColTexteFaible))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 130, 0, 0).HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(520.0f).HeightOverride(364.0f)
				[
					SNew(SBorder)
					.BorderImage(Plein())
					.BorderBackgroundColor(FSlateColor(ColPanneau))
					.Padding(FMargin(1.0f))
					[
						SAssignNew(PanelHost, SBox)[BuildPanel()]
					]
				]
			]
		]
	];
}

void SSPMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
                   const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// Géométrie ALLOUÉE, pas pixels du viewport : Slate a déjà divisé par le
	// facteur DPI de l'écran. Sur un 4K à 150 %, la différence est d'un tiers.
	const double Hauteur = AllottedGeometry.GetLocalSize().Y;
	if (Hauteur > 1.0) Echelle = static_cast<float>(Hauteur / 720.0);
}

void SSPMenu::SetPage(EPage P)
{
	Page = P;
	if (PanelHost.IsValid()) PanelHost->SetContent(BuildPanel());
}

namespace
{
// Un bouton du menu : plein sombre, libellé monospace centré, sans arrondi —
// exactement la facture de la référence.
TSharedRef<SWidget> BoutonMenu(const FText& Libelle, FOnClicked OnClick, float Hauteur = 46.0f)
{
	static FButtonStyle Style = []()
	{
		FButtonStyle St;
		St.SetNormal(Brosse(ColBouton));
		St.SetHovered(Brosse(ColBoutonSurvol));
		St.SetPressed(Brosse(SRGB(8, 12, 18, 0.96f)));
		St.SetNormalPadding(FMargin(0));
		St.SetPressedPadding(FMargin(0));
		return St;
	}();

	return SNew(SBox).HeightOverride(Hauteur)
	[
		SNew(SButton)
		.ButtonStyle(&Style)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked(OnClick)
		[
			SNew(STextBlock)
			.Text(Libelle)
			.Font(Mono(11.0f, 150))
			.ColorAndOpacity(FSlateColor(ColTexte))
		]
	];
}

// --- helpers de POSTE -------------------------------------------------------
FSlateColor AccentColor(const fen::app::PosteDef& D)
{
	return FSlateColor(SRGB(D.accent.r, D.accent.g, D.accent.b));
}

// Un texte simple, taille et couleur au choix.
TSharedRef<SWidget> Txt(const FString& S, float Size, const FLinearColor& Col,
                        int32 Tracking = 40)
{
	return SNew(STextBlock).Text(FText::FromString(S)).Font(Mono(Size, Tracking))
		.ColorAndOpacity(FSlateColor(Col));
}

// Une ligne clé -> valeur, valeur alignée à droite (facture de ref_poste.png).
TSharedRef<SWidget> LigneKV(const FString& K, const FString& V,
                            const FLinearColor& ColV = ColTitre)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[ Txt(K, 11.0f, ColTexte) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ Txt(V, 11.0f, ColV) ];
}

// Petit bouton d'ACTION dans un poste (compact, liseré discret).
// Le style des boutons d'action, partagé (postes ET bandeau du temps) : une seule
// définition, donc une seule apparence.
const FButtonStyle& StyleBouton()
{
	static FButtonStyle St = []() {
		FButtonStyle B;
		B.SetNormal(Brosse(ColBouton));
		B.SetHovered(Brosse(ColBoutonSurvol));
		B.SetPressed(Brosse(SRGB(8, 12, 18, 0.96f)));
		B.SetNormalPadding(FMargin(8, 3));
		B.SetPressedPadding(FMargin(8, 3));
		return B;
	}();
	return St;
}

TSharedRef<SWidget> BoutonAction(const FText& Libelle, FOnClicked OnClick,
                                 bool Enabled = true)
{
	return SNew(SButton).ButtonStyle(&StyleBouton()).IsEnabled(Enabled)
		.HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(OnClick)
		[ Txt(Libelle.ToString(), 9.0f, Enabled ? ColTexte : ColTexteFaible, 60) ];
}

// Bouton MINUSCULE à largeur fixe (les +/- et < > de l'atelier d'assemblage).
TSharedRef<SWidget> BoutonMini(const FString& Libelle, FOnClicked OnClick, float Width = 22.0f)
{
	static FButtonStyle St = []() {
		FButtonStyle B;
		B.SetNormal(Brosse(ColBouton));
		B.SetHovered(Brosse(ColBoutonSurvol));
		B.SetPressed(Brosse(SRGB(8, 12, 18, 0.96f)));
		B.SetNormalPadding(FMargin(0, 1));
		B.SetPressedPadding(FMargin(0, 1));
		return B;
	}();
	return SNew(SBox).WidthOverride(Width)
	[
		SNew(SButton).ButtonStyle(&St).HAlign(HAlign_Center).VAlign(VAlign_Center)
		.OnClicked(OnClick)
		[ Txt(Libelle, 10.0f, ColTexte, 0) ]
	];
}

// La CHROME holographique commune à tous les postes : cadre à liseré d'accent,
// titre + sous-titre, pastille EN LIGNE, contenu, pied « [ECHAP] FERMER ».
TSharedRef<SWidget> FramePoste(const fen::app::PosteDef& D, TSharedRef<SWidget> Contenu)
{
	return SNew(SBox).WidthOverride(720.0f).HeightOverride(540.0f)
	[
		SNew(SBorder).BorderImage(Plein()).BorderBackgroundColor(AccentColor(D))
		.Padding(FMargin(2.0f))
		[
			SNew(SBorder).BorderImage(Plein())
			.BorderBackgroundColor(FSlateColor(SRGB(15, 20, 33, 0.96f)))
			.Padding(FMargin(20, 14))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[ Txt(FString(D.label), 19.0f, AccentColor(D).GetSpecifiedColor(), 120) ]
					+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
					[ Txt(TEXT("* EN LIGNE"), 10.0f, SRGB(140, 217, 242), 90) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 8)
				[ Txt(FString(D.sub), 10.0f, ColTexteFaible, 90) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[ SNew(SSeparator).Thickness(1.0f) ]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[ Contenu ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
				[ SNew(SSeparator).Thickness(1.0f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[ Txt(TEXT("[ ECHAP ]  FERMER"), 9.0f, ColTexteFaible, 60) ]
			]
		]
	];
}

// Badge TRL coloré : opérationnel (vert), en cours (accent), verrouillé (gris).
TSharedRef<SWidget> BadgeTrl(int trl, bool researchable)
{
	FLinearColor c = SRGB(120, 132, 148);            // verrouillé
	if (trl >= fen::tech::TRL_OPERATIONAL) c = ColVert;
	else if (researchable) c = SRGB(255, 189, 87);   // prêt à lancer
	return SNew(SBox).WidthOverride(58.0f)
		[ Txt(FString::Printf(TEXT("TRL %d"), trl), 9.0f, c, 40) ];
}
} // namespace

TSharedRef<SWidget> SSPMenu::BuildPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);

	if (Page == EPage::Racine)
	{
		V->AddSlot().AutoHeight().Padding(26, 22, 26, 0)
		[ BoutonMenu(LOCTEXT("Nouvelle", "NOUVELLE PARTIE"),
		             FOnClicked::CreateSP(this, &SSPMenu::OnNouvellePartie)) ];
		V->AddSlot().AutoHeight().Padding(26, 18, 26, 0)
		[ BoutonMenu(LOCTEXT("Reprendre", "REPRENDRE"),
		             FOnClicked::CreateSP(this, &SSPMenu::OnReprendre)) ];
		V->AddSlot().AutoHeight().Padding(26, 18, 26, 0)
		[ BoutonMenu(LOCTEXT("Quitter", "QUITTER"),
		             FOnClicked::CreateSP(this, &SSPMenu::OnQuitter)) ];
	}
	else if (Page == EPage::Nouvelle)
	{
		V->AddSlot().AutoHeight().Padding(26, 22, 26, 6)
		[
			SNew(STextBlock).Text(LOCTEXT("NomAgence", "NOM DE L'AGENCE"))
			.Font(Mono(10.0f, 150)).ColorAndOpacity(FSlateColor(ColTexteFaible))
		];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 0)
		[
			SAssignNew(NomBox, SEditableTextBox)
			.Font(Mono(13.0f, 40))
			.HintText(LOCTEXT("HintNom", "ARES"))
		];
		V->AddSlot().AutoHeight().Padding(26, 18, 26, 6)
		[
			SNew(STextBlock).Text(LOCTEXT("ModeAide", "MODE D'AIDE"))
			.Font(Mono(10.0f, 150)).ColorAndOpacity(FSlateColor(ColTexteFaible))
		];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 6, 0)
			[ BoutonMenu(LOCTEXT("Normal", "NORMAL  (assistant)"),
			             FOnClicked::CreateLambda([this]() { ModeChoix = 0; SetPage(EPage::Nouvelle); return FReply::Handled(); }), 34.0f) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(6, 0, 0, 0)
			[ BoutonMenu(LOCTEXT("Pro", "PRO  (sans aide)"),
			             FOnClicked::CreateLambda([this]() { ModeChoix = 1; SetPage(EPage::Nouvelle); return FReply::Handled(); }), 34.0f) ]
		];
		V->AddSlot().AutoHeight().Padding(26, 6, 26, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() {
				return ModeChoix == 0
					? LOCTEXT("ModeNormalSel", "mode retenu : NORMAL")
					: LOCTEXT("ModeProSel", "mode retenu : PRO");
			})
			.Font(Mono(9.0f, 80)).ColorAndOpacity(FSlateColor(ColTexteFaible))
		];
		V->AddSlot().AutoHeight().Padding(26, 20, 26, 0)
		[ BoutonMenu(LOCTEXT("Fonder", "FONDER L'AGENCE"),
		             FOnClicked::CreateSP(this, &SSPMenu::OnValiderNouvelle)) ];
		V->AddSlot().AutoHeight().Padding(26, 10, 26, 0)
		[ BoutonMenu(LOCTEXT("Retour", "RETOUR"),
		             FOnClicked::CreateSP(this, &SSPMenu::OnRetour), 34.0f) ];
	}
	else // Reprendre
	{
		if (Session && !Session->saves_scannees) Session->scanner_sauvegardes();
		V->AddSlot().AutoHeight().Padding(26, 22, 26, 8)
		[
			SNew(STextBlock).Text(LOCTEXT("Sauvegardes", "PARTIES ENREGISTREES"))
			.Font(Mono(10.0f, 150)).ColorAndOpacity(FSlateColor(ColTexteFaible))
		];
		if (!Session || Session->saves_listees.empty())
		{
			V->AddSlot().AutoHeight().Padding(26, 4, 26, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("AucuneSave", "(aucune sauvegarde)"))
				.Font(Mono(11.0f, 60)).ColorAndOpacity(FSlateColor(ColTexteFaible))
			];
		}
		else
		{
			for (int32 i = 0; i < static_cast<int32>(Session->saves_listees.size()) && i < 6; ++i)
			{
				const FString Label(Session->saves_listees[static_cast<size_t>(i)].label.c_str());
				V->AddSlot().AutoHeight().Padding(26, 0, 26, 6)
				[
					BoutonMenu(FText::FromString(Label),
					           FOnClicked::CreateLambda([this, i]() {
						           Session->save_sel = i;
						           return OnChargerSelection();
					           }), 32.0f)
				];
			}
		}
		V->AddSlot().AutoHeight().Padding(26, 16, 26, 0)
		[ BoutonMenu(LOCTEXT("Retour2", "RETOUR"),
		             FOnClicked::CreateSP(this, &SSPMenu::OnRetour), 34.0f) ];
	}

	// Pied de panneau : bandeau d'honnêteté + accès aux réglages. La référence
	// n'a que TROIS boutons (ref_menu.png) : les réglages sont donc une ligne
	// discrète, pas un quatrième bouton.
	V->AddSlot().FillHeight(1.0f).VAlign(VAlign_Bottom).Padding(26, 0, 26, 14)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Pied", "monde synchronise sur l'instant reel [GDD 14.1]"))
			.Font(Mono(8.0f, 60))
			.ColorAndOpacity(FSlateColor(SRGB(90, 102, 120)))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(4, 0))
			.OnClicked_Lambda([this]() {
				if (Session) Session->modal = fen::app::Modal::Reglages;
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LienReglages", "REGLAGES"))
				.Font(Mono(8.0f, 100))
				.ColorAndOpacity(FSlateColor(ColTexteFaible))
			]
		]
	];
	return V;
}

FReply SSPMenu::OnNouvellePartie() { SetPage(EPage::Nouvelle); return FReply::Handled(); }

FReply SSPMenu::OnReprendre()
{
	if (Session) Session->saves_scannees = false;   // relire le disque à l'ouverture
	SetPage(EPage::Reprendre);
	return FReply::Handled();
}

FReply SSPMenu::OnQuitter()
{
	if (Session) Session->quitter = true;
	return FReply::Handled();
}

FReply SSPMenu::OnRetour() { SetPage(EPage::Racine); return FReply::Handled(); }

FReply SSPMenu::OnValiderNouvelle()
{
	if (!Session) return FReply::Handled();
	FString Nom = NomBox.IsValid() ? NomBox->GetText().ToString() : FString();
	Nom.TrimStartAndEndInline();
	if (Nom.IsEmpty()) Nom = TEXT("ARES");
	Session->nouvelle_partie(TCHAR_TO_UTF8(*Nom),
	                         ModeChoix == 1 ? fen::app::ModeAide::Pro : fen::app::ModeAide::Normal);
	SetPage(EPage::Racine);
	return FReply::Handled();
}

FReply SSPMenu::OnChargerSelection()
{
	if (!Session) return FReply::Handled();
	const int Sel = Session->save_sel;
	if (Sel >= 0 && Sel < static_cast<int>(Session->saves_listees.size()) &&
	    Session->charger_partie(Session->saves_listees[static_cast<size_t>(Sel)].chemin))
	{
		Session->poste_ouvert = -1;
		Session->scene = fen::app::SceneJeu::Monde;     // on reprend DANS le monde...
		Session->cadrage = fen::app::Cadrage::Bord;     // ...À BORD de Novellus
		SetPage(EPage::Racine);
	}
	else if (Session)
	{
		Session->jeu.erreur = "Sauvegarde illisible.";
	}
	return FReply::Handled();
}

// ═══════════════════════════════════════════════════════════════════════════
// LES MODALES — la faillite et les réglages, par-dessus la scène.
// ═══════════════════════════════════════════════════════════════════════════
void SSPModal::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	ChildSlot
	[
		SNew(SOverlay)
		// voile : la scène reste visible, mais on ne peut plus l'ignorer
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(Plein())
			.BorderBackgroundColor(FSlateColor(SRGB(2, 3, 5, 0.72f)))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SDPIScaler).DPIScale_Lambda([this]() { return Echelle; })
			[
				SAssignNew(Host, SBox).WidthOverride(560.0f)[SNullWidget::NullWidget]
			]
		]
	];
}

void SSPModal::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
                    const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const double Hauteur = AllottedGeometry.GetLocalSize().Y;
	if (Hauteur > 1.0) Echelle = static_cast<float>(Hauteur / 720.0);
	// Le contenu suit l'état du modèle : la faillite s'impose sans qu'aucun
	// clic ne l'ait demandée.
	const int32 Voulue = Session ? static_cast<int32>(Session->modal) : 0;
	if (Voulue != ModalAffichee) { ModalAffichee = Voulue; Rebuild(); }
}

void SSPModal::Rebuild()
{
	if (Host.IsValid()) Host->SetContent(BuildContenu());
}

TSharedRef<SWidget> SSPModal::BuildContenu()
{
	if (!Session || Session->modal == fen::app::Modal::Aucun) return SNullWidget::NullWidget;

	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);

	if (Session->modal == fen::app::Modal::GameOver)
	{
		const fen::app::Agence& A = Session->jeu.agence;
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 26, 0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("Faillite", "FAILLITE"))
			.Font(Mono(30.0f, 240)).ColorAndOpacity(FSlateColor(SRGB(226, 76, 64)))
		];
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 18)
		[
			SNew(STextBlock).Text(LOCTEXT("Dissoute", "l'agence est dissoute  -  fin de partie"))
			.Font(Mono(10.0f, 120)).ColorAndOpacity(FSlateColor(ColTexteFaible))
		];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("Pourquoi", "POURQUOI :"))
			.Font(Mono(10.0f, 120)).ColorAndOpacity(FSlateColor(SRGB(230, 217, 179)))
		];
		// La RAISON vient du modèle, jamais d'un texte générique [GDD 6.8].
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 14)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString(Session->jeu.raison_faillite.c_str())))
			.Font(Mono(11.0f, 30)).ColorAndOpacity(FSlateColor(ColTexte))
			.AutoWrapText(true).WrapTextAt(500.0f)
		];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 18)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Bilan de %s : %d mission(s) reussie(s), %d perdue(s), %.0f mois."),
				*FString(A.nom.c_str()), A.reussites, A.echecs, A.mois)))
			.Font(Mono(10.0f, 30)).ColorAndOpacity(FSlateColor(ColTexteFaible))
			.AutoWrapText(true).WrapTextAt(500.0f)
		];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 10)
		[ BoutonMenu(LOCTEXT("GoNouvelle", "NOUVELLE PARTIE"),
		             FOnClicked::CreateLambda([this]() {
			             Session->nouvelle_apres_faillite();
			             return FReply::Handled();
		             })) ];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 10)
		[ BoutonMenu(LOCTEXT("GoCharger", "CHARGER LA DERNIERE SAUVEGARDE"),
		             FOnClicked::CreateLambda([this]() {
			             const std::string Chemin = Session->chemin_sauvegarde;
			             Session->jeu.reinitialiser();
			             if (Session->charger_partie(Chemin))
			             {
				             Session->modal = fen::app::Modal::Aucun;
				             Session->poste_ouvert = -1;
				             Session->scene = fen::app::SceneJeu::Monde;
				             Session->cadrage = fen::app::Cadrage::Bord;
			             }
			             else
			             {
				             Session->nouvelle_apres_faillite();
			             }
			             return FReply::Handled();
		             })) ];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 26)
		[ BoutonMenu(LOCTEXT("GoQuitter", "QUITTER"),
		             FOnClicked::CreateLambda([this]() {
			             Session->quitter = true;
			             return FReply::Handled();
		             }), 34.0f) ];
	}
	else   // Réglages
	{
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 26, 0, 18)
		[
			SNew(STextBlock).Text(LOCTEXT("TitreReglages", "REGLAGES"))
			.Font(Mono(22.0f, 240)).ColorAndOpacity(FSlateColor(ColTitre))
		];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 6)
		[
			SNew(STextBlock).Text(LOCTEXT("Resolution", "RESOLUTION DE LA FENETRE"))
			.Font(Mono(9.0f, 120)).ColorAndOpacity(FSlateColor(ColTexteFaible))
		];
		static const TCHAR* RES[fen::app::Session::NB_RES] = {
			TEXT("1280 x 720"), TEXT("1360 x 880"), TEXT("1600 x 900"),
			TEXT("1920 x 1080"), TEXT("2560 x 1440")};
		for (int32 i = 0; i < fen::app::Session::NB_RES; ++i)
		{
			const bool bSel = (Session->res_choix == i);
			V->AddSlot().AutoHeight().Padding(26, 0, 26, 4)
			[
				BoutonMenu(FText::FromString(FString::Printf(TEXT("%s  %s"),
				                                             bSel ? TEXT("[x]") : TEXT("[ ]"), RES[i])),
				           FOnClicked::CreateLambda([this, i]() {
					           Session->res_choix = i;
					           Rebuild();
					           return FReply::Handled();
				           }), 30.0f)
			];
		}
		V->AddSlot().AutoHeight().Padding(26, 12, 26, 4)
		[
			BoutonMenu(FText::FromString(FString::Printf(TEXT("%s  PLEIN ECRAN"),
			                                             Session->plein_ecran ? TEXT("[x]") : TEXT("[ ]"))),
			           FOnClicked::CreateLambda([this]() {
				           Session->plein_ecran = !Session->plein_ecran;
				           Rebuild();
				           return FReply::Handled();
			           }), 30.0f)
		];
		V->AddSlot().AutoHeight().Padding(26, 16, 26, 8)
		[ BoutonMenu(LOCTEXT("Appliquer", "APPLIQUER"),
		             FOnClicked::CreateLambda([this]() {
			             Session->appliquer_affichage = true;
			             return FReply::Handled();
		             })) ];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 26)
		[ BoutonMenu(LOCTEXT("Fermer", "RETOUR"),
		             FOnClicked::CreateLambda([this]() {
			             Session->modal = fen::app::Modal::Aucun;
			             return FReply::Handled();
		             }), 34.0f) ];
	}

	return SNew(SBorder)
		.BorderImage(Plein())
		.BorderBackgroundColor(FSlateColor(SRGB(10, 15, 24, 0.96f)))
		.Padding(FMargin(1.0f))
		[ V ];
}

// ═══════════════════════════════════════════════════════════════════════════
// LES POSTES DE TRAVAIL — le cœur C++ remonte au joueur.
// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
// LE BANDEAU DU TEMPS — haut-droite, partout dans le Monde. Voir SPHud.h.
// ═══════════════════════════════════════════════════════════════════════════
namespace {

// Les cinq crans de `fen::game::TimeRate`, dans l'ordre de l'enum. Libellés
// courts : le bandeau est permanent, il doit rester discret.
struct CranTemps { const TCHAR* Court; const TCHAR* Long; };
const CranTemps CRANS[5] = {
	{TEXT("II"),   TEXT("PAUSE")},
	{TEXT("REEL"), TEXT("TEMPS REEL")},
	{TEXT("JOUR"), TEXT("1 JOUR / S")},
	{TEXT("SEM"),  TEXT("1 SEMAINE / S")},
	{TEXT("MOIS"), TEXT("1 MOIS / S")},
};

} // namespace

void SSPTemps::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;

	// LES CRANS. Tous CLIQUABLES, y compris le courant (le poser deux fois est sans
	// effet) : c'est la COULEUR qui dit où l'on en est, pas le grisé. Désactiver le
	// cran courant le rendait plus terne que les autres — l'inverse de ce qu'on veut
	// d'un indicateur d'état permanent.
	TSharedRef<SHorizontalBox> Boutons = SNew(SHorizontalBox);
	Boutons->SetVisibility(EVisibility::SelfHitTestInvisible);
	for (int32 k = 0; k < 5; ++k)
	{
		Boutons->AddSlot().AutoWidth().Padding(0, 0, 3, 0)
		[
			SNew(SButton)
			.ButtonStyle(&StyleBouton())
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			.OnClicked_Lambda([this, k]() {
				// Le modèle borne : le cran au-dessus du plafond [GDD 14.3] se pose
				// AU PLAFOND, jamais à la valeur demandée. Le refus se VOIT (le cran
				// interdit est déjà barré en rouge), il ne se devine pas.
				if (Session) Session->jeu.regler_cadence(static_cast<fen::game::TimeRate>(k));
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Font(Mono(9.0f, 60)).Text(FText::FromString(CRANS[k].Court))
				.ColorAndOpacity_Lambda([this, k]() -> FSlateColor {
					const bool bActif = Session && static_cast<int32>(Session->jeu.cadence) == k;
					// AU-DESSUS DU PLAFOND DE LA MISSION [GDD 14.3] : le cran existe
					// toujours (il redeviendra atteignable), mais il est marqué comme
					// fermé — rouge éteint. Sans ce signal, un clic sans effet passerait
					// pour un bug d'interface.
					if (k > g_render_bridge.cadence_max.load())
						return FSlateColor(SRGB(150, 66, 62));
					// Le cran courant : ambre en pause, vert sinon — la même
					// convention de couleur que la barre de temps de la carte. Les
					// autres restent en retrait.
					if (!bActif) return FSlateColor(SRGB(124, 132, 142));
					return FSlateColor(k == 0 ? SRGB(255, 189, 87) : ColVert);
				})
			]
		];
	}

	ChildSlot
	[
		SNew(SDPIScaler).DPIScale_Lambda([this]() { return Echelle; })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[ SNullWidget::NullWidget ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 14, 18, 0)
			[
				SNew(SBorder)
				.BorderImage(Plein())
				.BorderBackgroundColor(FSlateColor(SRGB(10, 14, 20, 0.72f)))
				.Padding(FMargin(12, 8))
				.Visibility(EVisibility::SelfHitTestInvisible)
				[
					SNew(SVerticalBox)
					.Visibility(EVisibility::SelfHitTestInvisible)
					// date + heure : l'état COURANT du monde [GDD 14]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility(EVisibility::SelfHitTestInvisible)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock).Font(Mono(10.0f, 40))
							.ColorAndOpacity(FSlateColor(ColTexte))
							.Text_Lambda([]() {
								FString D, H; DateHeure(g_render_bridge.epoch_tdb.load(), D, H);
								return FText::FromString(D);
							})
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
						[
							SNew(STextBlock).Font(Mono(10.0f, 40))
							.ColorAndOpacity(FSlateColor(ColTexte))
							.Text_Lambda([]() {
								FString D, H; DateHeure(g_render_bridge.epoch_tdb.load(), D, H);
								return FText::FromString(H);
							})
						]
					]
					// Les crans. Pas de ligne « cadence en clair » : le cran ACTIF est
					// déjà nommé et coloré, et le bandeau doit rester assez court pour
					// ne pas empiéter sur le cadre d'un poste ouvert (constaté en
					// capture). La carte, elle, l'épelle en entier dans sa barre.
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ Boutons ]
					// LE RYTHME IMPOSÉ PAR LA MISSION [GDD 14.3] — « toute manœuvre
					// fine ramène le temps à un rythme lent ». La phase qui l'impose
					// est NOMMÉE : le joueur doit comprendre en un coup d'œil que le
					// monde ralentit parce qu'une ascension est en cours, et non parce
					// que le jeu bloque. Ligne absente le reste du temps (le bandeau
					// est permanent : il ne doit pas grossir pour rien).
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
					[
						SNew(STextBlock).Font(Mono(8.0f, 60))
						.ColorAndOpacity(FSlateColor(SRGB(255, 189, 87)))
						.Visibility_Lambda([]() {
							return g_render_bridge.tempo_contraint.load()
								? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						})
						.Text_Lambda([]() {
							const auto p = static_cast<fen::mission::FlightPhase>(
								g_render_bridge.tempo_phase.load());
							return FText::FromString(FString::Printf(
								TEXT("RYTHME IMPOSE : %s"),
								ANSI_TO_TCHAR(fen::mission::phase_name(p))));
						})
					]
					// les touches : le bandeau se pilote aussi sans la souris (et à
					// bord, où le curseur est capturé, C'EST le seul moyen).
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
					[
						SNew(STextBlock).Font(Mono(8.0f, 30))
						.ColorAndOpacity(FSlateColor(SRGB(102, 115, 133, 0.9f)))
						.Text(FText::FromString(TEXT("[ P ] pause / reprise    [ 1-5 ] cadence")))
					]
				]
			]
		]
	];
}

void SSPTemps::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
                    const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// Échelle sur la géométrie ALLOUÉE, jamais sur la taille du viewport (piège n°19).
	const double H = AllottedGeometry.GetLocalSize().Y;
	if (H > 1.0) Echelle = static_cast<float>(H / 720.0);
}

void SSPPoste::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	ChildSlot
	[
		SNew(SDPIScaler).DPIScale_Lambda([this]() { return Echelle; })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SAssignNew(Host, SBox)[SNullWidget::NullWidget]
			]
		]
	];
}

void SSPPoste::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
                    const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const double H = AllottedGeometry.GetLocalSize().Y;
	if (H > 1.0) Echelle = static_cast<float>(H / 720.0);
	// Le contenu suit le poste ouvert. On reconstruit au CHANGEMENT de poste
	// (l'ouverture, la fermeture), pas à chaque frame — les actions internes
	// appellent Rebuild() elles-mêmes quand elles modifient le modèle.
	const int32 P = Session ? Session->poste_ouvert : -1;
	if (P != PosteAffiche) { PosteAffiche = P; Rebuild(); }
}

void SSPPoste::Rebuild()
{
	if (!Host.IsValid()) return;
	const int32 P = Session ? Session->poste_ouvert : -1;
	Host->SetContent(P >= 0 ? BuildContenu(P) : SNullWidget::NullWidget);
}

TSharedRef<SWidget> SSPPoste::BuildContenu(int Poste)
{
	int n = 0;
	const fen::app::PosteDef* defs = fen::app::postes_def(n);
	if (Poste < 0 || Poste >= n) return SNullWidget::NullWidget;
	const FString id(defs[Poste].id);
	if (id == TEXT("agence"))        return BuildAgence();
	if (id == TEXT("planification")) return BuildPlanification();
	if (id == TEXT("controle"))      return BuildControle();
	if (id == TEXT("analyse"))       return BuildAnalyse();
	if (id == TEXT("operations"))    return BuildOperations();
	if (id == TEXT("conception"))    return BuildConception();
	return BuildInfo(Poste);
}

// --- AGENCE : arbre technologique + file de recherche [GDD 5, 4.3] ----------
TSharedRef<SWidget> SSPPoste::BuildAgence()
{
	int n = 0; const fen::app::PosteDef* defs = fen::app::postes_def(n);
	const fen::app::PosteDef& D = defs[0];
	fen::app::Jeu& J = Session->jeu;
	if (!J.ares.initialisee())
		return FramePoste(D, Txt(TEXT("Couche ARES non initialisee."), 11.0f, ColTexteFaible));

	fen::game::GameState& G = *J.ares.etat;
	const int slots_max = fen::career::max_parallel_research(G.career.rank);
	const int slots_pris = static_cast<int>(G.research.active().size());

	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
	// bandeau : rang, slots de recherche, trésorerie
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[ LigneKV(TEXT("RANG"), FString(fen::career::rank_name(G.career.rank)),
	          SRGB(255, 220, 120)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[ LigneKV(TEXT("RECHERCHES EN COURS"),
	          FString::Printf(TEXT("%d / %d"), slots_pris, slots_max)) ];
	// FINANCES v1.2 [GDD 13.4] : trésorerie et réserve en Md€, + la confiance.
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[ LigneKV(TEXT("TRESORERIE"), FString::Printf(TEXT("%.1f Md EUR"), G.finance.treasury_me / 1000.0),
	          G.finance.treasury_me < 0 ? SRGB(242, 90, 80) : ColVert) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[ LigneKV(TEXT("FONDS DE RESERVE"),
	          FString::Printf(TEXT("%.1f Md EUR  (%.0f %%)"), G.finance.reserve_me / 1000.0,
	                          100.0 * G.finance.reserve_ratio()),
	          G.finance.reserve_level() >= fen::economy::AlertLevel::Crisis ? SRGB(242, 90, 80) : ColVert) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[ LigneKV(TEXT("CONFIANCE ARES"),
	          FString::Printf(TEXT("%.0f / 100  (%s)"), G.career.confidence_ares,
	                          ANSI_TO_TCHAR(fen::economy::access_band_name(
	                              fen::economy::access_band(G.career.confidence_ares)))),
	          G.career.confidence_ares >= 60.0 ? ColVert
	          : G.career.confidence_ares >= 40.0 ? SRGB(255, 189, 87) : SRGB(242, 90, 80)) ];

	// recherches actives, avec progression
	for (const auto& p : G.research.active())
	{
		const fen::tech::TechNode* nn = G.tree.find(p.node_id);
		Col->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
		[ LigneKV(FString(nn ? nn->name.c_str() : p.node_id.c_str()),
		          FString::Printf(TEXT("%.0f %%"), 100.0 * p.progress()),
		          SRGB(140, 179, 255)) ];
	}

	// ═══ LE SYSTÈME TEMPOREL DE L'AGENCE [GDD 14.2] ═══
	// C'est ICI que le temps se pilote, et nulle part ailleurs : la barre de temps
	// de la carte est un indicateur [GDD 14]. Accélérer n'est PAS neutre — les
	// recettes garanties hors activité (≈ 35 Md€/an) sont inférieures aux coûts
	// fixes (≈ 44 Md€/an) : le temps qui passe sans programme érode la trésorerie
	// puis la réserve [GDD 13.2]. On le DIT au joueur, sous les boutons.
	{
		using fen::game::TimeRate;
		const TimeRate courante = Session->jeu.cadence;
		static const TCHAR* const Libelles[5] = {
			TEXT("PAUSE"), TEXT("REEL"), TEXT("JOUR/S"), TEXT("SEMAINE/S"), TEXT("MOIS/S")
		};
		// LE PLAFOND DE LA MISSION [GDD 14.3] : les crans au-dessus sont fermés
		// tant qu'une manœuvre fine est en cours. Le poste étant le réglage
		// « institutionnel », c'est ici qu'on doit lire le POURQUOI en toutes
		// lettres — un bouton éteint sans motif serait une panne, pas une règle.
		const fen::mission::TempoLimit Tempo = Session->jeu.plafond_temps();
		TSharedRef<SHorizontalBox> Cadences = SNew(SHorizontalBox);
		Cadences->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[ Txt(TEXT("CADENCE DU TEMPS"), 10.0f, ColTexteFaible, 90) ];
		for (int32 k = 0; k < 5; ++k)
		{
			const TimeRate r = static_cast<TimeRate>(k);
			const bool bActive = (r == courante);
			const bool bFerme = k > static_cast<int32>(Tempo.max_rate);
			Cadences->AddSlot().AutoWidth().Padding(0, 0, 3, 0)
			[
				// Le cran courant est DÉSACTIVÉ : il n'y a rien à y cliquer, et le
				// griser est ce qui montre où l'on en est. Un cran au-dessus du
				// plafond l'est aussi, pour la raison inverse.
				BoutonAction(FText::FromString(FString(Libelles[k])),
				             FOnClicked::CreateLambda([this, r]() {
					             Session->jeu.regler_cadence(r);
					             Rebuild();
					             return FReply::Handled();
				             }), !bActive && !bFerme)
			];
		}
		Col->AddSlot().AutoHeight().Padding(0, 8, 0, 2)[ Cadences ];
		if (Tempo.constrained)
		{
			// DEUX LIGNES COURTES, pas une longue : le cadre du poste CLIPPE le
			// texte au lieu de le replier (constaté en capture) — une explication
			// tronquée en plein milieu vaut moins que pas d'explication du tout.
			Col->AddSlot().AutoHeight().Padding(0, 0, 0, 1)
			[ Txt(FString::Printf(
			          TEXT("%s en cours (%s) : rythme limite a %s [GDD 14.3]."),
			          ANSI_TO_TCHAR(fen::mission::phase_name(Tempo.phase)),
			          ANSI_TO_TCHAR(Tempo.mission_id.c_str()),
			          Libelles[static_cast<int32>(Tempo.max_rate)]),
			      9.0f, SRGB(255, 189, 87), 40) ];
			Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
			[ Txt(TEXT("Une manoeuvre fine se conduit, elle ne se survole pas."),
			      9.0f, SRGB(255, 189, 87), 40) ];
		}
		// L'AVERTISSEMENT CHIFFRÉ, tiré du modèle et non d'un texte décoratif.
		const double solde_an = G.finance.annual_idle_balance_me();
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[ Txt(FString::Printf(
		          TEXT("Sans programme ni service, le temps coute %.1f Md EUR/an (recettes garanties < couts fixes)."),
		          -solde_an / 1000.0),
		      9.0f, SRGB(255, 189, 87), 40) ];
	}

	Col->AddSlot().AutoHeight().Padding(0, 4, 0, 6)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[ Txt(TEXT("ARBRE TECHNOLOGIQUE — 6 branches"), 10.0f, ColTexteFaible, 90) ]
		+ SHorizontalBox::Slot().AutoWidth()
		[ BoutonAction(LOCTEXT("PasserMois", "PASSER 1 MOIS"),
		               FOnClicked::CreateLambda([this]() {
			               Session->jeu.passer_mois();
			               Rebuild();
			               return FReply::Handled();
		               })) ]
	];

	// l'arbre, branche par branche, dans une zone déroulante
	TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
	for (int b = 0; b < 6; ++b)
	{
		const auto branch = static_cast<fen::tech::Branch>(b);
		Scroll->AddSlot().Padding(0, 6, 0, 2)
		[ Txt(FString(fen::tech::branch_name(branch)).ToUpper(), 10.0f,
		      SRGB(112, 179, 255), 90) ];

		for (const auto& node : G.tree.all())
		{
			if (node.branch != branch) continue;
			const bool researchable = G.tree.researchable(node.id);
			const bool slot_libre = slots_pris < slots_max;
			const bool payable = (G.finance.treasury_me + G.finance.reserve_me) >= node.research_cost_musd;
			const bool lancable = researchable && slot_libre && payable;
			// bouton présent seulement si la techno est réellement à chercher
			TSharedRef<SWidget> action = SNullWidget::NullWidget;
			if (researchable)
			{
				const std::string nid = node.id;
				const double cost = node.research_cost_musd;
				action = BoutonAction(
					FText::FromString(FString::Printf(TEXT("LANCER  %.0f M EUR . %.0f j"),
					                                  cost, node.research_days)),
					FOnClicked::CreateLambda([this, nid, cost]() {
						fen::game::GameState& GS = *Session->jeu.ares.etat;
						// L'ÉCONOMIE v1.2 garde le dernier mot : on n'engage que si
						// les fonds passent [GDD 5.3, 13.4].
						if (GS.finance.engage(cost))
						{
							if (!GS.research.start(GS.tree, nid, GS.career.rank))
								GS.finance.credit(cost);   // remboursement si slot plein
						}
						Rebuild();
						return FReply::Handled();
					}),
					lancable);
			}
			Scroll->AddSlot().Padding(6, 1, 0, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BadgeTrl(node.trl, researchable) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(6, 0)
				[ Txt(FString(node.name.c_str()) + (node.transverse ? TEXT("  [transverse]") : TEXT("")),
				      10.0f, node.operational() ? ColTexte : ColTexteFaible, 30) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ action ]
			];
		}
	}
	Col->AddSlot().FillHeight(1.0f)[ Scroll ];
	// note d'honnêteté : les coûts sont provisoires [GDD 20]
	Col->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[ Txt(TEXT("couts et durees provisoires [GDD 20]"), 8.0f, SRGB(90, 102, 120), 40) ];

	return FramePoste(D, Col);
}

// --- PLANIFICATION : catalogue de missions + mails [GDD 10.1, 10.2] ---------
TSharedRef<SWidget> SSPPoste::BuildPlanification()
{
	int n = 0; const fen::app::PosteDef* defs = fen::app::postes_def(n);
	const fen::app::PosteDef& D = defs[5];
	fen::app::Jeu& J = Session->jeu;
	if (!J.ares.initialisee())
		return FramePoste(D, Txt(TEXT("Couche ARES non initialisee."), 11.0f, ColTexteFaible));
	fen::game::GameState& G = *J.ares.etat;

	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
	[ LigneKV(TEXT("COURRIER ARES"),
	          FString::Printf(TEXT("%d non lu(s)"), G.inbox.unread_count())) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
	[ LigneKV(TEXT("CONTRATS NOTIFIES"),
	          FString::Printf(TEXT("%d"), (int)G.inbox.pending_contracts().size())) ];

	// Motif du dernier refus d'acceptation (filtre de confiance, gel...) [GDD 13.4].
	if (!Session->dernier_refus_contrat.empty())
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[ Txt(FString(TEXT("REFUSE : ")) + FString(Session->dernier_refus_contrat.c_str()),
		      10.0f, SRGB(242, 90, 80), 30) ];

	// MISSIONS EN COURS : ce qu'on a déjà accepté [GDD 4.1].
	if (!G.missions.empty())
	{
		Col->AddSlot().AutoHeight().Padding(0, 2, 0, 2)
		[ Txt(TEXT("MISSIONS EN COURS"), 10.0f, ColTexteFaible, 90) ];
		for (const auto& m : G.missions)
			Col->AddSlot().AutoHeight().Padding(6, 0, 0, 1)
			[ LigneKV(FString(m.contract.title.c_str()),
			          FString(fen::mission::state_name(m.state)), SRGB(140, 179, 255)) ];
	}

	// LE VERROU DE 10.2 : une mission ne s'affiche que si elle a été NOTIFIÉE
	// par mail. On parcourt donc la boîte, pas le catalogue.
	Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[ Txt(TEXT("COURRIER — CONTRATS PROPOSES"), 10.0f, ColTexteFaible, 90) ];
	TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
	const auto pending = G.inbox.pending_contracts();
	if (pending.empty())
	{
		Scroll->AddSlot().Padding(6, 4)
		[ Txt(TEXT("Aucun contrat en attente. ARES notifie une mission des que ses"), 10.0f, ColTexteFaible) ];
		Scroll->AddSlot().Padding(6, 0)
		[ Txt(TEXT("quatre verrous (rang, TRL, budget, infra) sont leves [GDD 4.2]."), 10.0f, ColTexteFaible) ];
	}
	for (const auto* m : pending)
	{
		const std::string cid = m->contract_id;
		Scroll->AddSlot().Padding(6, 6, 0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[ Txt(FString(m->subject.c_str()), 11.0f, SRGB(255, 220, 120), 40) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ BoutonAction(LOCTEXT("Accepter", "ACCEPTER"),
			               FOnClicked::CreateLambda([this, cid]() {
				               Session->accepter_contrat(cid);
				               Rebuild();
				               return FReply::Handled();
			               })) ]
		];
		Scroll->AddSlot().Padding(10, 0, 6, 4)
		[
			SNew(STextBlock).Text(FText::FromString(FString(m->body.c_str())))
			.Font(Mono(9.0f, 20)).ColorAndOpacity(FSlateColor(ColTexteFaible))
			.AutoWrapText(true).WrapTextAt(620.0f)
		];
	}

	Col->AddSlot().FillHeight(1.0f)[ Scroll ];
	// aperçu du catalogue verrouillé (visible mais grisé — 4.2)
	int verrouilles = 0;
	for (const auto& e : G.catalog.entries())
		if (!G.inbox.contract_notified(e.contract.id)) ++verrouilles;
	Col->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[ Txt(FString::Printf(TEXT("%d mission(s) planifiee(s), verrouillee(s) [GDD 4.2]"),
	                      verrouilles), 8.0f, SRGB(90, 102, 120), 40) ];
	return FramePoste(D, Col);
}

// --- ANALYSE : base de fiabilité [GDD 12.3] ---------------------------------
TSharedRef<SWidget> SSPPoste::BuildAnalyse()
{
	int n = 0; const fen::app::PosteDef* defs = fen::app::postes_def(n);
	const fen::app::PosteDef& D = defs[1];
	fen::app::Jeu& J = Session->jeu;
	const fen::app::Agence& A = J.agence;
	const int vols = A.reussites + A.echecs;

	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("VOLS BOUCLES"), FString::Printf(TEXT("%d"), vols)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("REUSSITES"), FString::Printf(TEXT("%d"), A.reussites), ColVert) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("ECHECS"), FString::Printf(TEXT("%d"), A.echecs), SRGB(242, 90, 80)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[ LigneKV(TEXT("DONNEES"), FString::Printf(TEXT("%.1f Gbit"), J.donnees_gbit)) ];

	Col->AddSlot().AutoHeight().Padding(0, 4, 0, 4)
	[ Txt(TEXT("BASE DE FIABILITE — fiches tracees [GDD 12.3]"), 10.0f, ColTexteFaible, 90) ];

	TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
	if (J.ares.initialisee())
	{
		for (const auto& r : J.ares.etat->reliability_db.all())
		{
			const TCHAR* conf = (r.confidence == fen::reliability::Confidence::A) ? TEXT("A")
			                  : (r.confidence == fen::reliability::Confidence::B) ? TEXT("B")
			                  : (r.confidence == fen::reliability::Confidence::C) ? TEXT("C") : TEXT("D");
			Scroll->AddSlot().Padding(6, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[ Txt(FString(r.name.c_str()), 10.0f, ColTexte, 30) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[ Txt(FString::Printf(TEXT("%.4f"), r.nominal), 10.0f, SRGB(140, 179, 255), 30) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ Txt(FString::Printf(TEXT("[%s]"), conf), 10.0f,
				      r.confidence <= fen::reliability::Confidence::B ? ColVert : SRGB(255, 189, 87), 30) ]
			];
		}
	}
	Col->AddSlot().FillHeight(1.0f)[ Scroll ];
	Col->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[ Txt(TEXT("valeur nominale ; jamais utilisee brute [GDD 12.3.3]"), 8.0f, SRGB(90, 102, 120), 40) ];
	return FramePoste(D, Col);
}

// --- OPERATIONS : flotte en service [GDD 8.3, 10.1] -------------------------
TSharedRef<SWidget> SSPPoste::BuildOperations()
{
	int n = 0; const fen::app::PosteDef* defs = fen::app::postes_def(n);
	const fen::app::PosteDef& D = defs[2];
	fen::app::Jeu& J = Session->jeu;

	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("RELAIS GEO"), FString::Printf(TEXT("%d"), J.relais_geo)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("ORBITEURS MARS"), FString::Printf(TEXT("%d"), J.orbiteurs_mars)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("SONDES LOINTAINES"), FString::Printf(TEXT("%d"), J.sondes_lointaines)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[ LigneKV(TEXT("REVENU FLOTTE"), FString::Printf(TEXT("%.1f Gbit/mo"), J.revenu_mensuel_gbit())) ];
	Col->AddSlot().AutoHeight().Padding(0, 4, 0, 4)
	[ Txt(TEXT("ENGINS EN SERVICE — ephemeride propre [GDD 8.3]"), 10.0f, ColTexteFaible, 90) ];

	TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
	if (J.flotte.empty())
		Scroll->AddSlot().Padding(6, 4)[ Txt(TEXT("(aucun engin en service)"), 10.0f, ColTexteFaible) ];
	for (const auto& e : J.flotte)
	{
		const char* corps = e.type == fen::app::EnginFlotte::RelaisGeo ? "Terre"
		                  : e.type == fen::app::EnginFlotte::OrbiteurMars ? "Mars" : "Soleil";
		Scroll->AddSlot().Padding(6, 1)
		[ LigneKV(FString(e.nom.c_str()), FString::Printf(TEXT("ref. %s"), ANSI_TO_TCHAR(corps)),
		          SRGB(140, 179, 255)) ];
	}
	Col->AddSlot().FillHeight(1.0f)[ Scroll ];
	return FramePoste(D, Col);
}

// --- CONCEPTION : atelier d'assemblage [GDD 12.2] ---------------------------
// « Tableau de masse/delta-v recalculé automatiquement selon Tsiolkovsky à
// chaque modification. » Le joueur CHOISIT ses pièces et le partage du Δv ; le
// modèle (app/vehicle_design.hpp) recalcule les masses. Rien n'est optimisé à
// sa place [anti-feature 1.5].
TSharedRef<SWidget> SSPPoste::BuildConception()
{
	int n = 0; const fen::app::PosteDef* defs = fen::app::postes_def(n);
	const fen::app::PosteDef& D = defs[4];
	fen::app::VehicleDesign& VD = Session->vehicule_design;
	const bool pro = (Session->jeu.agence.mode == fen::app::ModeAide::Pro);
	const fen::app::DesignSummary S = fen::app::evaluate_design(VD);
	const auto& engs = fen::vehicle::engine_catalog();

	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);

	// --- BILAN (recalculé à chaque frame) — le tableau de 12.2 ---------------
	// En mode Pro, aucune aide au calcul : on masque le bilan automatique et on
	// ne laisse que la validation réaliste (convergence, décollage) [GDD 2.2, 12.2].
	if (!pro)
	{
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
		[ LigneKV(TEXT("DELTA-V TOTAL"), FString::Printf(TEXT("%.0f m/s"), S.total_dv_ms),
		          SRGB(140, 179, 255)) ];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
		[ LigneKV(TEXT("MASSE AU DECOLLAGE"), FString::Printf(TEXT("%.0f kg"), S.liftoff_mass_kg)) ];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
		[ LigneKV(TEXT("CHARGE UTILE"), FString::Printf(TEXT("%.0f kg  (%.1f %%)"),
		          S.payload_kg, 100.0 * S.payload_fraction)) ];
	}
	else
	{
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
		[ Txt(TEXT("MODE PRO : aucun calcul automatique. Derivez la masse a la main."),
		      10.0f, SRGB(255, 189, 87), 40) ];
	}
	// La VALIDATION réaliste subsiste dans les deux modes.
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[ Txt(S.warning.empty() ? FString(TEXT("VALIDE : converge, decollage possible"))
	                        : FString(S.warning.c_str()),
	      10.0f, S.warning.empty() ? ColVert : SRGB(242, 90, 80), 40) ];

	// --- charge utile : capsule + charge nue ---------------------------------
	{
		const auto& caps = fen::vehicle::capsule_catalog();
		const FString capname = (VD.capsule >= 0 && VD.capsule < (int)caps.size())
			? FString(caps[VD.capsule].name) : FString(TEXT("(charge nue)"));
		Col->AddSlot().AutoHeight().Padding(0, 2, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ Txt(TEXT("CAPSULE"), 10.0f, ColTexte) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
			[ BoutonMini(TEXT("<"), FOnClicked::CreateLambda([this, &VD]() {
				const int nc = (int)fen::vehicle::capsule_catalog().size();
				VD.capsule = (VD.capsule <= -1) ? nc - 1 : VD.capsule - 1;
				Rebuild(); return FReply::Handled(); })) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).HAlign(HAlign_Center)
			[ Txt(capname, 10.0f, SRGB(140, 179, 255), 0) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ BoutonMini(TEXT(">"), FOnClicked::CreateLambda([this, &VD]() {
				const int nc = (int)fen::vehicle::capsule_catalog().size();
				VD.capsule = (VD.capsule + 1 >= nc) ? -1 : VD.capsule + 1;
				Rebuild(); return FReply::Handled(); })) ]
		];
	}

	// --- LA PILE D'ÉTAGES : du bas (index 0) vers le haut --------------------
	Col->AddSlot().AutoHeight().Padding(0, 2, 0, 2)
	[ Txt(TEXT("ETAGES (bas -> haut) — moteur, Delta-v confie, ergols"), 10.0f, ColTexteFaible, 90) ];

	TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
	for (int k = 0; k < (int)VD.stages.size(); ++k)
	{
		const int ei = fen::app::detail::clampi(VD.stages[k].engine, 0, (int)engs.size() - 1);
		const FString mname(engs[ei].name);
		const FString ergols = (k < (int)S.stages.size())
			? FString::Printf(TEXT("%.0f kg"), S.stages[k].propellant_kg) : FString(TEXT("-"));
		Scroll->AddSlot().Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ Txt(FString::Printf(TEXT("E%d"), k + 1), 10.0f, SRGB(255, 189, 87), 0) ]
			// choix du moteur (< nom >)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
			[ BoutonMini(TEXT("<"), FOnClicked::CreateLambda([this, &VD, k]() {
				const int ne = (int)fen::vehicle::engine_catalog().size();
				VD.stages[k].engine = (VD.stages[k].engine + ne - 1) % ne;
				Rebuild(); return FReply::Handled(); })) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[ SNew(SBox).WidthOverride(190.0f)[ Txt(mname, 10.0f, ColTexte, 0) ] ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ BoutonMini(TEXT(">"), FOnClicked::CreateLambda([this, &VD, k]() {
				const int ne = (int)fen::vehicle::engine_catalog().size();
				VD.stages[k].engine = (VD.stages[k].engine + 1) % ne;
				Rebuild(); return FReply::Handled(); })) ]
			// Δv confié (- valeur +)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 2, 0)
			[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this, &VD, k]() {
				VD.stages[k].dv_target_ms = FMath::Max(0.0, VD.stages[k].dv_target_ms - 500.0);
				Rebuild(); return FReply::Handled(); })) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(SBox).WidthOverride(66.0f)[ Txt(FString::Printf(TEXT("%.0f"),
			      VD.stages[k].dv_target_ms), 10.0f, SRGB(140, 179, 255), 0) ] ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this, &VD, k]() {
				VD.stages[k].dv_target_ms += 500.0;
				Rebuild(); return FReply::Handled(); })) ]
			// ergols dimensionnés (Normal seulement)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
			[ SNew(SBox).WidthOverride(80.0f)[ Txt(pro ? FString(TEXT("")) : ergols, 9.0f, ColTexteFaible, 0) ] ]
			// retirer l'étage
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ BoutonMini(TEXT("x"), FOnClicked::CreateLambda([this, &VD, k]() {
				if (VD.stages.size() > 1) VD.stages.erase(VD.stages.begin() + k);
				Rebuild(); return FReply::Handled(); }), 22.0f) ]
		];
	}
	Col->AddSlot().FillHeight(1.0f)[ Scroll ];

	Col->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[ BoutonAction(LOCTEXT("AjouterEtage", "+ ETAGE"),
		               FOnClicked::CreateLambda([this, &VD]() {
			               VD.stages.push_back(fen::app::StagePick{});
			               Rebuild(); return FReply::Handled(); })) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
		[ Txt(TEXT("Tsiolkovsky : la physique tranche la masse [GDD 12.2]"), 8.0f, SRGB(90, 102, 120), 40) ]
	];
	return FramePoste(D, Col);
}

// --- CONTROLE : la boucle de mission [GDD 4.1] ------------------------------
// DESTINY . CONTROLE DE VOL. On y PILOTE une mission de bout en bout : concevoir
// (choix de programme + bilan viabilité), franchir chaque gate, engager le feu
// vert (commit financier), exécuter le vol, lire le débrief. La logique vit dans
// `app/session.hpp` (sous oracle) ; ici, la vue et les commandes.
TSharedRef<SWidget> SSPPoste::BuildControle()
{
	int n = 0; const fen::app::PosteDef* defs = fen::app::postes_def(n);
	const fen::app::PosteDef& D = defs[3];
	fen::app::Session& Se = *Session;

	// Cibler une mission à piloter si la précédente est close (jamais en plein
	// débrief : on ne veut pas perdre l'issue affichée).
	fen::mission::Mission* mc = Se.mission_courante();
	if (!mc || mc->state == fen::mission::MissionState::Completed ||
	    mc->state == fen::mission::MissionState::Failed ||
	    mc->state == fen::mission::MissionState::Aborted)
	{
		Se.piloter_premiere_mission();
		mc = Se.mission_courante();
	}

	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
	if (!mc)
	{
		Col->AddSlot().AutoHeight().Padding(0, 4)
		[ Txt(TEXT("Aucune mission a piloter. Acceptez un contrat au poste"), 10.0f, ColTexteFaible) ];
		Col->AddSlot().AutoHeight()
		[ Txt(TEXT("PLANIFICATION, puis revenez ici la conduire [GDD 4.1]."), 10.0f, ColTexteFaible) ];
		return FramePoste(D, Col);
	}

	using St = fen::mission::MissionState;
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
	[ LigneKV(TEXT("MISSION"), FString(mc->contract.title.c_str()), SRGB(255, 220, 120)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
	[ LigneKV(TEXT("PHASE"), FString(fen::mission::state_name(mc->state)), SRGB(140, 179, 255)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
	[ LigneKV(TEXT("BUDGET CONTRAT"), FString::Printf(TEXT("%.0f M EUR"), mc->contract.terms.budget_musd)) ];

	// --- LE PROGRAMME : ce que le joueur choisit (couche Program.hpp) ---------
	Se.evaluer_plan();
	fen::mission::MissionPlan& P = Se.mission_plan;
	const auto& progEngines = fen::mission::engines();
	const int ei = FMath::Clamp(P.program.engine_index, 0, (int)progEngines.size() - 1);

	Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[ Txt(TEXT("PROGRAMME — moteur, etages, qualification"), 10.0f, ColTexteFaible, 90) ];

	// moteur
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Txt(TEXT("MOTEUR"), 10.0f, ColTexte) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
		[ BoutonMini(TEXT("<"), FOnClicked::CreateLambda([this, &P]() {
			const int ne = (int)fen::mission::engines().size();
			P.program.engine_index = (P.program.engine_index + ne - 1) % ne;
			Rebuild(); return FReply::Handled(); })) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[ Txt(FString(progEngines[ei].eng.id.c_str()), 10.0f, SRGB(140, 179, 255), 0) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ BoutonMini(TEXT(">"), FOnClicked::CreateLambda([this, &P]() {
			const int ne = (int)fen::mission::engines().size();
			P.program.engine_index = (P.program.engine_index + 1) % ne;
			Rebuild(); return FReply::Handled(); })) ]
	];
	// nombre d'étages
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Txt(TEXT("ETAGES"), 10.0f, ColTexte) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
		[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this, &P]() {
			P.n_stages = FMath::Max(1, P.n_stages - 1); Rebuild(); return FReply::Handled(); })) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ SNew(SBox).WidthOverride(40.0f)[ Txt(FString::Printf(TEXT("%d"), P.n_stages), 10.0f, SRGB(140, 179, 255), 0) ] ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this, &P]() {
			P.n_stages = FMath::Min(4, P.n_stages + 1); Rebuild(); return FReply::Handled(); })) ]
		// revue
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
		[ BoutonAction(FText::FromString(FString::Printf(TEXT("%s REVUE INDEP."),
		               P.program.review ? TEXT("[x]") : TEXT("[ ]"))),
		               FOnClicked::CreateLambda([this, &P]() {
			               P.program.review = !P.program.review; Rebuild(); return FReply::Handled(); })) ]
	];
	// heures d'essai
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Txt(TEXT("ESSAIS A FEU"), 10.0f, ColTexte) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
		[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this, &P]() {
			P.program.test_hours = FMath::Max(0.0, P.program.test_hours - 50.0); Rebuild(); return FReply::Handled(); })) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ SNew(SBox).WidthOverride(60.0f)[ Txt(FString::Printf(TEXT("%.0f h"), P.program.test_hours), 10.0f, SRGB(140, 179, 255), 0) ] ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this, &P]() {
			P.program.test_hours += 50.0; Rebuild(); return FReply::Handled(); })) ]
		// marge de dv
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
		[ Txt(FString::Printf(TEXT("marge %.0f m/s"), P.program.dv_margin), 9.0f, ColTexteFaible, 30) ]
	];

	// --- LE BILAN (assess) : le tableau de viabilité -------------------------
	const fen::mission::Assessment& A = P.assessment;
	Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[ Txt(TEXT("BILAN DE VIABILITE"), 10.0f, ColTexteFaible, 90) ];
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[ LigneKV(TEXT("MASSE AU DECOLLAGE"), FString::Printf(TEXT("%.0f kg"), A.m0_kg),
	          A.fits_mass ? ColVert : SRGB(242, 90, 80)) ];
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[ LigneKV(TEXT("COUT PROGRAMME"), FString::Printf(TEXT("%.0f / %.0f M EUR"), A.cost_total, mc->contract.terms.budget_musd),
	          A.fits_budget ? ColVert : SRGB(242, 90, 80)) ];
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[ LigneKV(TEXT("CALENDRIER"), FString::Printf(TEXT("%.0f / %.0f mois"), A.schedule_months, mc->contract.terms.deadline_months),
	          A.fits_schedule ? ColVert : SRGB(242, 90, 80)) ];
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[ LigneKV(TEXT("P(SUCCES)"), FString::Printf(TEXT("%.1f %% / %.0f %% exige"), 100.0 * A.p_success, 100.0 * mc->contract.terms.min_success_prob),
	          A.fits_risk ? ColVert : SRGB(242, 90, 80)) ];
	Col->AddSlot().AutoHeight().Padding(6, 3)
	[ Txt(A.ok ? FString(TEXT("PROGRAMME VIABLE")) : (FString(TEXT("VERROU : ")) + FString(A.why.c_str())),
	      10.0f, A.ok ? ColVert : SRGB(242, 90, 80), 40) ];

	// --- L'ISSUE (au débrief) ------------------------------------------------
	if (mc->state == St::Debrief && Se.mission_outcome_pret)
	{
		Col->AddSlot().AutoHeight().Padding(0, 4, 0, 1)
		[ Txt(Se.mission_outcome.success ? TEXT("VOL REUSSI") : TEXT("VOL ECHOUE"),
		      12.0f, Se.mission_outcome.success ? ColVert : SRGB(242, 90, 80), 90) ];
		Col->AddSlot().AutoHeight().Padding(6, 0)
		[ Txt(FString(Se.mission_outcome.cause.c_str()), 10.0f, ColTexteFaible, 30) ];
	}

	Col->AddSlot().FillHeight(1.0f)[ SNew(SSpacer) ];

	// --- LE BOUTON D'AVANCE : franchit un gate, ou dit pourquoi il refuse -----
	{
		const TCHAR* lib =
			mc->state == St::Prerequisites ? TEXT("PASSER EN CONCEPTION")
			: mc->state == St::Design ? TEXT("VALIDER LA CONCEPTION")
			: mc->state == St::WindowSearch ? TEXT("RETENIR LA FENETRE")
			: mc->state == St::Qualification ? TEXT("FEU VERT : ENGAGER (paye le programme)")
			: mc->state == St::Launched ? TEXT("EXECUTER LE VOL")
			: mc->state == St::Debrief ? TEXT("CLORE LE DEBRIEF")
			: TEXT("MISSION CLOSE");
		const bool closable = mc->state != St::Completed && mc->state != St::Failed &&
		                      mc->state != St::Aborted;
		// motif de refus (gate) affiché sous le bouton
		Col->AddSlot().AutoHeight().Padding(0, 2, 0, 4)
		[ BoutonAction(FText::FromString(lib),
		               FOnClicked::CreateLambda([this]() {
			               Session->avancer_mission();
			               Rebuild(); return FReply::Handled();
		               }), closable) ];
	}
	return FramePoste(D, Col);
}

// --- Postes en LECTURE SEULE (COUPOLE, NOVELLUS) -----------------------------
TSharedRef<SWidget> SSPPoste::BuildInfo(int Poste)
{
	int n = 0; const fen::app::PosteDef* defs = fen::app::postes_def(n);
	const fen::app::PosteDef& D = defs[Poste];
	fen::app::Jeu& J = Session->jeu;
	const fen::app::Agence& A = J.agence;
	const FString id(D.id);

	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
	if (id == TEXT("vigie"))
	{
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("MODULE"), TEXT("VIGIE")) ];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("STATION"), TEXT("NOVELLUS")) ];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("PROGRAMME"), FString(A.nom.c_str())) ];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("ORDINATEUR"), TEXT("EN LIGNE"), ColVert) ];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("CARTE"), TEXT("SYSTEME SOLAIRE [M]")) ];
		if (J.ares.initialisee())
		{
			const auto fx = fen::station::effects(J.ares.etat->station);
			Col->AddSlot().AutoHeight().Padding(0, 6, 0, 3)
			[ LigneKV(TEXT("STATION OPERATIONNELLE"), fx.operational ? TEXT("OUI") : TEXT("NON"),
			          fx.operational ? ColVert : SRGB(242, 90, 80)) ];
			Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
			[ LigneKV(TEXT("PALIER NOVELLUS"), FString::Printf(TEXT("%d / 4"), J.ares.etat->station.tier())) ];
			Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
			[ LigneKV(TEXT("MARGE ENERGIE"), FString::Printf(TEXT("%.0f kW"), fx.power_margin_kw),
			          fx.power_sufficient ? ColVert : SRGB(242, 90, 80)) ];
		}
	}
	else if (id == TEXT("observation"))
	{
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("ORBITE"), TEXT("LEO 418 km")) ];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ LigneKV(TEXT("ECHANTILLONS"), FString::Printf(TEXT("%.1f kg"), J.echantillons_kg)) ];
		Col->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[ Txt(TEXT("La Coupole donne sur la Terre et le systeme solaire."), 10.0f, ColTexteFaible) ];
	}
	else
	{
		Col->AddSlot().AutoHeight()[ Txt(TEXT("Poste en cours de portage."), 11.0f, ColTexteFaible) ];
	}
	return FramePoste(D, Col);
}

// ═══════════════════════════════════════════════════════════════════════════
// LA RACINE
// ═══════════════════════════════════════════════════════════════════════════
void SSPHud::Construct(const FArguments& InArgs)
{
	Session = InArgs._Session;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(WorldHud, SSPWorldHud).Session(Session)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(Menu, SSPMenu).Session(Session)
		]
		// Le poste ouvert : au-dessus du monde, SOUS la modale (une faillite doit
		// pouvoir s'imposer même un poste ouvert).
		+ SOverlay::Slot()
		[
			SAssignNew(Poste, SSPPoste).Session(Session)
		]
		// LE BANDEAU DU TEMPS : au-dessus du poste (il ne doit JAMAIS être masqué —
		// le temps coule et se paie pendant qu'on travaille dans un poste), sous la
		// modale (une décision suspend tout).
		+ SOverlay::Slot()
		[
			SAssignNew(Temps, SSPTemps).Session(Session)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(Modale, SSPModal).Session(Session)
		]
	];
	Menu->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SSPHud::MenuVisibility));
	WorldHud->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SSPHud::WorldVisibility));
	Modale->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SSPHud::ModalVisibility));
	Poste->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SSPHud::PosteVisibility));
	Temps->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SSPHud::TempsVisibility));
}

EVisibility SSPHud::PosteVisibility() const
{
	return (Session && Session->scene == fen::app::SceneJeu::Monde &&
	        Session->cadrage == fen::app::Cadrage::Bord &&
	        Session->poste_ouvert >= 0 && Session->modal == fen::app::Modal::Aucun)
	           ? EVisibility::Visible : EVisibility::Collapsed;
}

// LE BANDEAU DU TEMPS : partout dans le Monde — les deux cadrages, poste ouvert
// compris. Masqué sous une modale (elle porte une décision, le monde attend) et au
// menu (pas de partie, donc pas d'horloge).
// `SelfHitTestInvisible` et NON `Visible` : le bandeau laisse passer la souris,
// seuls ses boutons la prennent (piège n°6).
EVisibility SSPHud::TempsVisibility() const
{
	return (Session && Session->scene == fen::app::SceneJeu::Monde &&
	        Session->modal == fen::app::Modal::Aucun)
	           ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

EVisibility SSPHud::ModalVisibility() const
{
	return (Session && Session->modal != fen::app::Modal::Aucun) ? EVisibility::Visible
	                                                             : EVisibility::Collapsed;
}

EVisibility SSPHud::MenuVisibility() const
{
	return (Session && Session->scene == fen::app::SceneJeu::Titre) ? EVisibility::Visible
	                                                                : EVisibility::Collapsed;
}

EVisibility SSPHud::WorldVisibility() const
{
	return (Session && Session->scene != fen::app::SceneJeu::Titre) ? EVisibility::HitTestInvisible
	                                                                : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
