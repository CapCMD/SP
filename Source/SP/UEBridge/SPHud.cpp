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
#include "HAL/FileManager.h"
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
#include "Widgets/Input/SMultiLineEditableTextBox.h"
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
	// MAJ EST NOMMÉE, et il le faut : en impesanteur on ne s'arrête pas en lâchant
	// la touche, on s'agrippe (cf. app/impesanteur.hpp). Un joueur qui l'ignore
	// dérive jusqu'à la cloison suivante et croit à un bug — c'est la commande la
	// moins devinable du jeu, donc celle qui doit être écrite.
	const FString Haut = FString::Printf(
		TEXT("%s   |   ZQSD/WASD se deplacer   |   MAJ : s'agripper   |   "
		     "SOURIS : regarder   |   E : poste   |   M : carte   |   F5 : sauvegarder"),
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
	else if (Session->modal == fen::app::Modal::Passation)
	{
		// ═══ LA PASSATION [GDD 3.4, 3.5] ═══ Ce n'est PAS une fin de partie, et
		// l'écran doit le dire avant tout le reste : le poste change de titulaire,
		// l'agence continue. D'où un titre sobre et une seule action.
		fen::game::GameState* G = Session->jeu.ares.initialisee()
			? Session->jeu.ares.etat.get() : nullptr;
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 26, 0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("Passation", "PASSATION"))
			.Font(Mono(28.0f, 240)).ColorAndOpacity(FSlateColor(SRGB(230, 217, 179)))
		];
		// ⚠ SOUS-TITRE COURT, MESURÉ SUR CAPTURE : la première rédaction ajoutait
		// « - l'agence poursuit ses programmes » et la ligne SORTAIT du panneau
		// (560 u), la fin coupée net. Le HUD tronque au bord et c'est toujours la
		// FIN qui saute — ce qui compte se met devant, le reste va dans le corps,
		// qui lui est enveloppé.
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 18)
		[
			SNew(STextBlock)
			.Text(FText::FromString(
				G ? FString(G->passation_motif.c_str()) : FString(TEXT("fin de fonction"))))
			.Font(Mono(10.0f, 120)).ColorAndOpacity(FSlateColor(ColTexteFaible))
		];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 10)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PassSuite",
			              "Ce n'est pas une fin de partie : l'agence poursuit ses programmes "
			              "sous un nouvel Architecte. [GDD 3.4]"))
			.Font(Mono(10.0f, 30)).ColorAndOpacity(FSlateColor(ColTexte))
			.AutoWrapText(true).WrapTextAt(500.0f)
		];
		// CE QUE LE SUCCESSEUR TROUVE — lu du modèle, jamais recomposé ici.
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 6)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString(Session->resume_passation().c_str())))
			.Font(Mono(10.0f, 30)).ColorAndOpacity(FSlateColor(ColTexte))
			.AutoWrapText(true).WrapTextAt(500.0f)
		];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 16)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Ce qui ne se legue pas : la credibilite personnelle, le score, "
				     "la dose recue. [GDD 3.5]  Generation %d."),
				G ? G->generation + 1 : 2)))
			.Font(Mono(9.0f, 30)).ColorAndOpacity(FSlateColor(ColTexteFaible))
			.AutoWrapText(true).WrapTextAt(500.0f)
		];
		V->AddSlot().AutoHeight().Padding(26, 0, 26, 26)
		[ BoutonMenu(LOCTEXT("PrendreLePoste", "PRENDRE LE POSTE"),
		             FOnClicked::CreateLambda([this]() {
			             Session->passer_la_main();
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

// ═══════════════════════════════════════════════════════════════════════════
// LA COUPE DU VÉHICULE [GDD 12.2] — le dessin que l'atelier n'avait pas
// ═══════════════════════════════════════════════════════════════════════════
void SSPCoupe::Construct(const FArguments&) { SetCanTick(false); }

int32 SSPCoupe::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
                        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&,
                        bool) const
{
	const auto& H = fen::app::g_render_bridge.hull_design;
	if (!H.valid.load() || H.n <= 0 || H.length_m <= 0.0) return Layer;

	const FVector2D Taille = G.GetLocalSize();
	const double MargeX = 8.0, MargeY = 6.0;
	// UNE SEULE ÉCHELLE POUR LES DEUX AXES : une coupe qui étirerait la longueur
	// sans étirer le diamètre mentirait sur l'élancement, qui est justement ce
	// qu'un bureau d'études regarde en premier.
	const double EchX = (Taille.X - 2.0 * MargeX) / FMath::Max(0.01, H.length_m);
	const double EchY = (Taille.Y - 2.0 * MargeY) / FMath::Max(0.01, H.diameter_m);
	const double E = FMath::Min(EchX, EchY);
	const double X0 = MargeX;
	const double Yc = Taille.Y * 0.5;

	// L'AXE, comme sur tout plan en coupe.
	Ligne(Out, Layer, G, FVector2D(X0, Yc), FVector2D(X0 + H.length_m * E, Yc),
	      SRGB(90, 98, 108, 0.9f), 1.0f);

	static const FLinearColor ColRole[5] = {
		SRGB(120, 128, 140),   // ajutage
		SRGB(200, 210, 224),   // réservoir
		SRGB(140, 150, 162),   // interétage
		SRGB(150, 158, 170),   // charge utile
		SRGB(214, 140, 84),    // capsule
	};

	const int32 N = FMath::Min(H.n, fen::app::RenderBridge::HullSnap::MAX_SEG);
	for (int32 i = 0; i < N; ++i)
	{
		const auto& S = H.seg[i];
		const double xa = X0 + S.z0_m * E, xb = X0 + S.z1_m * E;
		const double ra = S.r0_m * E, rb = S.r1_m * E;
		const FLinearColor C = ColRole[FMath::Clamp(S.role, 0, 4)];
		// Le profil : deux génératrices symétriques, plus les deux sections. Un
		// interétage se dessine en TRAIT FIN — il enveloppe le moteur du dessus,
		// il ne s'ajoute pas à la pile (c'est la seule pièce qui se superpose).
		const float Ep = (S.role == 2) ? 0.7f : 1.4f;
		Ligne(Out, Layer + 1, G, FVector2D(xa, Yc - ra), FVector2D(xb, Yc - rb), C, Ep);
		Ligne(Out, Layer + 1, G, FVector2D(xa, Yc + ra), FVector2D(xb, Yc + rb), C, Ep);
		Ligne(Out, Layer + 1, G, FVector2D(xa, Yc - ra), FVector2D(xa, Yc + ra), C, Ep);
		Ligne(Out, Layer + 1, G, FVector2D(xb, Yc - rb), FVector2D(xb, Yc + rb), C, Ep);
	}
	return Layer + 2;
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
	// (l'ouverture, la fermeture) — les actions internes appellent Rebuild()
	// elles-mêmes quand elles modifient le modèle.
	const int32 P = Session ? Session->poste_ouvert : -1;
	if (P != PosteAffiche) { PosteAffiche = P; MoisAffiche = -1.0; Rebuild(); return; }

	// ═══ ...ET QUAND LE CALENDRIER A BOUGÉ ═══ [GDD 9.1, 14.2]
	// « Pas à chaque frame » était juste TANT QUE RIEN NE BOUGEAIT SANS CLIC.
	// Depuis que le temps COULE, un poste affiche des grandeurs que le monde
	// change tout seul : l'arrivée qui se rapproche, les vivres qui se
	// consomment, la dose qui monte. Figé, il MENT — trouvé en capture, où le
	// poste annonçait encore 897 jours d'autonomie et « arrivée dans 322 jours »
	// après quatre mois de vol, alors que le modèle, lui, était à 784 jours.
	// C'est [GDD 9.1] qui l'exige : on ne « surveille » pas une valeur figée.
	//
	// RYTHME BORNÉ à 5 Hz : une télémétrie n'a pas besoin de la fréquence
	// d'image, et reconstruire une quarantaine de widgets par frame serait payer
	// cher un chiffre que l'œil ne suit pas. Rien ne se reconstruit quand le
	// temps est en pause — l'état par défaut d'une partie.
	if (P < 0 || !Session || !Session->jeu.agence.creee) return;
	const double Mois = Session->jeu.agence.mois;
	if (Mois != MoisAffiche && InCurrentTime - DernierRefresh > 0.2)
	{
		MoisAffiche = Mois;
		DernierRefresh = InCurrentTime;
		Rebuild();
	}
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
	// ═══ L'ARCHITECTE VIEILLIT, ET IL FAUT QUE ÇA SE VOIE ═══ [GDD 3.4]
	// « Le personnage vieillit ; mort naturelle vers 85 ans » — une fin de
	// fonction qui tomberait sans prévenir serait une surprise, pas une échéance.
	// L'âge est en temps PROPRE : sur une architecture relativiste, il diverge du
	// calendrier, et c'est précisément ce que [GDD 3.4] veut rendre opposable.
	{
		const double age = G.character.age_bio_years();
		const double reste = fen::career::LIFE_EXPECTANCY_Y - age;
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[ LigneKV(TEXT("ARCHITECTE"),
		          FString::Printf(TEXT("%.0f ans  .  generation %d  .  %s"), age,
		                          G.generation,
		                          reste > 0.0
		                              ? *FString::Printf(TEXT("%.0f ans de fonction devant lui"), reste)
		                              : TEXT("fin de fonction")),
		          reste > 10.0 ? ColTexte
		          : reste > 3.0 ? SRGB(255, 189, 87) : SRGB(242, 90, 80)) ];
	}
	// ═══ LE SCORE DE PROMOTION, ET SES TROIS CRITÈRES ═══ [GDD 3.3]
	// Un total nu n'apprend rien : le joueur doit lire LEQUEL des trois critères
	// l'a fait progresser — c'est la seule façon de savoir quoi corriger. Le
	// détail vient du modèle (`career::score_mission`), le HUD ne calcule rien.
	{
		const int ir = static_cast<int>(G.career.rank);
		const bool terminal = fen::career::terminal_rank(G.career.rank);
		const double seuil = terminal ? 0.0 : fen::career::PROMOTION_THRESHOLDS[ir];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[ LigneKV(TEXT("SCORE DE PROMOTION"),
		          terminal
		              ? FString::Printf(TEXT("%.0f  .  rang terminal"), G.career.score)
		              : FString::Printf(TEXT("%.0f / %.0f  vers %s"), G.career.score, seuil,
		                                ANSI_TO_TCHAR(fen::career::rank_name(
		                                    static_cast<fen::career::Rank>(ir + 1)))),
		          SRGB(140, 179, 255)) ];
		const fen::career::MissionScore& d = Session->dernier_score_mission;
		if (d.reussite != 0.0 || d.budget != 0.0 || d.crise != 0.0)
		{
			Col->AddSlot().AutoHeight().Padding(22, 0, 0, 4)
			[ Txt(FString::Printf(
				  TEXT("dernier vol %+.0f pts : reussite %+.2f . budget %+.2f . crise %+.2f [3.3]"),
				  d.total(), d.reussite, d.budget, d.crise),
			      9.0f, ColTexteFaible, 0) ];
		}
	}
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

	// ═══ LE STOCK D'ANTIMATIÈRE [GDD 5.12.12, 19.3] ═══
	// N'apparaît QUE si la filière est qualifiée : afficher une ligne à zéro
	// pendant toute la partie ferait du bruit, pas de l'information. Une fois
	// visible, elle dit les trois choses qui décident de la fin de jeu — ce qu'on
	// a, où la fuite plafonne le stock, et l'écart à ce qu'un vol relativiste
	// exigerait. LE VERDICT EST AFFICHÉ, pas caché dans un modèle : c'est la
	// différence entre un cul-de-sac conçu et un cul-de-sac subi.
	if (const fen::tech::TechNode* NAm = G.tree.find("antimatiere"))
	if (NAm->operational())
	{
		const auto& Prod = G.antimatiere.prod;
		const double Eq = G.antimatiere.equilibrium_g();
		// LA CIBLE DE RÉFÉRENCE EST UN VOL HABITÉ, ALLER-RETOUR [GDD 3.4, 6.7.4].
		// « Le relativisme n'a d'intérêt que pour les vols habités » — une dilatation
		// que personne ne vit n'a aucune conséquence de jeu. Le nombre de poussées
		// vient donc de `CALIB_BURNS` (quatre) et non de 1 : afficher l'aller simple
		// annonçait une cible **26 fois trop basse**, donc « ATTEINT » bien avant que
		// le vol soit réellement payé.
		const double Cible = fen::rel::antimatter_needed_g(
			fen::rel::AntimatterProduction::CALIB_DRY_MASS_KG,
			fen::rel::AntimatterProduction::CALIB_TARGET_BETA,
			fen::rel::AntimatterProduction::CALIB_BURNS);
		const bool bHors = G.antimatiere.hors_atteinte(Cible);
		Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
		[ Txt(TEXT("ANTIMATIERE"), 12.0f, SRGB(200, 150, 255)) ];
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("STOCK CONFINE"),
		          FString::Printf(TEXT("%.3e g  /  %.1e g de capacite"),
		                          G.antimatiere.grams, Prod.confinement_capacity_g)) ];
		// LE DÉBIT DIT MAINTENANT D'OÙ IL VIENT. Il se lisait « marge Novellus » —
		// et c'était vrai, ce qui était le défaut : la station n'a rien à voir avec
		// une usine à antimatière [GDD 5.12.12]. Rendement ET puissance sont
		// affichés parce que ce sont les deux seuls leviers, et qu'ils sont dans
		// deux branches différentes.
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("DEBIT  (usine, branche 6)"),
		          FString::Printf(TEXT("%.3e g/an   %.0e W a rendement %.0e"),
		                          Prod.rate_g_yr(), Prod.plant_power_w,
		                          Prod.production_efficiency)) ];
		// « Plafonné par la fuite » n'est PAS « plafonné par le réservoir » : le
		// joueur qui agrandit son confinement sans rien changer d'autre doit
		// comprendre pourquoi il ne gagne rien.
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("PLAFOND REEL"),
		          FString::Printf(TEXT("%.3e g  (%s)"), Eq,
		                          G.antimatiere.borne_par_la_fuite()
		                              ? TEXT("borne par la fuite") : TEXT("borne par le confinement")),
		          SRGB(255, 190, 90)) ];
		// TROIS VERDICTS, PAS DEUX. « Hors d'atteinte », « dans N ans » et « atteint »
		// sont trois états distincts, et le troisième s'affichait « 0 ans » — ce qui
		// se lit « c'est instantané » alors que cela veut dire « c'est fait ». Le
		// stock couvre déjà la cible : le vol relativiste est PAYÉ, il ne reste qu'à
		// le concevoir [GDD 3.1].
		const bool bAtteint = !bHors && G.antimatiere.grams >= Cible;
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("VOL HABITE b=0,3 (aller-retour)"),
		          bHors
		              ? FString::Printf(TEXT("%.2e g requis - HORS D'ATTEINTE"), Cible)
		          : bAtteint
		              ? FString::Printf(TEXT("%.2e g requis - ATTEINT"), Cible)
		              : FString::Printf(TEXT("%.2e g requis - %.0f ans"), Cible,
		                                G.antimatiere.years_to_reach(Cible)),
		          bHors ? SRGB(242, 90, 80) : ColVert) ];
	}

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
		// ═══ CE QUE LA FILIÈRE TRAÎNE ═══ [GDD 5.12.1, 6.5]
		// N'apparaît que si une filière alimentée est au véhicule : sur une pile
		// chimique, la ligne serait un zéro permanent — donc du bruit.
		if (S.powerplant_mass_kg > 0.0)
		{
			Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
			[ LigneKV(TEXT("CENTRALE + RADIATEURS"),
			          FString::Printf(TEXT("%.0f kg  (%.0f %% du decollage)"), S.powerplant_mass_kg,
			                          S.liftoff_mass_kg > 0.0
			                              ? 100.0 * S.powerplant_mass_kg / S.liftoff_mass_kg : 0.0),
			          SRGB(255, 189, 87)) ];
		}
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

	// ═══ LA RENTRÉE EST UN VERROU, PAS UN DÉCOR ═══ [GDD 9.2, 7.6]
	// `flight/Reentry.hpp` ne décidait de RIEN pendant que la capsule portait cinq
	// champs qui n'existent que pour lui, et que l'arbre vendait trois nœuds de
	// rentrée. Le joueur voit maintenant d'où sa capsule sait revenir, à quel g, et
	// avec quelle marge — le corridor de retour lunaire fait 0,2°, il faut que ça
	// se voie.
	if (S.rentree.evalue)
	{
		Col->AddSlot().AutoHeight().Padding(22, 0, 0, 4)
		[ Txt(FString::Printf(
			  TEXT("RENTREE %.0f m/s  .  %.1f g  .  flux a %d %% du tenable  .  corridor %.2f deg"),
			  S.rentree.v_interface_ms, S.rentree.pic_g,
			  (int)FMath::RoundToInt(100.0 / FMath::Max(1e-9, S.rentree.marge_flux)),
			  S.rentree.largeur_corridor_rad * 180.0 / 3.14159265358979),
		      9.0f, S.rentree.ok ? ColTexteFaible : SRGB(242, 90, 80), 0) ];
	}

	// ═══ LA COUPE [GDD 12.2] ═══ « éditeur EN COUPE ». L'atelier n'avait que le
	// tableau de masses ; la géométrie du véhicule y manquait, et c'est elle que
	// [17.2] réutilise pour le RENDU. Les cotes ne sont pas des réglages : elles
	// tombent des ergols (donc de Tsiolkovsky), de la densité du couple et de la
	// section de la capsule.
	{
		const auto& HB = fen::app::g_render_bridge.hull_design;
		if (HB.valid.load() && HB.n > 0)
		{
			Col->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
			[ LigneKV(TEXT("COUPE"),
			          FString::Printf(TEXT("%.1f m x %.2f m  (elancement %.1f)"),
			                          HB.length_m, HB.diameter_m,
			                          HB.length_m / FMath::Max(0.01, HB.diameter_m)),
			          SRGB(140, 179, 255)) ];
			Col->AddSlot().AutoHeight().Padding(0, 2, 0, 4)
			[ SNew(SBox).HeightOverride(78.0f)[ SNew(SSPCoupe) ] ];
		}
	}

	// --- LA PILE D'ÉTAGES : du bas (index 0) vers le haut --------------------
	Col->AddSlot().AutoHeight().Padding(0, 2, 0, 2)
	[ Txt(TEXT("ETAGES (bas -> haut) — moteur, Delta-v confie, ergols"), 10.0f, ColTexteFaible, 90) ];

	// ═══ LES SOURCES QUE L'ARCHITECTE PEUT POSER SOUS UNE FILIÈRE ALIMENTÉE ═══
	// [GDD 5.12.1] `Chemical` = « pas encore choisie » : l'atelier le SIGNALE au
	// lieu d'en poser une à la place du joueur [anti-feature 1.5].
	static const fen::vehicle::PropTier SourcesDispo[] = {
		fen::vehicle::PropTier::Chemical, fen::vehicle::PropTier::Solar,
		fen::vehicle::PropTier::Rtg,      fen::vehicle::PropTier::Fission,
	};
	auto NomSource = [](fen::vehicle::PropTier t) -> FString {
		switch (t) {
			case fen::vehicle::PropTier::Solar:   return TEXT("SOLAIRE");
			case fen::vehicle::PropTier::Rtg:     return TEXT("RTG");
			case fen::vehicle::PropTier::Fission: return TEXT("REACTEUR");
			default:                              return TEXT("(aucune)");
		}
	};

	TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
	for (int k = 0; k < (int)VD.stages.size(); ++k)
	{
		const int ei = fen::app::detail::clampi(VD.stages[k].engine, 0, (int)engs.size() - 1);
		const FString mname(engs[ei].name);
		const FString ergols = (k < (int)S.stages.size())
			? FString::Printf(TEXT("%.0f kg"), S.stages[k].propellant_kg) : FString(TEXT("-"));
		TSharedRef<SVerticalBox> Etage = SNew(SVerticalBox);
		Etage->AddSlot().AutoHeight()
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

		// ═══ LA SECONDE LIGNE N'EXISTE QUE POUR UNE FILIÈRE ALIMENTÉE ═══
		// [GDD 5.12.1, 6.2, 6.5] « Énergie ≠ propulsion » : un propulseur
		// électrique ou NEP réclame une puissance qui SE DÉDUIT de sa poussée et
		// de son Isp, et cette puissance traîne une centrale et des radiateurs
		// qui pèsent bien plus que la tuyère. Le chimique et le NTP ne portent
		// rien — leur afficher une ligne vide serait du bruit (piège n°65).
		if (k < (int)S.stages.size() && S.stages[k].power.needs_power)
		{
			const fen::vehicle::PowerPlant& PP = S.stages[k].power;
			TSharedRef<SHorizontalBox> Ligne = SNew(SHorizontalBox);
			Ligne->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(22, 0, 4, 0)
			[ Txt(FString::Printf(TEXT("%.0f kW"), PP.p_electric_w / 1000.0),
			      9.0f, SRGB(140, 179, 255), 0) ];
			// La fusion et l'antimatière SONT leur propre source : leur proposer
			// un réacteur en plus serait une faute de modèle, pas une option.
			if (!PP.self_powered)
			{
				Ligne->AddSlot().AutoWidth().VAlign(VAlign_Center)
				[ BoutonMini(TEXT("<"), FOnClicked::CreateLambda([this, &VD, k]() {
					int i = 0; const int n = (int)UE_ARRAY_COUNT(SourcesDispo);
					for (int j = 0; j < n; ++j) if (SourcesDispo[j] == VD.stages[k].source) i = j;
					VD.stages[k].source = SourcesDispo[(i + n - 1) % n];
					Rebuild(); return FReply::Handled(); })) ];
				Ligne->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)
				[ SNew(SBox).WidthOverride(74.0f)
				  [ Txt(NomSource(VD.stages[k].source), 9.0f,
				        PP.source_missing ? SRGB(242, 90, 80) : ColTexte, 0) ] ];
				Ligne->AddSlot().AutoWidth().VAlign(VAlign_Center)
				[ BoutonMini(TEXT(">"), FOnClicked::CreateLambda([this, &VD, k]() {
					int i = 0; const int n = (int)UE_ARRAY_COUNT(SourcesDispo);
					for (int j = 0; j < n; ++j) if (SourcesDispo[j] == VD.stages[k].source) i = j;
					VD.stages[k].source = SourcesDispo[(i + 1) % n];
					Rebuild(); return FReply::Handled(); })) ];
			}
			// L'ENDURANCE DU RADIATEUR EST UNE CARACTÉRISTIQUE ACHETÉE [GDD 12.4, 6.5].
			// Sa surface excédentaire n'est plus un forfait : elle est dimensionnée
			// sur le flux micrométéoritique de Grün pour une durée d'exposition
			// donnée. Voler au-delà se paie en fiabilité, et le poste CONTROLE le
			// nomme — encore faut-il que le joueur sache pour combien de jours il a
			// construit. Sans ce chiffre affiché, la sanction serait imméritée.
			const fen::env::RadiatorSpec RS{};
			Ligne->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[ Txt(PP.source_missing
			          ? FString(TEXT("source a choisir [GDD 5.12.1]"))
			          // MESURÉ SUR CAPTURE : la rédaction longue (« paroi 1.5 mm,
			          // endurance 900 j ») était TRONQUÉE au bord du panneau, et
			          // c'est le mot « endurance » qui disparaissait — le seul qui
			          // porte l'information. Forme courte, même contenu.
			          : FString::Printf(TEXT("centrale %.0f kg  .  radiateur %.0f m2"
			                                 " (paroi %.1f mm, %.0f j)"),
			                            PP.total_mass_kg(), PP.radiator_area_m2,
			                            RS.wall_mm, RS.endurance_days),
			      9.0f, PP.source_missing ? SRGB(242, 90, 80) : ColTexteFaible, 0) ];
			Etage->AddSlot().AutoHeight().Padding(0, 1, 0, 3)[ Ligne ];
		}
		Scroll->AddSlot().Padding(4, 2)[ Etage ];
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
// ═══ OÙ VIT LA TOOLCHAIN ═══ [GDD 18]
// Le modèle ne devine PAS où le projet est installé : les chemins lui sont
// fournis, et c'est la couche plateforme qui les connaît. En développement on
// compile contre les en-têtes du dépôt avec le compilateur DÉJÀ présent ;
// l'embarquer dans la distribution reste une tâche de packaging, déclarée comme
// telle dans `fen/code/Toolchain.hpp`. Quand il manque, la chaîne rend
// `Indisponible` — et le poste le DIT, au lieu de faire croire à un code faux.
static void ConfigurerToolchain(fen::app::Session& Se)
{
	if (!Se.toolchain.dossier_travail.empty()) return;
	const FString Racine = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString Travail = FPaths::Combine(
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), TEXT("SP"), TEXT("CodeVol"));
	IFileManager::Get().MakeDirectory(*Travail, /*Tree*/ true);
	Se.toolchain.dossier_travail = TCHAR_TO_UTF8(*Travail);

	// ═══ LE SDK : EMBARQUÉ D'ABORD, DÉPÔT ENSUITE ═══ [GDD 18]
	// L'atelier compilait contre `Source/SP/SpaceProgram/…` — un chemin qui
	// **n'existe pas dans un build packagé**. Il marchait donc sur la machine de
	// développement et nulle part ailleurs, sans que rien ne le dise.
	// `Content/SP/Sdk` est produit par `Tools/stage_sdk.py` et empaqueté en
	// NON-UFS (lu par `cl.exe`, pas par Unreal — dans un .pak il serait
	// inatteignable). On préfère donc TOUJOURS l'embarqué, et on retombe sur
	// l'arbre source quand il n'a pas été staged : en développement, ça continue
	// de marcher sans rien faire.
	const FString Sdk = FPaths::Combine(
		FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), TEXT("SP"), TEXT("Sdk"));
	const FString SdkInc = FPaths::Combine(Sdk, TEXT("include"));
	const FString Src = FPaths::Combine(Racine, TEXT("Source"), TEXT("SP"), TEXT("SpaceProgram"));
	if (IFileManager::Get().DirectoryExists(*SdkInc))
	{
		Se.toolchain.includes = { TCHAR_TO_UTF8(*SdkInc) };
		Se.toolchain.sources = {
			TCHAR_TO_UTF8(*FPaths::Combine(Sdk, TEXT("src"), TEXT("Kepler.cpp"))),
		};
	}
	else
	{
		Se.toolchain.includes = {
			TCHAR_TO_UTF8(*FPaths::Combine(Src, TEXT("astro_core"), TEXT("include"))),
			TCHAR_TO_UTF8(*FPaths::Combine(Src, TEXT("mission"), TEXT("include"))),
			TCHAR_TO_UTF8(*Src),
		};
		// `ares::vol` n'est plus fait que d'en-têtes : son solveur résout la
		// correction sur la vraie matrice de transition, donc le programme du
		// joueur doit LIER le propagateur képlérien du moteur — le même, pas une
		// copie.
		Se.toolchain.sources = {
			TCHAR_TO_UTF8(*FPaths::Combine(Src, TEXT("astro_core"), TEXT("src"), TEXT("Kepler.cpp"))),
		};
	}

	// ═══ LE COMPILATEUR : EMBARQUÉ D'ABORD, MACHINE ENSUITE ═══ [GDD 18]
	// Le premier candidat est le point de DÉPÔT de la distribution : une
	// distribution qui y pose les Build Tools fonctionne sans toucher à ce code.
	// Les quatre suivants sont le mode développement — pratique ici, mais on ne
	// peut pas en faire une hypothèse chez le joueur.
	Se.toolchain.vcvars.clear();
	const FString Embarque = FPaths::Combine(
		FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()),
		TEXT("SP"), TEXT("Toolchain"), TEXT("VC"), TEXT("Auxiliary"), TEXT("Build"),
		TEXT("vcvars64.bat"));
	static const TCHAR* Candidats[] = {
		TEXT("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"),
		TEXT("C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvars64.bat"),
		TEXT("C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvars64.bat"),
		TEXT("C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat"),
	};
	if (IFileManager::Get().FileExists(*Embarque))
	{
		Se.toolchain.vcvars = TCHAR_TO_UTF8(*Embarque);
	}
	else
	{
		for (const TCHAR* C : Candidats)
		{
			if (IFileManager::Get().FileExists(C)) { Se.toolchain.vcvars = TCHAR_TO_UTF8(C); break; }
		}
	}
	// OÙ IL FAUDRAIT LE METTRE, pour que le poste puisse le DIRE. Un atelier qui
	// refuse sans indiquer le chemin attendu est une panne aux yeux du joueur
	// (piège n°42) — et ici la réparation est à sa portée.
	Se.toolchain_depot = TCHAR_TO_UTF8(*Embarque);
}

// ═══ L'ATELIER LOGICIEL — mode PRO [GDD 15.1, 15.5, 18] ═══
// « Le joueur écrit du VRAI C++, compilé et exécuté par une toolchain
// embarquée. » Cette vue est la SURFACE de la chaîne déjà vérifiée : on écrit,
// on compile (coût nul), on qualifie au banc (qui coûte du budget et des jours),
// puis on téléverse — et seulement alors le code monte à bord.
//
// CE QU'ELLE REFUSE DE FAIRE : proposer un bouton « corriger mon code », donner
// une solution toute faite, ou laisser monter à bord un texte que le banc n'a
// jamais exercé. Une procédure prête à rejouer est exactement ce que [GDD 2.4]
// interdit ; en PRO il n'y a même plus de graphe — il y a le code, et son prix.
TSharedRef<SWidget> SSPPoste::BuildCodeVol()
{
	int n = 0; const fen::app::PosteDef* defs = fen::app::postes_def(n);
	const fen::app::PosteDef& D = defs[3];
	fen::app::Session& Se = *Session;
	ConfigurerToolchain(Se);
	fen::mission::Mission* mc = Se.mission_courante();

	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);

	// --- L'ÉTAT DE LA CHAÎNE, en une ligne : où en est CE texte ---------------
	// Les trois étapes de [GDD 15.5] portent sur un TEXTE, pas sur le joueur :
	// éditer une ligne après le banc invalide la fiche. Le dire ici évite qu'on
	// croie voler avec un code qualifié qu'on a modifié depuis.
	{
		const bool bComp = Se.source_vol_compilee() && Se.resultat_vol.ok();
		const bool bCert = Se.source_vol_certifiee();
		const bool bBord = Se.source_vol_a_bord();
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ Txt(TEXT("CHAINE"), 10.0f, ColTexteFaible, 90) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
			[ Txt(bComp ? TEXT("[x] COMPILE") : TEXT("[ ] COMPILE"), 10.0f,
			      bComp ? ColVert : ColTexteFaible) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
			[ Txt(bCert ? TEXT("[x] QUALIFIE") : TEXT("[ ] QUALIFIE"), 10.0f,
			      bCert ? ColVert : ColTexteFaible) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
			[ Txt(bBord ? TEXT("[x] A BORD") : TEXT("[ ] A BORD"), 10.0f,
			      bBord ? ColVert : ColTexteFaible) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
			[ BoutonAction(FText::FromString(TEXT("< CONDUITE DE MISSION")),
			               FOnClicked::CreateLambda([this]() {
				               Session->atelier_logiciel = false; Rebuild(); return FReply::Handled(); })) ]
		];
		// Le cas qui trompe : un code EST à bord, mais ce n'est plus celui-ci.
		if (Se.code_a_bord && !bBord)
		{
			Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
			[ Txt(TEXT("Texte MODIFIE depuis le televersement : le bord execute l ancienne version."),
			      9.0f, SRGB(255, 190, 90), 20) ];
		}
		else if (Se.cert_vol.certified && !bCert)
		{
			Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
			[ Txt(TEXT("Texte MODIFIE depuis le banc : la fiche de qualification ne le couvre plus."),
			      9.0f, SRGB(255, 190, 90), 20) ];
		}
	}

	// --- LE CODE ---------------------------------------------------------------
	Col->AddSlot().FillHeight(1.0f).Padding(0, 2)
	[
		SNew(SBorder).BorderImage(Plein())
		.BorderBackgroundColor(FSlateColor(SRGB(6, 10, 18, 0.96f)))
		.Padding(FMargin(4))
		[
			SNew(SMultiLineEditableTextBox)
			.Text(FText::FromString(FString(UTF8_TO_TCHAR(Se.source_vol.c_str()))))
			.Font(Mono(9.0f, 0))
			.AllowMultiLine(true)
			.OnTextChanged_Lambda([this](const FText& T) {
				Session->source_vol = TCHAR_TO_UTF8(*T.ToString());
			})
		]
	];

	// --- CE QUE LA CHAÎNE A RÉPONDU -------------------------------------------
	if (Se.resultat_vol_lu)
	{
		const bool bOk = Se.resultat_vol.ok();
		Col->AddSlot().AutoHeight().Padding(0, 3, 0, 1)
		[ LigneKV(TEXT("TOOLCHAIN"), FString(UTF8_TO_TCHAR(fen::code::issue_nom(Se.resultat_vol.issue))),
		          bOk ? ColVert : SRGB(242, 90, 80)) ];
		if (Se.resultat_vol.issue == fen::code::IssueCode::Indisponible)
		{
			// NE PAS FAIRE PASSER UNE MACHINE MAL EQUIPEE POUR UN CODE FAUX
			// (piège n°69) : la distinction est dite, pas devinée. Et on DONNE
			// le chemin attendu : un atelier qui refuse sans dire ce qu'il
			// attend est une panne, alors que la reparation est a portee.
			Col->AddSlot().AutoHeight().Padding(6, 0)
			[ Txt(TEXT("Aucun compilateur : ni embarque dans la distribution, ni installe ici."),
			      9.0f, ColTexteFaible, 20) ];
			Col->AddSlot().AutoHeight().Padding(6, 0)
			[ Txt(FString::Printf(TEXT("Deposer les MSVC Build Tools de sorte que ce chemin existe :")),
			      9.0f, ColTexteFaible, 20) ];
			Col->AddSlot().AutoHeight().Padding(10, 0)
			[ Txt(FString(UTF8_TO_TCHAR(Se.toolchain_depot.c_str())), 9.0f,
			      SRGB(255, 190, 90), 20) ];
		}
		else if (!Se.resultat_vol.diagnostics.empty() && !bOk)
		{
			// Les diagnostics du compilateur, TELS QUELS [GDD 15.5]. Trois lignes
			// suffisent à situer l'erreur ; le cadre clippe le reste (piège n°42).
			FString Diag = FString(UTF8_TO_TCHAR(Se.resultat_vol.diagnostics.c_str()));
			TArray<FString> Lignes; Diag.ParseIntoArrayLines(Lignes);
			int32 Montrees = 0;
			for (const FString& L : Lignes)
			{
				if (L.TrimStartAndEnd().IsEmpty()) continue;
				Col->AddSlot().AutoHeight().Padding(6, 0)
				[ Txt(L.Left(96), 9.0f, SRGB(242, 90, 80), 0) ];
				if (++Montrees >= 3) break;
			}
		}
		else if (bOk)
		{
			const fen::code::DecisionsVol& Dec = Se.resultat_vol.decisions;
			Col->AddSlot().AutoHeight().Padding(6, 0)
			[ LigneKV(TEXT("DECISION"),
			          Dec.execute
			              ? FString::Printf(TEXT("EXECUTE  %.2f m/s"), fen::norm(Dec.dv))
			              : FString::Printf(TEXT("rien execute  (%d differee(s), replan %.0f h)"),
			                                Dec.differees, Dec.replan_s / 3600.0),
			          Dec.execute ? ColVert : SRGB(140, 179, 255)) ];
			for (const std::string& A : Dec.alertes)
			{
				Col->AddSlot().AutoHeight().Padding(6, 0)
				[ Txt(FString::Printf(TEXT("! %s"), UTF8_TO_TCHAR(A.c_str())), 9.0f, SRGB(255, 190, 90), 0) ];
			}
			for (const std::string& J : Dec.journal)
			{
				Col->AddSlot().AutoHeight().Padding(6, 0)
				[ Txt(FString::Printf(TEXT("  %s"), UTF8_TO_TCHAR(J.c_str())), 9.0f, ColTexteFaible, 0) ];
			}
		}
	}

	// --- LA CAMPAGNE D'ESSAI : ce qu'on DÉCLARE couvrir, et ce que ça coûte ----
	// Le banc RASSURE SANS GARANTIR [GDD 15.5]. Chaque réglage est montré AVEC sa
	// conséquence chiffrée AVANT le clic : un bouton qui prélève un budget sans
	// dire combien serait un piège (même doctrine que la marge, piège n°64).
	if (mc)
	{
		const fen::code::Certification Prev = fen::code::run_test_bench(
			mc->contract.id, /*compiled*/ true, Se.domaine_vise(*mc), Se.banc_heures);
		Col->AddSlot().AutoHeight().Padding(0, 4, 0, 1)
		[ Txt(TEXT("BANC D'ESSAI — ce qu'on declare avoir exerce"), 10.0f, ColTexteFaible, 90) ];

		auto Reglage = [&](const TCHAR* Nom, double& Valeur, double Pas, const FString& Lu)
		{
			return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(SBox).WidthOverride(140.0f)[ Txt(Nom, 10.0f, ColTexte) ] ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
			[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this, &Valeur, Pas]() {
				Valeur = FMath::Max(Pas, Valeur - Pas); Rebuild(); return FReply::Handled(); })) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(SBox).WidthOverride(110.0f)[ Txt(Lu, 10.0f, SRGB(140, 179, 255), 0) ] ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this, &Valeur, Pas]() {
				Valeur += Pas; Rebuild(); return FReply::Handled(); })) ];
		};
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ Reglage(TEXT("HEURES D'ESSAI"), Se.banc_heures, 50.0,
		          FString::Printf(TEXT("%.0f h"), Se.banc_heures)) ];
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ Reglage(TEXT("PLAGE 3s COUVERTE"), Se.banc_borne_sigma3_m, 2000.0,
		          FString::Printf(TEXT("0 - %.0f km"), Se.banc_borne_sigma3_m / 1000.0)) ];
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[ SNew(SBox).WidthOverride(180.0f)
			  [ BoutonAction(FText::FromString(FString::Printf(TEXT("%s PROFILS DEGRADES"),
			                 Se.banc_degrade ? TEXT("[x]") : TEXT("[ ]"))),
			                 FOnClicked::CreateLambda([this]() {
				                 Session->banc_degrade = !Session->banc_degrade;
				                 Rebuild(); return FReply::Handled(); })) ] ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
			[ SNew(SBox).WidthOverride(180.0f)
			  [ BoutonAction(FText::FromString(FString::Printf(TEXT("%s INTERFACES"),
			                 Se.banc_interfaces ? TEXT("[x]") : TEXT("[ ]"))),
			                 FOnClicked::CreateLambda([this]() {
				                 Session->banc_interfaces = !Session->banc_interfaces;
				                 Rebuild(); return FReply::Handled(); })) ] ]
		];
		// LA LECTURE QUI MOTIVE CES BOUTONS : couverture attendue, prix, retard.
		// Élargir le domaine DILUE la couverture — c'est ici que ça se voit.
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("SI ON LANCE LE BANC"),
		          FString::Printf(TEXT("couverture %.0f %%   %.1f M EUR   +%.1f j"),
		                          Prev.coverage * 100.0, Prev.budget_spent_me,
		                          fen::code::bench_delay_days(Prev)),
		          SRGB(140, 179, 255)) ];
		// LA FICHE OBTENUE — et son domaine, qui est tout ce qu'elle vaut.
		if (Se.cert_vol.certified)
		{
			const TCHAR* Conf = (Se.cert_vol.confidence == fen::reliability::Confidence::A) ? TEXT("A")
			                  : (Se.cert_vol.confidence == fen::reliability::Confidence::B) ? TEXT("B")
			                  : (Se.cert_vol.confidence == fen::reliability::Confidence::C) ? TEXT("C") : TEXT("D");
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("FICHE"),
			          FString::Printf(TEXT("%s  couverture %.0f %%  confiance %s  |  %s, 3s < %.0f km%s"),
			                          Se.source_vol_certifiee() ? TEXT("VALIDE") : TEXT("PERIMEE"),
			                          Se.cert_vol.coverage * 100.0, Conf,
			                          UTF8_TO_TCHAR(Se.cert_vol.domain.environment.c_str()),
			                          Se.cert_vol.domain.input_hi / 1000.0,
			                          Se.cert_vol.domain.degraded_profiles ? TEXT(", degrades") : TEXT("")),
			          Se.source_vol_certifiee() ? ColVert : SRGB(255, 190, 90)) ];
			Col->AddSlot().AutoHeight().Padding(6, 0)
			[ Txt(TEXT("Le banc RASSURE, il ne garantit pas : un etat non imagine passe toujours."),
			      9.0f, ColTexteFaible, 20) ];
			// L'AVERTISSEMENT DOIT ARRIVER QUAND IL EST ENCORE ACTIONNABLE.
			// « Un code qualifie en orbite basse n'est PAS qualifie pour Mars »
			// [GDD 15.5] : ce desaccord-la est connu AVANT le feu vert, et le
			// feu vert le consignera sur la mission (`code_non_couvert`), d'ou
			// `fly_mission` en tirera un echec. Le dire au debrief serait une
			// sanction ; le dire ici est une decision.
			const FString EnvFiche(UTF8_TO_TCHAR(Se.cert_vol.domain.environment.c_str()));
			const FString EnvMission(UTF8_TO_TCHAR(fen::app::Session::env_vol(*mc)));
			if (Se.source_vol_a_bord() && EnvFiche != EnvMission)
			{
				Col->AddSlot().AutoHeight().Padding(6, 1)
				[ Txt(FString::Printf(TEXT("HORS DOMAINE AU DECOLLAGE : qualifie \"%s\", cette mission vole \"%s\"."),
				                      *EnvFiche, *EnvMission),
				      9.0f, SRGB(242, 90, 80), 20) ];
				Col->AddSlot().AutoHeight().Padding(6, 0)
				[ Txt(TEXT("Requalifier sur ce profil, ou ne pas embarquer ce code."),
				      9.0f, ColTexteFaible, 20) ];
			}
		}
	}

	// --- LES TROIS ÉTAPES, DANS L'ORDRE [GDD 15.5] ----------------------------
	Col->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 3, 0)
		[ BoutonAction(FText::FromString(TEXT("COMPILER")),
		               FOnClicked::CreateLambda([this]() {
			               Session->compiler_vol(Session->mission_courante());
			               Rebuild(); return FReply::Handled(); })) ]
		// Le banc n'est offert QUE sur un texte qui compile : [GDD 15.5] étape 1
		// avant étape 2, et un code faux ne coûte donc jamais un centime.
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(3, 0)
		[ BoutonAction(FText::FromString(TEXT("BANC D'ESSAI")),
		               FOnClicked::CreateLambda([this]() {
			               if (fen::mission::Mission* M = Session->mission_courante())
				               Session->banc_essai_vol(*M);
			               Rebuild(); return FReply::Handled(); }),
		               mc != nullptr && Se.source_vol_compilee() && Se.resultat_vol.ok()) ]
		// « TOUT code de vol passe par un banc d'essai avant televersement. »
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(3, 0, 0, 0)
		[ BoutonAction(FText::FromString(TEXT("TELEVERSER")),
		               FOnClicked::CreateLambda([this]() {
			               Session->televerser_vol(); Rebuild(); return FReply::Handled(); }),
		               Se.source_vol_certifiee()) ]
	];

	return FramePoste(D, Col);
}

// ═══ CE QUI EST TOMBÉ EN PANNE, ET CE QU'ON PEUT Y FAIRE ═══ [GDD 9.5, 9.1]
// « Diagnostics / réparations » : c'est la seule ligne du chapitre 9 qui demande
// une ACTION du joueur à bord. Une avarie qu'on ne pourrait que regarder serait
// un bandeau d'ambiance ; ici elle coûte des vivres CHAQUE JOUR, et la réparer
// arrête l'hémorragie. D'où l'affichage du coût à côté du bouton : sans lui,
// réparer serait un geste de confort au lieu d'un arbitrage.
void SSPPoste::AjouterAvaries(const TSharedRef<SVerticalBox>& Col,
                              fen::app::Session& Se, fen::game::GameState& G)
{
	const double Now = G.clock.now_days();
	const auto Eff = fen::mission::effets_avaries(G.avaries, Now);
	if (Eff.n_actives <= 0) return;

	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 1)
	[ Txt(FString::Printf(TEXT("AVARIES EN COURS (%d)"), Eff.n_actives),
	      11.0f, SRGB(242, 90, 80)) ];
	if (Eff.recup_eau_restante < 1.0 || Eff.recup_o2_restante < 1.0)
		Col->AddSlot().AutoHeight().Padding(12, 0)
		[ Txt(FString::Printf(TEXT("boucles degradees : eau %.0f %%  O2 %.0f %% du nominal"),
		                      Eff.recup_eau_restante * 100.0, Eff.recup_o2_restante * 100.0),
		      9.0f, SRGB(255, 190, 90), 30) ];
	if (Eff.fuite_o2_kg_j > 0.0)
		Col->AddSlot().AutoHeight().Padding(12, 0)
		[ Txt(FString::Printf(TEXT("fuite : %.2f kg d air et %.2f kg d eau par jour"),
		                      Eff.fuite_o2_kg_j, Eff.fuite_eau_kg_j),
		      9.0f, SRGB(255, 190, 90), 30) ];
	if (Eff.sol_injoignable)
		Col->AddSlot().AutoHeight().Padding(12, 0)
		[ Txt(TEXT("le sol est injoignable — plus de commande depuis la Terre"),
		      9.0f, SRGB(255, 190, 90), 30) ];

	const fen::mission::CapaciteBord Cap = Se.capacite_bord();
	for (std::size_t k = 0; k < G.avaries.size(); ++k)
	{
		const fen::mission::Avarie& A = G.avaries[k];
		if (!A.active(Now)) continue;
		const bool EnCours = A.en_reparation(Now);
		const FString Nom = FString::Printf(TEXT("%hs  (gravite %.0f %%)"),
		                                    fen::mission::event_name(A.kind), A.gravite01 * 100.0);
		Col->AddSlot().AutoHeight().Padding(12, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[ Txt(Nom, 10.0f, EnCours ? SRGB(255, 190, 90) : ColTexte, 44) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ EnCours
				? Txt(FString::Printf(TEXT("reparation : %.1f j"), A.fin_reparation_days - Now),
				      9.0f, SRGB(255, 190, 90), 0)
				: (fen::mission::reparable(A.kind, Cap)
					? BoutonMini(TEXT("REPARER"), FOnClicked::CreateLambda([this, k]() {
						Session->reparer_avarie(k); Rebuild(); return FReply::Handled(); }))
					: Txt(TEXT("hors capacite"), 9.0f, SRGB(242, 90, 80), 0)) ]
		];
	}
	// LE MOTIF DU REFUS NOMME LA TECHNO MANQUANTE (piège n°42) : sans lui,
	// « hors capacité » serait une impasse au lieu d'un objectif de recherche.
	if (!Se.dernier_refus_reparation.empty())
		Col->AddSlot().AutoHeight().Padding(12, 0, 0, 2)
		[ Txt(FString(Se.dernier_refus_reparation.c_str()), 9.0f, SRGB(242, 90, 80), 44) ];
}

TSharedRef<SWidget> SSPPoste::BuildControle()
{
	int n = 0; const fen::app::PosteDef* defs = fen::app::postes_def(n);
	const fen::app::PosteDef& D = defs[3];
	fen::app::Session& Se = *Session;

	// En PRO, le poste a une seconde face : l'atelier logiciel [GDD 15.1].
	if (Se.atelier_logiciel && Se.jeu.agence.mode == fen::app::ModeAide::Pro) return BuildCodeVol();
	if (Se.jeu.agence.mode != fen::app::ModeAide::Pro) Se.atelier_logiciel = false;

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
	// ═══ CE QUI EST EN PANNE PASSE AVANT TOUT LE RESTE ═══ [GDD 9.5, 9.1]
	// EN TÊTE DU POSTE, et c'est un choix d'ergonomie payé en capture : placées
	// après le bilan de viabilité et la vie à bord, les avaries tombaient sous la
	// ligne de flottaison de la zone déroulante. Une alarme qu'il faut aller
	// chercher n'est pas une alarme — et le bilan de viabilité, lui, est une
	// préoccupation de CONCEPTION, pas de conduite.
	if (Se.jeu.ares.initialisee())
	{
		fen::game::GameState& GA = *Se.jeu.ares.etat;
		if (GA.lived.active && GA.lived.mission_id == mc->contract.id)
			AjouterAvaries(Col, Se, GA);
	}

	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
	[ LigneKV(TEXT("MISSION"), FString(mc->contract.title.c_str()), SRGB(255, 220, 120)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
	[ LigneKV(TEXT("PHASE"), FString(fen::mission::state_name(mc->state)), SRGB(140, 179, 255)) ];
	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
	[ LigneKV(TEXT("BUDGET CONTRAT"), FString::Printf(TEXT("%.0f M EUR"), mc->contract.terms.budget_musd)) ];

	// LA PORTE DE L'ATELIER LOGICIEL. Un mécanisme correct mais inatteignable est
	// un mécanisme absent (piège n°40) : sans ce bouton, toute la toolchain
	// vérifiée resterait un fichier que personne n'ouvre. En NORMAL, elle n'a pas
	// à exister — l'assistance y est le graphe [GDD 2.2].
	if (Se.jeu.agence.mode == fen::app::ModeAide::Pro)
	{
		Col->AddSlot().AutoHeight().Padding(0, 1, 0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[ Txt(Se.source_vol_a_bord() ? TEXT("Logiciel de vol : A BORD")
			                             : TEXT("Logiciel de vol : rien a bord"),
			      10.0f, Se.source_vol_a_bord() ? ColVert : ColTexteFaible, 20) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[ BoutonAction(FText::FromString(TEXT("ATELIER LOGICIEL >")),
			               FOnClicked::CreateLambda([this]() {
				               Session->atelier_logiciel = true; Rebuild(); return FReply::Handled(); })) ]
		];
	}

	// --- LE VOL EN COURS : sa phase et sa date d'arrivée [GDD 9, 14.3] -------
	// Le vol DURE (fen/mission/FlightTimeline.hpp) : ascension, parking,
	// injection, croisière, puis insertion ou EDL. Sans ces deux lignes, une
	// mission lancée aurait l'air figée pendant des mois de temps de jeu.
	if (mc->state == St::Launched && Session->jeu.ares.initialisee())
	{
		const double NowDays = Session->jeu.ares.etat->clock.now_days();
		const fen::mission::FlightPhase Ph = fen::mission::flight_phase_of(*mc, NowDays);
		const fen::mission::ArrivalStatus Arr = fen::mission::flight_arrival(*mc, NowDays);
		const bool bCritique = fen::mission::is_critical_phase(Ph);
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
		[ LigneKV(TEXT("PHASE DE VOL"), FString(fen::mission::phase_name(Ph)),
		          bCritique ? SRGB(255, 190, 90) : ColVert) ];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
		[ LigneKV(TEXT("ARRIVEE"),
		          !Arr.dated ? FString(TEXT("cible non nommee : non datee"))
		          : Arr.arrived ? FString(TEXT("atteinte"))
		          : FString::Printf(TEXT("dans %.0f jours"), Arr.reste_jours),
		          Arr.dated && !Arr.arrived ? SRGB(140, 179, 255) : ColVert) ];
		// LE CORRIDOR SE LIT ICI [GDD 8.3] : « Terminal : ... incertitude 1σ/3σ ».
		// La carte le DESSINE, mais rapporté à l'échelle du système il ne fait que
		// quelques pixels — ce qui n'est pas séparable à l'écran doit être CHIFFRÉ
		// (même doctrine que Novellus, piège n°41).
		if (Se.trace_vol.ok && Se.trace_vol.corridor_3s_m > 0.0)
		{
			Col->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
			[ LigneKV(TEXT("CORRIDOR 3s"),
			          FString::Printf(TEXT("%.0f km  (sans poursuite)"),
			                          Se.trace_vol.corridor_3s_m / 1000.0),
			          SRGB(255, 190, 90)) ];
		}

		// ═══ LA CORRECTION EST UN ACTE DU JOUEUR [GDD 7.4] ═══
		// À la date d'une manœuvre critique, le monde est retombé au temps réel
		// (le plafond de cadence l'impose) et le joueur a quelque chose à FAIRE :
		// lire sa solution de navigation, décider d'un Δv, l'exécuter. Le modèle
		// applique littéralement ce qu'il commande.
		const fen::mission::VueNavigation Vue = Se.vue_vol(*mc);
		if (Vue.ok && bCritique)
		{
			Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
			[ Txt(TEXT("SOLUTION DE NAVIGATION — correction de mi-parcours"), 10.0f, ColTexteFaible, 90) ];
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("MANQUE AU BUT PROJETE"),
			          FString::Printf(TEXT("%.0f km  (1s : %.0f km)"), Vue.manque_km, Vue.sigma_r / 1000.0),
			          SRGB(255, 190, 90)) ];
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("MARGE RESTANTE"),
			          FString::Printf(TEXT("%.0f / %.0f m/s"),
			                          Se.mission_plan.program.dv_margin - mc->tcm_dv_depense, Se.mission_plan.program.dv_margin),
			          mc->tcm_dv_depense < Se.mission_plan.program.dv_margin ? ColVert : SRGB(242, 90, 80)) ];
			// LE DÉLAI DE COMMUNICATION — que [GDD 8.3] liste depuis toujours
			// parmi ce que le plan terminal doit afficher, et que rien ne
			// montrait. Ce n'est pas décoratif : ce qu'on commande ici part
			// maintenant et n'arrive là-bas qu'après ce délai.
			{
				const double Dm = Vue.delai_com_s / 60.0;
				Col->AddSlot().AutoHeight().Padding(6, 1)
				[ LigneKV(TEXT("DELAI DE COMMUNICATION"),
				          FString::Printf(TEXT("%.0f min %02.0f s  (aller)  -  la commande arrive apres"),
				                          std::floor(Dm), Vue.delai_com_s - std::floor(Dm) * 60.0),
				          SRGB(255, 190, 90)) ];
				// LA BOUCLE SOL SE FERME-T-ELLE ? [GDD 9.6] La comparaison de
				// l'aller-retour a la duree PROPRE de la manoeuvre — deux
				// grandeurs physiques, aucun seuil de confort. C'est ce qui dit
				// au joueur pourquoi une descente ne se pilote pas du sol.
				const fen::app::Session::BoucleSol B = Se.boucle_sol(*mc);
				if (B.valide && B.duree_phase_s > 0.0)
				{
					Col->AddSlot().AutoHeight().Padding(6, 1)
					[ LigneKV(TEXT("BOUCLE SOL"),
					          B.fermee
					            ? FString::Printf(TEXT("FERMEE  -  %.0f s aller-retour contre %.0f min de manoeuvre"),
					                              B.aller_retour_s, B.duree_phase_s / 60.0)
					            : FString::Printf(TEXT("OUVERTE  -  %.0f min aller-retour contre %.0f min : conduite A BORD"),
					                              B.aller_retour_s / 60.0, B.duree_phase_s / 60.0),
					          B.fermee ? ColVert : SRGB(242, 90, 80)) ];
				}
			}

			// ═══ LE RYTHME DE MESURE EST UN CHOIX [GDD 8.6] ═══
			// « Trop rare laisse deriver, trop frequent coute des ressources et
			// du temps. » Sans ce bouton il n'y a rien a choisir : la poursuite
			// etait achetee une fois a la conception et ne bougeait plus. Le prix
			// est montre AVANT le clic, comme la marge et le banc (piege n°64).
			{
				const double Arc = mc->arc_poursuite_j;
				Col->AddSlot().AutoHeight().Padding(6, 1)
				[ LigneKV(TEXT("ARC DE POURSUITE"),
				          FString::Printf(TEXT("%.1f j exploites  -  1s en vitesse : %.3f m/s"),
				                          Arc, mc->nav_sigma_v),
				          Arc > 0.0 ? ColVert : SRGB(242, 90, 80)) ];
				const double CoutPasse = fen::mission::cout_poursuite_me(7.0);
				Col->AddSlot().AutoHeight().Padding(6, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[ Txt(FString::Printf(TEXT("SI ON ECOUTE 7 JOURS DE PLUS : %.3f M EUR d antenne"), CoutPasse),
					      9.0f, ColTexteFaible, 70) ]
					+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Right).VAlign(VAlign_Center)
					[
						BoutonAction(FText::FromString(TEXT("ACHETER 7 J D ECOUTE")),
						             FOnClicked::CreateLambda([this, mc]() {
							             Session->acheter_poursuite(*mc, 7.0);
							             return FReply::Handled();
						             }),
						             Se.jeu.ares.etat->finance.treasury_me >= CoutPasse)
					]
				];
			}

			// Les trois composantes, en repère RSW — celui que le cœur déclare
			// comme « LE repère dans lequel le joueur exprime ses Delta-v ».
			auto AxeDv = [&](const TCHAR* Nom, int Axe)
			{
				return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(SBox).WidthOverride(150.0f)[ Txt(Nom, 10.0f, ColTexte) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
				[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this, Axe]() {
					Session->tcm_commande[Axe] -= 1.0; Rebuild(); return FReply::Handled(); })) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(SBox).WidthOverride(80.0f)
				  [ Txt(FString::Printf(TEXT("%.1f m/s"), Session->tcm_commande[Axe]), 10.0f, SRGB(140, 179, 255), 0) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this, Axe]() {
					Session->tcm_commande[Axe] += 1.0; Rebuild(); return FReply::Handled(); })) ];
			};
			Col->AddSlot().AutoHeight().Padding(6, 1)[ AxeDv(TEXT("Dv RADIAL   (R)"), 0) ];
			Col->AddSlot().AutoHeight().Padding(6, 1)[ AxeDv(TEXT("Dv LONGITUD.(S)"), 1) ];
			Col->AddSlot().AutoHeight().Padding(6, 1)[ AxeDv(TEXT("Dv HORS-PLAN(W)"), 2) ];

			Col->AddSlot().AutoHeight().Padding(6, 3)
			[
				SNew(SHorizontalBox)
				// L'ASSISTANCE DÉPEND DU MODE, ET DE RIEN D'AUTRE [GDD 2.2]. En
				// NORMAL, le solveur est un nœud préconstruit qu'on peut appeler ;
				// en PRO, il n'existe pas — le joueur fait son calcul. C'est le
				// premier endroit du jeu où `ModeAide` change quelque chose.
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[
					BoutonAction(FText::FromString(TEXT("EXECUTER LA CORRECTION")),
					             FOnClicked::CreateLambda([this, mc]() {
						             Session->executer_tcm(*mc,
						                 fen::Vec3{Session->tcm_commande[0],
						                           Session->tcm_commande[1],
						                           Session->tcm_commande[2]});
						             Session->tcm_commande[0] = Session->tcm_commande[1] =
						                 Session->tcm_commande[2] = 0.0;
						             Rebuild(); return FReply::Handled(); }))
				]
			];

			// ═══ LE GRAPHE — l'assistance du mode NORMAL [GDD 2.2, 2.4] ═══
			// Il n'y a PAS de bouton « solveur » : une réponse en un clic serait
			// la « procédure prête à rejouer » que [GDD 2.4] interdit. Ce que
			// Normal accorde, ce sont des PRIMITIVES typées — un nœud, un appel
			// d'API — que le joueur assemble lui-même, à chaque analyse. En PRO,
			// rien : il calcule et saisit ses trois composantes.
			if (Se.jeu.agence.mode == fen::app::ModeAide::Normal)
			{
				const auto Res = fen::mission::evaluer_graphe(Se.graphe, Vue);
				Col->AddSlot().AutoHeight().Padding(0, 5, 0, 2)
				[ Txt(TEXT("GRAPHE — assemblez le calcul (mode NORMAL)"), 10.0f, ColTexteFaible, 90) ];

				for (int32 k = 0; k < (int32)Se.graphe.size(); ++k)
				{
					const fen::mission::NoeudDef& D2 = fen::mission::noeud_def(Se.graphe[k]);
					const bool bFautif = (Res.noeud_fautif == k);
					Col->AddSlot().AutoHeight().Padding(10, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[ SNew(SBox).WidthOverride(170.0f)
						  [ Txt(FString::Printf(TEXT("%d. %hs"), k + 1, D2.nom), 10.0f,
						        bFautif ? SRGB(242, 90, 80) : SRGB(140, 179, 255)) ] ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[ SNew(SBox).WidthOverride(230.0f)
						  [ Txt(FString::Printf(TEXT("%hs"), D2.appel), 9.0f, ColTexteFaible) ] ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[ Txt(FString::Printf(TEXT("-> %hs"),
						        fen::mission::type_nom(k < (int32)Res.sorties.size()
						                               ? Res.sorties[k] : fen::mission::TypeSignal::Aucun)),
						      9.0f, ColTexteFaible) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
						[ BoutonMini(TEXT("x"), FOnClicked::CreateLambda([this, k]() {
							Session->graphe.erase(Session->graphe.begin() + k);
							Rebuild(); return FReply::Handled(); })) ]
					];
				}
				if (!Res.valide)
				{
					Col->AddSlot().AutoHeight().Padding(10, 1)
					[ Txt(FString(Res.motif.c_str()), 9.0f, SRGB(242, 90, 80), 20) ];
				}

				// La PALETTE : les primitives disponibles. Chacune NOMME la
				// fonction d'API qu'elle est — c'est l'équivalence stricte de
				// [GDD 2.2], lisible à l'écran et pas seulement promise.
				// Deux rangees : huit primitives ne tiennent pas sur une ligne du
				// cadre, et un bouton dont le nom est tronque ne dit plus quelle
				// fonction il est — ce qui ruine justement l'equivalence [GDD 2.2].
				for (int32 Rangee = 0; Rangee < 2; ++Rangee)
				{
					TSharedRef<SHorizontalBox> Palette = SNew(SHorizontalBox);
					const auto& Dispo = fen::mission::noeuds_disponibles();
					for (int32 q = Rangee * 4; q < (Rangee + 1) * 4 && q < (int32)Dispo.size(); ++q)
					{
						const fen::mission::TypeNoeud T = Dispo[q].type;
						Palette->AddSlot().AutoWidth().Padding(2, 0)
						[ SNew(SBox).WidthOverride(150.0f)
						  [ BoutonAction(FText::FromString(FString::Printf(TEXT("%hs"), Dispo[q].nom)),
						                 FOnClicked::CreateLambda([this, T]() {
							                 Session->graphe.push_back(T);
							                 Rebuild(); return FReply::Handled(); })) ] ];
					}
					Col->AddSlot().AutoHeight().Padding(10, 1)[ Palette ];
				}

				if (Res.valide)
				{
					Col->AddSlot().AutoHeight().Padding(10, 2)
					[ BoutonAction(FText::FromString(FString::Printf(
					      TEXT("REPORTER LE RESULTAT : R %.1f  S %.1f  W %.1f"),
					      Res.dv_rsw.x, Res.dv_rsw.y, Res.dv_rsw.z)),
					      FOnClicked::CreateLambda([this, Res]() {
						      Session->tcm_commande[0] = Res.dv_rsw.x;
						      Session->tcm_commande[1] = Res.dv_rsw.y;
						      Session->tcm_commande[2] = Res.dv_rsw.z;
						      Rebuild(); return FReply::Handled(); })) ];
				}
			}
			else
			{
				// ═══ MODE PRO : LE CODE DÉCIDE [GDD 9.6, 15.1] ═══
				// « Le joueur ne pilote pas : il écrit à l'avance la logique qui
				// décidera à sa place, avec ses propres garde-fous. » Ici, le
				// logiciel téléversé s'exécute sur la solution de navigation
				// RÉELLE — dans son processus, avec son délai — et sa manœuvre
				// remplit les trois composantes ci-dessus. Il PROPOSE ; c'est
				// encore le joueur qui appuie sur EXECUTER.
				Col->AddSlot().AutoHeight().Padding(0, 5, 0, 2)
				[ Txt(TEXT("LOGICIEL DE VOL — mode PRO, aucun solveur fourni [GDD 2.2]"),
				      10.0f, ColTexteFaible, 90) ];
				if (!Se.source_vol_a_bord())
				{
					Col->AddSlot().AutoHeight().Padding(6, 1)
					[ Txt(Se.code_a_bord
					          ? TEXT("Le code a bord n est plus celui de l atelier. Rien d autre ne s executera.")
					          : TEXT("Aucun code televerse. Sans lui, la correction se saisit a la main."),
					      9.0f, SRGB(255, 190, 90), 20) ];
				}
				else
				{
					// EXÉCUTER HORS DU DOMAINE DE VALIDITÉ [GDD 15.5] : on ne
					// l'empêche pas — on le DIT. C'est la cause d'anomalie la plus
					// fréquente du logiciel de vol, et elle doit être un choix
					// éclairé, pas une surprise au débrief.
					if (Se.code_hors_domaine(*mc))
					{
						Col->AddSlot().AutoHeight().Padding(6, 1)
						[ Txt(FString::Printf(TEXT("HORS DOMAINE : qualifie 3s < %.0f km, la solution est a %.0f km"),
						                      Se.cert_vol.domain.input_hi / 1000.0,
						                      3.0 * Vue.sigma_r / 1000.0),
						      9.0f, SRGB(242, 90, 80), 20) ];
						Col->AddSlot().AutoHeight().Padding(6, 0)
						[ Txt(TEXT("Comportement NON COUVERT par le banc — anomalie legitime [GDD 15.5]."),
						      9.0f, ColTexteFaible, 20) ];
					}
					Col->AddSlot().AutoHeight().Padding(6, 2)
					[ BoutonAction(FText::FromString(TEXT("EXECUTER LE CODE DE VOL")),
					               FOnClicked::CreateLambda([this, mc]() {
						               Session->executer_code_vol(*mc);
						               Rebuild(); return FReply::Handled(); })) ];
					// CE QU'IL A DÉCIDÉ. Les quatre décisions, pas seulement le Δv :
					// différer et alerter sont des décisions, et ne rien exécuter
					// en est une aussi.
					if (Se.resultat_vol_lu)
					{
						const fen::code::DecisionsVol& Dec = Se.resultat_vol.decisions;
						Col->AddSlot().AutoHeight().Padding(6, 1)
						[ LigneKV(TEXT("LE CODE A DECIDE"),
						          !Se.resultat_vol.ok()
						              ? FString(UTF8_TO_TCHAR(fen::code::issue_nom(Se.resultat_vol.issue)))
						              : Dec.execute
						                  ? FString::Printf(TEXT("executer %.2f m/s"), fen::norm(Dec.dv))
						                  : FString::Printf(TEXT("ne rien executer (%d differee(s))"), Dec.differees),
						          !Se.resultat_vol.ok() ? SRGB(242, 90, 80)
						          : Dec.execute ? ColVert : SRGB(140, 179, 255)) ];
						for (const std::string& A : Dec.alertes)
						{
							Col->AddSlot().AutoHeight().Padding(6, 0)
							[ Txt(FString::Printf(TEXT("! %s"), UTF8_TO_TCHAR(A.c_str())),
							      9.0f, SRGB(255, 190, 90), 0) ];
						}
					}
				}
			}
		}
	}

	// --- LE PROGRAMME : ce que le joueur choisit (couche Program.hpp) ---------
	Se.evaluer_plan();
	fen::mission::MissionPlan& P = Se.mission_plan;
	const auto& progEngines = fen::mission::engines();
	const int ei = FMath::Clamp(P.program.engine_index, 0, (int)progEngines.size() - 1);

	// ON NE RECONÇOIT PAS UN VÉHICULE EN VOL. Les commandes de programme (moteur,
	// étages, essais, marge) et l'étude de navigation appartiennent à la
	// CONCEPTION : une fois le feu vert donné, le véhicule est parti et ces
	// boutons ne peuvent plus rien changer. Les masquer n'est donc pas un gain de
	// place, c'est la vérité de la phase — et ça laisse la place aux données de
	// VOL, qui elles ne servent qu'à ce moment-là. (Le cadre d'un poste CLIPPE le
	// texte, il ne le replie pas : piège n°42.)
	const bool bEnVol = (mc->state == St::Launched || mc->state == St::Debrief);
	if (!bEnVol)
	{
	Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[ Txt(TEXT("VEHICULE — concu au poste CONCEPTION [GDD 4.1]"), 10.0f, ColTexteFaible, 90) ];

	// ═══ LE VÉHICULE VIENT DU POSTE CONCEPTION — ON NE LE RECHOISIT PAS ICI ═══
	// [GDD 4.1, 12.2] Ce poste portait SES PROPRES sélecteurs de moteur et de
	// nombre d'étages, sur un catalogue séparé : le joueur choisissait un moteur
	// DEUX FOIS, dans deux postes, et seul celui-ci comptait. Depuis que la pile
	// conçue vole réellement, ces boutons ne décideraient plus rien — les laisser
	// serait pire qu'inutile, ce serait mentir sur qui décide. Le poste MONTRE
	// donc le véhicule, avec ce qu'il coûte et ce que l'arbre en dit.
	{
		static const TCHAR* Conf[] = { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") };
		double CoutPile = 0.0;
		for (std::size_t k = 0; k < P.pile.size(); ++k)
		{
			const fen::vehicle::EnginePart& EP = P.pile[k].engine_part();
			const fen::mission::EngineOption Opt = fen::mission::option_from_part(EP);
			const bool bQualifie = !P.moteurs_qualifies || P.moteurs_qualifies(Opt);
			CoutPile += fen::vehicle::effective_cost_musd(EP);
			Col->AddSlot().AutoHeight().Padding(6, 1, 6, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ Txt(FString::Printf(TEXT("E%d"), (int)k + 1), 10.0f, SRGB(255, 189, 87), 0) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[ Txt(ANSI_TO_TCHAR(EP.name), 10.0f,
				      bQualifie ? SRGB(140, 179, 255) : SRGB(242, 90, 80), 0) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ Txt(FString::Printf(TEXT("%.1f M$  (%s : %.1f-%.1f)"),
				      fen::vehicle::effective_cost_musd(EP), Conf[(int)EP.cost_confidence],
				      EP.cost_lo_musd, EP.cost_hi_musd), 9.0f, ColTexteFaible, 0) ]
			];
			if (!bQualifie)
			{
				Col->AddSlot().AutoHeight().Padding(24, 0, 6, 1)
				[ Txt(FString::Printf(TEXT("NON QUALIFIE : RECHERCHER %s [GDD 5.4]"),
				      ANSI_TO_TCHAR(EP.tech_id)), 9.0f, SRGB(242, 90, 80), 0) ];
			}
		}
		Col->AddSlot().AutoHeight().Padding(6, 2, 6, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[ Txt(FString::Printf(TEXT("%d etage(s), %.1f M$ de moteurs — concevoir au poste CONCEPTION"),
			      (int)P.pile.size(), CoutPile), 9.0f, ColTexteFaible, 0) ]
			// revue
			+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Right).VAlign(VAlign_Center)
			[ BoutonAction(FText::FromString(FString::Printf(TEXT("%s REVUE INDEP."),
			               P.program.review ? TEXT("[x]") : TEXT("[ ]"))),
			               FOnClicked::CreateLambda([this, &P]() {
				               P.program.review = !P.program.review; Rebuild(); return FReply::Handled(); })) ]
		];
	}
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
	];
	// MARGE DE CORRECTION — elle était AFFICHÉE sans être réglable. Tant que
	// `p_physics` valait 0,985 elle ne servait qu'à alourdir l'étage, et personne
	// ne la touchait ; depuis qu'elle COMMANDE la probabilité de navigation
	// [GDD 8.4], ne pas pouvoir la régler rendrait toute mission interplanétaire
	// impossible — un mécanisme correct et inatteignable est un mécanisme absent
	// (piège n°40).
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Txt(TEXT("MARGE CORRECTION"), 10.0f, ColTexte) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
		[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this, &P]() {
			P.program.dv_margin = FMath::Max(0.0, P.program.dv_margin - 25.0);
			Rebuild(); return FReply::Handled(); })) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ SNew(SBox).WidthOverride(70.0f)[ Txt(FString::Printf(TEXT("%.0f m/s"), P.program.dv_margin), 10.0f, SRGB(140, 179, 255), 0) ] ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this, &P]() {
			P.program.dv_margin += 25.0; Rebuild(); return FReply::Handled(); })) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
		[ Txt(TEXT("provisionnee en ergols"), 9.0f, ColTexteFaible, 30) ]
	];
	// ═══ LA TRAJECTOIRE : DIRECT OU ASSISTANCE GRAVITATIONNELLE ═══ [GDD 5.11]
	// « Navigation et opérations interplanétaires : ... ASSISTANCES », colonne
	// Senior → Directeur. C'est une décision d'architecte au même titre que la
	// marge : elle achète du Δv avec des ANNÉES. N'apparaît que si un tour mène
	// réellement là où va la mission — un bouton qui ne décide rien apprendrait
	// une fausse leçon (piège n°40).
	//
	// LE CALCUL A LIEU AU CLIC, ET NULLE PART AILLEURS : résoudre les époques
	// d'un tour prend 0,5 à 1,8 s. C'est un calcul de bureau d'études, pas un
	// rafraîchissement d'écran — `evaluer_plan` ne fait que LIRE son résultat.
	{
		const std::vector<const fen::mission::TourType*> Tours = Session->tours_offerts(*mc);
		if (!Tours.empty())
		{
			TSharedRef<SHorizontalBox> Ligne = SNew(SHorizontalBox);
			Ligne->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[ Txt(TEXT("TRAJECTOIRE"), 10.0f, ColTexte) ];
			const bool bDirect = mc->tour_id.empty();
			Ligne->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
			[ BoutonAction(FText::FromString(bDirect ? TEXT("[x] DIRECT") : TEXT("[ ] DIRECT")),
			               FOnClicked::CreateLambda([this]() {
				               Session->choisir_tour(std::string()); Rebuild(); return FReply::Handled(); })) ];
			for (const fen::mission::TourType* T : Tours)
			{
				const std::string Id = T->id;
				const bool bPris = (mc->tour_id == Id);
				Ligne->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
				[ BoutonAction(FText::FromString(FString::Printf(TEXT("%s %s"),
				               bPris ? TEXT("[x]") : TEXT("[ ]"), UTF8_TO_TCHAR(Id.c_str()))),
				               FOnClicked::CreateLambda([this, Id]() {
					               Session->choisir_tour(Id); Rebuild(); return FReply::Handled(); })) ];
			}
			Col->AddSlot().AutoHeight().Padding(6, 1)[ Ligne ];
			// LE TROC, CHIFFRÉ DES DEUX CÔTÉS — sans quoi le bouton demanderait au
			// joueur de choisir à l'aveugle. Le chiffre d'abord, le renvoi au GDD
			// à la fin : le cadre du poste TRONQUE vers 72 caractères.
			FString Ligne2;
			if (Session->tour_bilan_valide(*mc))
			{
				const auto& B = Session->tour_bilan;
				Ligne2 = FString::Printf(
					TEXT("%s : %.0f m/s en %.1f ans (%d survol(s), DSM %.0f m/s) [5.11]"),
					UTF8_TO_TCHAR(mc->tour_id.c_str()), B.dv_total_ms, B.tof_ans,
					(int)B.rp_survol_m.size(), B.dv_bord_ms);
			}
			else if (!Session->tour_bilan.cause.empty())
			{
				Ligne2 = FString::Printf(TEXT("refuse : %s"),
				                         UTF8_TO_TCHAR(Session->tour_bilan.cause.c_str()));
			}
			else
			{
				Ligne2 = FString::Printf(TEXT("direct : %.0f m/s en %.0f j — un tour echange du Dv contre des annees"),
				                         P.dv_traj_override,
				                         Session->duree_transit_jours(*mc));
			}
			Col->AddSlot().AutoHeight().Padding(6, 0)
			[ Txt(Ligne2, 9.0f,
			      Session->tour_bilan_valide(*mc) ? SRGB(140, 179, 255)
			                                      : (Session->tour_bilan.cause.empty()
			                                             ? ColTexteFaible : SRGB(255, 190, 90)), 0) ];
			// ═══ CE QUE LE SURVOL EXIGE ═══ [GDD 8.4] — le corridor en paramètre
			// d'impact, la dernière correction et ce qu'elle laisse. C'est la
			// SECONDE condition de navigation d'un tour, et elle est nommée : un
			// chiffre sans cause ne se corrige pas.
			// ═══ ET CE QUE L'ARCHITECTE EXIGE DU SURVOL ═══ [GDD 3.1, 8.5]
			// L'optimiseur colle toujours le périastre à sa borne basse : cette
			// borne EST la décision. Viser plus haut élargit le corridor et se paie
			// en Δv. Chaque clic REFAIT le tour (une à six secondes) — c'est un
			// calcul de bureau d'études, pas un curseur d'affichage.
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ Txt(TEXT("ALTITUDE DE SURVOL"), 10.0f, ColTexte) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
				[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this]() {
					Session->alt_survol_min_km = FMath::Max(0.0, Session->alt_survol_min_km - 250.0);
					if (!Session->mission_courante()->tour_id.empty())
						Session->choisir_tour(Session->mission_courante()->tour_id);
					Rebuild(); return FReply::Handled(); })) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(SBox).WidthOverride(80.0f)
				  [ Txt(Session->alt_survol_min_km > 0.0
				            ? FString::Printf(TEXT("%.0f km"), Session->alt_survol_min_km)
				            : FString(TEXT("vol reel")),
				        10.0f, SRGB(140, 179, 255), 0) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this]() {
					Session->alt_survol_min_km += 250.0;
					if (!Session->mission_courante()->tour_id.empty())
						Session->choisir_tour(Session->mission_courante()->tour_id);
					Rebuild(); return FReply::Handled(); })) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[ Txt(TEXT("plus haut = plus sur, plus cher"), 9.0f, ColTexteFaible, 34) ]
			];
			if (Session->nav_survol_.ok)
			{
				const auto& Sv = Session->nav_survol_;
				Col->AddSlot().AutoHeight().Padding(6, 0)
				[ Txt(FString::Printf(
					TEXT("survol : corridor %.0f km, residu %.0f km apres TCM finale de %.0f m/s"
					     " -> P %.3f [8.4]"),
					Sv.demi_corridor_m / 1000.0, Sv.sigma_b_m / 1000.0,
					Sv.dv_derniere_corr, Sv.p_survol),
				      9.0f,
				      Sv.p_survol > 0.95 ? SRGB(120, 220, 140)
				                         : (Sv.p_survol > 0.7 ? SRGB(255, 190, 90)
				                                              : SRGB(255, 110, 110)), 0) ];
			}
		}
	}
	// ═══ BLINDAGE — L'ARBITRAGE MASSE / PROTECTION / MISSION ═══ [GDD 6.6]
	// Seulement pour un vol HABITÉ : il n'y a pas d'équipage à protéger ailleurs,
	// et un curseur qui ne sert à rien apprend une fausse leçon. `env/Radiation`
	// existait depuis toujours sans aucun consommateur — [GDD 7.7] déclare
	// l'environnement « acteur de mission », il n'était que décor.
	//
	// LA LEÇON EST DANS LES CHIFFRES, pas dans un texte : doubler la masse au
	// décollage n'achète qu'une dizaine de pour cent de dose en moins, parce que
	// le GCR ne se blinde quasiment pas (secondaires de spallation). Le joueur
	// doit pouvoir le CONSTATER — d'où l'affichage de la masse à côté du réglage.
	if (mc->contract.crewed)
	{
		const int NCrew = mc->contract.terms.crew_required;   // l'objectif d'ARES
		// ═══ COMBIEN DE VOLUME PAR PERSONNE ═══ [GDD 3.1] — la première décision
		// d'architecte, et la plus lourde de conséquences. « ARES dit : on doit
		// aller là pour faire ça » ; c'est ICI qu'on décide comment loger un
		// équipage. Serrer l'habitat allège la coque ET la surface à blinder —
		// mesuré : 25 → 15 m³/personne fait passer une croisière martienne de
		// 131 t à 100 t au décollage. Ce n'est plus un forfait du contrat.
		// N'apparaît que si le véhicule EST le domicile : un vol LEO s'amarre à
		// une station, il n'emporte pas sa maison.
		if (P.crew_round_trip_days > 0.0)
		{
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Txt(TEXT("VOLUME PAR PERSONNE"), 10.0f, ColTexte) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
				[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this, &P]() {
					P.volume_par_personne_m3 = FMath::Max(10.0, P.volume_par_personne_m3 - 5.0);
					Session->evaluer_plan(); Rebuild(); return FReply::Handled(); })) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(SBox).WidthOverride(70.0f)[ Txt(FString::Printf(TEXT("%.0f m3"), P.volume_par_personne_m3), 10.0f, SRGB(140, 179, 255), 0) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this, &P]() {
					P.volume_par_personne_m3 += 5.0;
					Session->evaluer_plan(); Rebuild(); return FReply::Handled(); })) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[ Txt(FString::Printf(TEXT("habitat %.1f t  (%d a bord)"),
				                      P.masse_habitat_kg_ / 1000.0, NCrew), 9.0f, ColTexteFaible, 30) ]
			];
		}
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Txt(TEXT("BLINDAGE EQUIPAGE"), 10.0f, ColTexte) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
			[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this, &P]() {
				P.blindage.areal_density_gcm2 = FMath::Max(0.0, P.blindage.areal_density_gcm2 - 2.5);
				Rebuild(); return FReply::Handled(); })) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(SBox).WidthOverride(70.0f)[ Txt(FString::Printf(TEXT("%.1f g/cm2"), P.blindage.areal_density_gcm2), 10.0f, SRGB(140, 179, 255), 0) ] ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this, &P]() {
				P.blindage.areal_density_gcm2 += 2.5; Rebuild(); return FReply::Handled(); })) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
			[ Txt(FString::Printf(TEXT("%.1f t sur %.0f m2"),
			                      fen::mission::masse_blindage_kg(NCrew, P.blindage.areal_density_gcm2,
			                                                      P.volume_par_personne_m3) / 1000.0,
			                      fen::mission::surface_habitat_m2(NCrew, P.volume_par_personne_m3)),
			      9.0f, ColTexteFaible, 30) ]
		];
		// LA DOSE QUE CE CHOIX PROMET, sur la durée réelle de la mission. C'est
		// le seul chiffre qui rende le curseur ci-dessus décidable — sinon le
		// joueur achèterait des tonnes à l'aveugle.
		{
			const double Jours = fen::mission::crew_occupation_days(
				mc->contract.family, P.crew_round_trip_days);
			const double Sv = fen::mission::dose_chronique_sv(
				Jours, fen::mission::FlightPhase::TransferCruise, P.blindage, 0.0);
			const bool Depasse = Sv >= fen::env::CAREER_DOSE_LIMIT_SV;
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("DOSE PREVUE (pire cas)"),
			          FString::Printf(TEXT("%.2f Sv  /  %.1f Sv de carriere"),
			                          Sv, fen::env::CAREER_DOSE_LIMIT_SV),
			          Depasse ? SRGB(242, 90, 80) : ColVert) ];
		}
	}

	// POURSUITE — même histoire que la marge : `tracking_days` était acheté et
	// facturé depuis le premier jour SANS RIEN FAIRE, donc sans bouton. Depuis
	// qu'il commande la qualité de la solution de navigation [GDD 8.6], c'est le
	// second levier du joueur sur son vol — et sans lui, la correction se calcule
	// sur un état faux (en-tête de nav/Tracking.hpp).
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Txt(TEXT("POURSUITE"), 10.0f, ColTexte) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
		[ BoutonMini(TEXT("-"), FOnClicked::CreateLambda([this, &P]() {
			P.program.tracking_days = FMath::Max(0.0, P.program.tracking_days - 2.0);
			P.program.tracking_musd = 0.12 * P.program.tracking_days;
			Rebuild(); return FReply::Handled(); })) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ SNew(SBox).WidthOverride(70.0f)[ Txt(FString::Printf(TEXT("%.0f j"), P.program.tracking_days), 10.0f, SRGB(140, 179, 255), 0) ] ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ BoutonMini(TEXT("+"), FOnClicked::CreateLambda([this, &P]() {
			P.program.tracking_days += 2.0;
			// Le prix d'une passe : `nav::Station::cost_musd_per_hour` x 8 h/jour,
			// pris sur le catalogue réel du DSN — pas un tarif inventé ici.
			P.program.tracking_musd = 0.12 * P.program.tracking_days;
			Rebuild(); return FReply::Handled(); })) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
		[ Txt(FString::Printf(TEXT("%.1f M EUR de passes DSN"), P.program.tracking_musd), 9.0f, ColTexteFaible, 30) ]
	];

	// --- LA NAVIGATION : ce que P(succes) doit a la trajectoire [GDD 8.4] ----
	// Sans ces trois lignes, la marge se reglerait a l'aveugle : le joueur doit
	// voir CE QU IL FAUT couvrir, et d'ou ca vient. C'est le chiffre du 99e
	// centile qu'il provisionne — comme un vrai bureau d'etudes.
	if (Se.nav_disp.ok)
	{
		const fen::mission::NavDispersion& N = Se.nav_disp;
		Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
		[ Txt(TEXT("NAVIGATION — dispersion d'injection"), 10.0f, ColTexteFaible, 90) ];
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("INJECTION"), FString::Printf(TEXT("%.0f m/s  (+/- %.1f, Oberth x%.1f)"),
		          N.dv_injection, N.sigma_dv_inj, N.oberth_gain)) ];
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("MANQUE AU BUT 1s"), FString::Printf(TEXT("%.0f km sans correction"),
		          N.sigma_r_arr_km), SRGB(255, 190, 90)) ];
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("CORRECTION A PREVOIR"), FString::Printf(TEXT("%.0f m/s (99e centile, TCM a J+%.0f)"),
		          N.dv_corr_p99, N.t_tcm_days),
		          P.program.dv_margin >= N.dv_corr_p99 ? ColVert : SRGB(242, 90, 80)) ];
	}
	}   // fin du bloc CONCEPTION (voir bEnVol)

	// --- LE BILAN (assess) : le tableau de viabilité -------------------------
	// PENDANT UNE MANŒUVRE CRITIQUE, l'ecran est une CONSOLE DE VOL : la fiche de
	// viabilite n'y a pas plus sa place que les commandes de conception (piege
	// n°65). Le joueur pilote, il n'evalue pas un programme.
	const bool bConsole = bEnVol && Se.vue_vol(*mc).ok &&
	                      fen::mission::is_critical_phase(fen::mission::flight_phase_of(
	                          *mc, Se.jeu.ares.initialisee() ? Se.jeu.ares.etat->clock.now_days() : 0.0));
	const fen::mission::Assessment& A = P.assessment;
	// ═══ UNE FOIS PARTI, LE BILAN EST DE L'HISTOIRE ═══ [GDD 4.1]
	// Même raisonnement que le mode console ci-dessus, étendu à TOUT le vol : la
	// viabilité est une décision de CONCEPTION, gelée au feu vert. En vol, aucune
	// de ses sept lignes n'est actionnable — alors que la télémétrie vitale, elle,
	// l'est à chaque instant. Les laisser occuper le haut du poste poussait
	// « VIE À BORD » sous la ligne de flottaison du défilement : dose, boucles et
	// écart d'horloge n'étaient plus contrôlables par aucune capture, et c'est
	// exactement la condition dans laquelle le piège n°74 (postes figés) avait
	// prospéré pendant des semaines. On garde donc UNE ligne — ce qu'on a
	// emporté et sous quel verdict — et on rend la place à ce qui vit.
	const bool bBilanReduit = bEnVol && !bConsole && P.evaluated;
	if (bBilanReduit)
	{
		Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
		[ LigneKV(TEXT("ARCHITECTURE EMPORTEE"),
		          A.assemblage.n_lancements > 1
		              ? FString::Printf(TEXT("%.0f t en %d lancements - %s"),
		                                A.m0_kg / 1000.0, A.assemblage.n_lancements,
		                                A.ok ? TEXT("viable au depart") : *FString(A.why.c_str()))
		              : FString::Printf(TEXT("%.0f t en un lancement - %s"),
		                                A.m0_kg / 1000.0,
		                                A.ok ? TEXT("viable au depart") : *FString(A.why.c_str())),
		          ColTexteFaible) ];
	}
	if (!bConsole && !bBilanReduit) {
	Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[ Txt(TEXT("BILAN DE VIABILITE"), 10.0f, ColTexteFaible, 90) ];
	// ═══ « PAS ENCORE ÉVALUÉ » N'EST PAS « RATÉ SUR LES QUATRE AXES » ═══
	// TROUVÉ EN AUDITANT LA CAPTURE, une fois les termes du contrat réparés.
	// `Assessment` naît avec ses quatre `fits_*` à false et ses chiffres à zéro ;
	// le poste les affichait tels quels, si bien qu'une mission fraîchement
	// acceptée — donc AVANT tout passage au poste CONCEPTION, ce qui est l'ordre
	// normal de la boucle [GDD 4.1] — s'annonçait « COUT 0 / 1200 M EUR,
	// P(SUCCES) 0,0 %, VERROU : MASSE BUDGET CALENDRIER RISQUE ». Le joueur lit
	// une catastrophe là où il n'y a qu'un travail pas encore fait.
	// `MissionPlan::evaluated` existait exactement pour ça, et PERSONNE ne le
	// lisait — même famille de défaut que les modèles sans consommateur.
	if (!P.evaluated)
	{
		Col->AddSlot().AutoHeight().Padding(6, 2)
		[ Txt(TEXT("CONCEPTION NON EVALUEE"), 10.0f, SRGB(255, 190, 90), 40) ];
		Col->AddSlot().AutoHeight().Padding(6, 0, 0, 3)
		[ Txt(TEXT("Choisir moteur, lanceur et marges au poste CONCEPTION : le bilan s'ecrit la."),
		      9.0f, ColTexteFaible, 20) ];
	}
	else {
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[ LigneKV(TEXT("MASSE AU DECOLLAGE"), FString::Printf(TEXT("%.0f kg"), A.m0_kg),
	          A.fits_mass ? ColVert : SRGB(242, 90, 80)) ];
	// ═══ ON N'AFFICHE PAS UN ZÉRO QU'ON N'A JAMAIS CALCULÉ ═══
	// Quand la masse bloque, `assess` s'arrête là : coût, calendrier et
	// fiabilité ne SONT PAS des échecs, ils sont inconnus. Les peindre en rouge
	// à zéro faisait passer une seule cause pour quatre, et cachait la seule
	// information utile — laquelle des deux masses reprendre.
	if (A.fits_mass) {
	// ═══ LA CAMPAGNE DE MISE EN ORBITE [GDD 5.2 branche 1] ═══
	// N'apparaît QUE si la masse a réellement demandé un assemblage : afficher
	// « 1 lancement » sur chaque vol serait du bruit. Quand elle apparaît, elle
	// dit les TROIS choses que l'assemblage coûte — des tirs, du temps, et de la
	// fiabilité — parce que c'est un arbitrage et non un contournement.
	if (A.assemblage.n_lancements > 1)
	{
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("ASSEMBLAGE EN ORBITE"),
		          FString::Printf(TEXT("%d lancements sur %.0f jours"),
		                          A.assemblage.n_lancements, A.assemblage.duree_jours),
		          SRGB(255, 190, 90)) ];
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("SEGMENT DE MISE EN ORBITE"),
		          FString::Printf(TEXT("P = %.3f  (%.1f t d'ergols evapores)"),
		                          A.p_launcher, A.assemblage.ergols_evapores_kg / 1000.0),
		          A.p_launcher >= 0.95 ? ColVert : SRGB(255, 190, 90)) ];
	}
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[ LigneKV(TEXT("COUT PROGRAMME"), FString::Printf(TEXT("%.0f / %.0f M EUR"), A.cost_total, mc->contract.terms.budget_musd),
	          A.fits_budget ? ColVert : SRGB(242, 90, 80)) ];
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[ LigneKV(TEXT("CALENDRIER"), FString::Printf(TEXT("%.0f / %.0f mois"), A.schedule_months, mc->contract.terms.deadline_months),
	          A.fits_schedule ? ColVert : SRGB(242, 90, 80)) ];
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[ LigneKV(TEXT("P(SUCCES)"), FString::Printf(TEXT("%.1f %% / %.0f %% exige"), 100.0 * A.p_success, 100.0 * mc->contract.terms.min_success_prob),
	          A.fits_risk ? ColVert : SRGB(242, 90, 80)) ];
	// ═══ CE QUE LES SOUS-SYSTÈMES AVANCÉS PRÉLÈVENT ═══ [GDD 12.4]
	// « Souvent DIMENSIONNANTE » : sur un aller-retour, le vieillissement du cœur
	// pèse plus que le moteur lui-même. La ligne n'apparaît que si l'architecture
	// en porte — sur un chimique elle vaudrait 100 %, donc du bruit — et elle
	// NOMME la cause, parce qu'un chiffre sans cause n'est pas actionnable.
	if (A.p_filieres < 0.9999)
	{
		Col->AddSlot().AutoHeight().Padding(6, 1)
		[ LigneKV(TEXT("SOUS-SYSTEMES AVANCES"),
		          FString::Printf(TEXT("-%.1f %%  .  %s"), 100.0 * (1.0 - A.p_filieres),
		                          UTF8_TO_TCHAR(A.cause_filieres.c_str())),
		          A.p_filieres > 0.9 ? SRGB(255, 189, 87) : SRGB(242, 90, 80)) ];
	}
	} else {
	Col->AddSlot().AutoHeight().Padding(6, 1)
	[ Txt(TEXT("cout, calendrier et fiabilite NON EVALUES : l'etude s'arrete a la masse"),
	      9.0f, ColTexteFaible, 20) ];
	}
	Col->AddSlot().AutoHeight().Padding(6, 3)
	[ Txt(A.ok ? FString(TEXT("PROGRAMME VIABLE")) : (FString(TEXT("VERROU : ")) + FString(A.why.c_str())),
	      10.0f, A.ok ? ColVert : SRGB(242, 90, 80), 40) ];
	}   // fin du bilan CHIFFRÉ (plan évalué)

	}   // fin du BILAN (masque en console de vol)

	// --- L'ISSUE (au débrief) ------------------------------------------------
	if (mc->state == St::Debrief && Se.mission_outcome_pret)
	{
		Col->AddSlot().AutoHeight().Padding(0, 4, 0, 1)
		[ Txt(Se.mission_outcome.success ? TEXT("VOL REUSSI") : TEXT("VOL ECHOUE"),
		      12.0f, Se.mission_outcome.success ? ColVert : SRGB(242, 90, 80), 90) ];
		Col->AddSlot().AutoHeight().Padding(6, 0)
		[ Txt(FString(Se.mission_outcome.cause.c_str()), 10.0f, ColTexteFaible, 30) ];
		// LE DÉBRIEF DIT CE QUI S'EST RÉELLEMENT PASSÉ [GDD 10.4]. L'écart
		// d'injection était invisible pendant tout le vol [GDD 7.5] : c'est ici,
		// et seulement ici, que le joueur apprend le chiffre — et qu'il peut
		// juger sa marge sur autre chose qu'une statistique.
		if (mc->nav_evaluee)
		{
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("MANQUE AU BUT REEL"), FString::Printf(TEXT("%.0f km"), mc->nav_miss_km)) ];
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("CORRECTION DEPENSEE"),
			          FString::Printf(TEXT("%.0f m/s  /  %.0f m/s provisionnes"),
			                          mc->nav_dv_required, P.program.dv_margin),
			          mc->nav_dv_required <= P.program.dv_margin ? ColVert : SRGB(242, 90, 80)) ];
			// QUI A TENU LES RENDEZ-VOUS. Sans cette ligne, un vol perdu faute
			// d'avoir corrige ne se lit que comme un manque au but inexplique —
			// et le joueur n'apprend rien de ce qui l'a coute (piege n°42).
			{
				const TCHAR* Qui = TEXT("PERSONNE - aucun rendez-vous tenu");
				FLinearColor ColQui = SRGB(242, 90, 80);
				switch (mc->vol_conduit_par)
				{
					case 1: Qui = TEXT("VOUS, depuis le terminal");       ColQui = ColVert;          break;
					case 2: Qui = TEXT("LE LOGICIEL DE BORD");            ColQui = ColVert;          break;
					case 3: Qui = TEXT("L'ADJOINT, en votre absence");    ColQui = SRGB(255, 190, 90); break;
					default: break;
				}
				Col->AddSlot().AutoHeight().Padding(6, 1)
				[ LigneKV(TEXT("CORRECTIONS CONDUITES PAR"), Qui, ColQui) ];
			}
		}
	}

	// ═══ LA MISSION VÉCUE [GDD 9, décision 18] ═══
	// « Vol habité vécu INCLUS. » Le modèle savait tout faire — provisionner les
	// vivres, geler l'agence, écrire la reconstitution d'absence — et rien ne
	// menait à cet état : `try_embark` n'avait aucun appelant. C'est ici que le
	// joueur monte à bord, et c'est ici qu'il voit ce qu'il lui reste à respirer.
	if (Se.jeu.ares.initialisee())
	{
		fen::game::GameState& GV = *Se.jeu.ares.etat;

		if (GV.lived.active && GV.lived.mission_id == mc->contract.id)
		{
			Col->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
			[ Txt(TEXT("VIE A BORD"), 12.0f, SRGB(120, 210, 255)) ];

			const double Jours = GV.lived.days_left();
			// LE SEUL CHIFFRE QUI COMPTE VRAIMENT [GDD 9.1] : combien de temps
			// l'équipage tient au rythme courant. Rouge quand il descend sous le
			// délai d'un retour — l'ordre de grandeur d'un transit, pas un seuil
			// d'affichage.
			const bool Critique = Jours < 30.0;
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("AUTONOMIE RESTANTE"),
			          FString::Printf(TEXT("%.1f jours  (%d a bord)"), Jours, GV.lived.n_crew),
			          Critique ? SRGB(242, 90, 80) : ColVert) ];
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("OXYGENE / EAU"),
			          FString::Printf(TEXT("%.0f kg  /  %.0f kg"),
			                          GV.lived.vitals.o2_kg, GV.lived.vitals.water_kg)) ];
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("VIVRES / EPURATION CO2"),
			          FString::Printf(TEXT("%.0f kg  /  %.0f kg"),
			                          GV.lived.vitals.food_kg,
			                          GV.lived.vitals.co2_scrub_capacity_kg)) ];
			// LE RECYCLAGE EST UN FAIT DU VOL, pas une case de menu : il vient de
			// la branche 4 au moment de l'embarquement [GDD 5.10].
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("BOUCLES DE RECYCLAGE"),
			          FString::Printf(TEXT("eau %.0f %%   O2 %.0f %%"),
			                          GV.lived.loops.water_recovery * 100.0,
			                          GV.lived.loops.o2_recovery * 100.0)) ];
			// ═══ LE SECOND CONSOMMABLE, CELUI QUI NE SE RECHARGE PAS ═══ [GDD 6.6]
			// Les vivres se comptent en jours restants ; la dose, elle, se compte
			// en fraction de carrière DÉFINITIVEMENT dépensée. Les deux lignes se
			// suivent exprès : c'est le même équipage, et ce sont ses deux
			// horloges.
			{
				const auto& D = GV.dose_architecte;
				const double Frac = D.career_sv / fen::env::CAREER_DOSE_LIMIT_SV;
				Col->AddSlot().AutoHeight().Padding(6, 1)
				[ LigneKV(TEXT("DOSE MISSION"),
				          FString::Printf(TEXT("%.3f Sv  (blindage %.1f g/cm2)"),
				                          D.mission_sv, GV.lived.blindage.areal_density_gcm2)) ];
				Col->AddSlot().AutoHeight().Padding(6, 1)
				[ LigneKV(TEXT("DOSE DE CARRIERE"),
				          FString::Printf(TEXT("%.3f Sv  /  %.1f Sv  (%.0f %%)"),
				                          D.career_sv, fen::env::CAREER_DOSE_LIMIT_SV, Frac * 100.0),
				          Frac >= 1.0 ? SRGB(242, 90, 80)
				                      : (Frac > 0.75 ? SRGB(255, 190, 90) : ColVert)) ];
			}

			// ═══ ET LA TROISIÈME HORLOGE, CELLE QUI NE BAT PAS COMME LES AUTRES ═══
			// [GDD 6.7, 14.4] « Le vieillissement différentiel pèse sur la carrière
			// et la passation. » Il valait exactement zéro jusqu'ici : `DualClock`
			// n'était avancé nulle part. Affiché en MILLISECONDES parce que c'est
			// l'ordre de grandeur réel d'une croisière interplanétaire — et c'est
			// tout l'intérêt de le montrer : le joueur constate de lui-même que le
			// cliché relativiste ne s'applique pas à son vol, au lieu qu'on le lui
			// affirme. Le SIGNE est la vraie information : en croisière l'équipage
			// vieillit PLUS vite (le potentiel solaire l'emporte sur la vitesse), en
			// orbite basse il vieillit MOINS.
			{
				// `aging_gap` = Terre − bord : positif = le bord a pris du retard.
				const double EcartMs = -GV.dual_clock.aging_gap() * 1000.0;
				const FString Sens = FMath::Abs(EcartMs) < 1.0e-3
					? TEXT("aucun ecart mesurable")
					: (EcartMs > 0.0 ? TEXT("l'equipage vieillit PLUS vite")
					                 : TEXT("l'equipage vieillit MOINS vite"));
				Col->AddSlot().AutoHeight().Padding(6, 1)
				[ LigneKV(TEXT("HORLOGE DE BORD"),
				          FString::Printf(TEXT("%+.1f ms  -  %s"), EcartMs, *Sens),
				          SRGB(150, 190, 230)) ];
			}

			// CE QUE DEVIENT ARES PENDANT CE TEMPS [GDD 9.3] — sans cette ligne,
			// le joueur croirait son agence à l'abandon.
			Col->AddSlot().AutoHeight().Padding(6, 1)
			[ LigneKV(TEXT("ARES"),
			          FString::Printf(TEXT("sous l'adjoint - confiance gelee a %.0f"),
			                          GV.lived.confidence_frozen),
			          SRGB(255, 190, 90)) ];
		}
	}

	// ═══ LE CADRE CLIPPE, IL NE REPLIE PAS (piège n°42) ═══
	// La conduite de mission a fini par porter plus de lignes qu'un poste n'en
	// affiche : bilan, navigation, vie à bord, dose, avaries. Trouvé EN CAPTURE —
	// « ARES » chevauchait « [ECHAP] FERMER » et les boutons sortaient du cadre.
	// Même remède que les postes AGENCE et PLANIFICATION, qui portaient déjà des
	// listes longues : la LECTURE défile, les ACTIONS restent ancrées en bas. Un
	// bouton qu'il faut aller chercher en défilant est un bouton qu'on ne trouve
	// pas.
	TSharedRef<SVerticalBox> Bas = SNew(SVerticalBox);

	// --- EMBARQUER / DEBARQUER [GDD 9.2] -------------------------------------
	// Proposé seulement quand il y a un vol habité à rejoindre : un bouton qui
	// refuse toujours n'apprend rien. Quand il refuse, il DIT pourquoi (piège
	// n°42) — c'est ce qui rend lisibles les conditions de 9.2 (rang terminal,
	// maturité de support-vie, confiance) au lieu de les laisser deviner.
	if (Se.jeu.ares.initialisee() && mc->contract.crewed)
	{
		fen::game::GameState& GV = *Se.jeu.ares.etat;
		const bool ABord = GV.lived.active && GV.lived.mission_id == mc->contract.id;
		if (ABord)
		{
			Bas->AddSlot().AutoHeight().Padding(0, 2, 0, 1)
			[ BoutonAction(FText::FromString(TEXT("DEBARQUER : reprendre le poste au sol")),
			               FOnClicked::CreateLambda([this]() {
				               Session->debarquer();
				               Rebuild(); return FReply::Handled();
			               }), true) ];
		}
		else if (!GV.lived.active)
		{
			const auto V = Se.peut_embarquer();
			Bas->AddSlot().AutoHeight().Padding(0, 2, 0, 1)
			[ BoutonAction(FText::FromString(TEXT("EMBARQUER : vivre cette mission")),
			               FOnClicked::CreateLambda([this]() {
				               Session->embarquer();
				               Rebuild(); return FReply::Handled();
			               }), V.possible) ];
			if (!V.possible && !V.raison.empty())
			{
				Bas->AddSlot().AutoHeight().Padding(6, 0, 0, 4)
				[ Txt(FString(V.raison.c_str()), 10.0f, SRGB(242, 90, 80), 30) ];
			}
		}
	}

	// --- LE BOUTON D'AVANCE : franchit un gate, ou dit pourquoi il refuse -----
	{
		const TCHAR* lib =
			mc->state == St::Prerequisites ? TEXT("PASSER EN CONCEPTION")
			: mc->state == St::Design ? TEXT("VALIDER LA CONCEPTION")
			: mc->state == St::WindowSearch ? TEXT("RETENIR LA FENETRE")
			: mc->state == St::Qualification ? TEXT("FEU VERT : ENGAGER (paye le programme)")
			: mc->state == St::Launched ? TEXT("CLORE LE VOL ET DEBRIEFER")
			: mc->state == St::Debrief ? TEXT("CLORE LE DEBRIEF")
			: TEXT("MISSION CLOSE");
		const bool closable = mc->state != St::Completed && mc->state != St::Failed &&
		                      mc->state != St::Aborted;
		Bas->AddSlot().AutoHeight().Padding(0, 2, 0, 1)
		[ BoutonAction(FText::FromString(lib),
		               FOnClicked::CreateLambda([this]() {
			               Session->avancer_mission();
			               Rebuild(); return FReply::Handled();
		               }), closable) ];
		// LE MOTIF DU REFUS. Il était calculé et jeté : le bouton refusait en
		// silence, ce qui se lit comme une panne (piège n°42). Deux lignes
		// courtes plutôt qu'une longue — le cadre du poste CLIPPE, il ne replie pas.
		if (!Session->dernier_refus_mission.empty())
		{
			Bas->AddSlot().AutoHeight().Padding(6, 0, 0, 4)
			[ Txt(FString(Session->dernier_refus_mission.c_str()), 10.0f, SRGB(242, 90, 80), 30) ];
		}
	}

	// LA LECTURE DÉFILE, LES ACTIONS RESTENT : c'est ce qui permet au poste de
	// porter une mission longue sans que rien ne déborde ni ne se dérobe.
	return FramePoste(D,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[ SNew(SScrollBox) + SScrollBox::Slot()[ Col ] ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ Bas ]);
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

			// ═══ LE CARNET [GDD 15.4] ═══
			// « Documentation personnelle du personnage […] transmis en
			// passation. » Il est chez l'Architecte, à son poste — pas ailleurs.
			// Il était sérialisé et transmis au successeur depuis le premier
			// jour, et VIDE : personne n'y écrivait, personne ne le lisait.
			// ═══ LE PASSAGE EN MODE PRO [GDD 2.3] ═══
			// « Possible et UNIDIRECTIONNEL ; les graphes existants sont
			// archivés en lecture seule dans le carnet. » Le mode ne s'écrivait
			// qu'à la création d'une partie : la bascule n'existait pas. Le prix
			// est annoncé AVANT le clic (piège n°64) — c'est une perte voulue,
			// pas une surprise.
			if (A.mode == fen::app::ModeAide::Normal)
			{
				Col->AddSlot().AutoHeight().Padding(0, 8, 0, 1)
				[ Txt(FString::Printf(TEXT("MODE NORMAL — passer en PRO archive vos %d noeud(s) et les efface"),
				                      (int32)Session->graphe.size()), 9.0f, SRGB(255, 190, 90), 80) ];
				Col->AddSlot().AutoHeight().Padding(6, 1)
				[ BoutonAction(FText::FromString(TEXT("PASSER EN MODE PRO (irreversible)")),
				               FOnClicked::CreateLambda([this]() {
					               Session->basculer_en_pro();
					               Rebuild(); return FReply::Handled(); })) ];
			}
			else
			{
				Col->AddSlot().AutoHeight().Padding(0, 8, 0, 1)
				[ LigneKV(TEXT("MODE D AIDE"), TEXT("PRO — le calcul est a vous [GDD 2.2]"),
				          SRGB(255, 190, 90)) ];
			}

			const auto& Pages = J.ares.etat->notebook.entries;
			Col->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
			[ Txt(FString::Printf(TEXT("CARNET — %d page(s), transmis en passation [GDD 15.4]"),
			                      (int32)Pages.size()), 10.0f, ColTexteFaible, 90) ];
			if (Pages.empty())
			{
				Col->AddSlot().AutoHeight().Padding(6, 0)
				[ Txt(TEXT("(vide — il se remplira au premier debrief)"), 9.0f, ColTexteFaible) ];
			}
			// LES PLUS RÉCENTES D'ABORD, et seulement quelques-unes : le cadre
			// d'un poste CLIPPE (piège n°42), et une carrière en écrit des
			// dizaines. Le corps est tronqué à ses premières lignes — le carnet
			// est ici une CONSULTATION, pas un traitement de texte.
			{
				int32 Montrees = 0;
				for (int32 k = (int32)Pages.size() - 1; k >= 0 && Montrees < 4; --k, ++Montrees)
				{
					const auto& P = Pages[(std::size_t)k];
					Col->AddSlot().AutoHeight().Padding(6, 2, 0, 0)
					[ LigneKV(FString::Printf(TEXT("J+%.0f"), P.date_days),
					          FString(UTF8_TO_TCHAR(P.title.c_str())), SRGB(235, 143, 235)) ];
					FString Corps = FString(UTF8_TO_TCHAR(P.body.c_str()));
					Corps.ReplaceInline(TEXT("\r"), TEXT(""));
					TArray<FString> Lignes;
					Corps.ParseIntoArray(Lignes, TEXT("\n"), true);
					for (int32 L = 0; L < Lignes.Num() && L < 3; ++L)
					{
						Col->AddSlot().AutoHeight().Padding(14, 0)
						[ Txt(Lignes[L], 9.0f, ColTexteFaible, 80) ];
					}
					if (Lignes.Num() > 3)
					{
						Col->AddSlot().AutoHeight().Padding(14, 0)
						[ Txt(FString::Printf(TEXT("... (%d lignes de plus)"), Lignes.Num() - 3),
						      9.0f, ColTexteFaible) ];
					}
				}
			}
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
