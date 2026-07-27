// SPCameraPost.h — L'IMAGE DU MONDE UNIQUE : UN SEUL post-traitement.
//
// Le monde d'ARES est UNE scène persistante [GDD v1.2 décision 19, ch.17.3] :
// elle n'a donc qu'une image. Les deux caméras qui la regardent — le plan système
// (SPSolarSystem) et la première personne à bord (SPStation) — doivent partager
// le MÊME post-traitement.
//
// POURQUOI C'EST UNE RÈGLE ET PAS UN DÉTAIL : la carte fige son exposition (voir
// plus bas) tandis qu'une caméra neuve garde l'auto-exposition du projet. Deux
// politiques d'exposition = un saut de luminance à l'instant où le vol [M] rend la
// main à l'ambulation. Constaté par capture pendant l'incrément 3c-3 : géométrie,
// position, orientation et champ concordaient au pixel, et l'image « clignotait »
// quand même. Un réglage partagé rend la faute impossible plutôt que de la
// corriger après coup.
#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"

namespace SPCameraPost
{
	// ═══ L'IMAGE DU MONDE ═══
	inline void Appliquer(FPostProcessSettings& PP)
	{
		// EXPOSITION FIGÉE À 1. L'auto-exposition cherche une luminance moyenne :
		// sur un ciel presque noir semé d'étoiles elle ouvrirait à fond et laverait
		// tout (le fond de la référence est un noir profond). Le mode « Manual » ne
		// conviendrait pas non plus : il dérive des réglages d'appareil photo (f/4,
		// 1/60 s, ISO 100 -> facteur ~0,09) et éteindrait les étoiles. On borne donc
		// la luminance de référence à 1 des deux côtés : le rendu passe tel quel,
		// sans correction.
		// Note : le projet active r.DefaultFeature.AutoExposure.
		// ExtendDefaultLuminanceRange, donc ces bornes sont des EV100 et non des
		// luminances — 0 EV100 = aucune correction.
		PP.bOverride_AutoExposureMinBrightness = true;
		PP.AutoExposureMinBrightness = 0.0f;
		PP.bOverride_AutoExposureMaxBrightness = true;
		PP.AutoExposureMaxBrightness = 0.0f;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = 0.0f;
		// BLOOM : le halo du Soleil et l'éclat des marqueurs émissifs. Seuil haut
		// pour que seules les sources vraiment lumineuses débordent — les étoiles du
		// fond doivent rester des points nets (et les néons de la station aussi).
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.85f;
		PP.bOverride_BloomThreshold = true;
		PP.BloomThreshold = 1.0f;
		// Aucun flou de profondeur ni grain : la lisibilité prime [GDD 6.8, 17.1].
		PP.bOverride_DepthOfFieldFocalDistance = true;
		PP.DepthOfFieldFocalDistance = 0.0f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.15f;
		// MOTION BLUR COUPÉ. Le monde est rebasé sur l'œil chaque frame : quand la
		// caméra tourne, ce sont les OBJETS que le moteur voit « bouger » (vecteurs
		// de mouvement énormes et faux), pas la caméra. Le motion blur les étale
		// alors en traînées (« les textures bavent »). Une vue façon NASA Eyes n'en a
		// de toute façon aucun besoin, et l'ambulation en apesanteur pas davantage.
		PP.bOverride_MotionBlurAmount = true;
		PP.MotionBlurAmount = 0.0f;
	}
}
