// spr/bridge/DataBridge.hpp
//
// LE PONT. C'est le SEUL fichier du module de rendu qui inclut des entetes fen/
// (astro_core). Il LIT la physique en lecture seule et en fige une photographie
// (RenderSnapshot). Il ne modifie rien, ne propage rien de son cru : quand il a
// besoin d'une orbite, d'elements ou de la position d'un corps, il appelle
// astro_core (la verite), il ne reimplemente pas Kepler dans le rendu.
//
// Separation nette :  PhysicsCore (fen::)  ->  [DataBridge]  ->  RenderSnapshot
//                                                                   |
//                                              RenderCore / Scene <-+  (sans fen::)
#pragma once
#include <vector>
#include "spr/bridge/RenderSnapshot.hpp"

#include "fen/core/State.hpp"
#include "fen/core/Epoch.hpp"
#include "fen/ephem/Ephemeris.hpp"

namespace spr {

// Quels corps afficher, autour de quel centre. Le centre definit l'origine du
// repere monde (le vaisseau y est deja exprime dans astro_core).
struct WorldConfig {
  fen::ephem::Body              central{fen::ephem::Body::EarthBary};
  std::vector<fen::ephem::Body> bodies{fen::ephem::Body::Sun, fen::ephem::Body::Moon};
};

class DataBridge {
 public:
  DataBridge(const fen::ephem::IEphemeris& eph, WorldConfig cfg);

  // Fige une photographie du systeme + du vaisseau a l'epoque t. `vehicle` est
  // l'etat (r,v,m) dans le repere inertiel du corps central. L'appelant decide
  // quelle est la source autoritaire de cet etat (trajectoire concue, estime de
  // Session::observe(), ou verite en post-mortem) : le pont ne tranche pas
  // l'epistemique, il fige ce qu'on lui donne.
  RenderSnapshot freeze(fen::Epoch t, const fen::State& vehicle) const;

  // Variante systeme seul (pas de vaisseau).
  RenderSnapshot freeze(fen::Epoch t) const;

 private:
  const fen::ephem::IEphemeris& eph_;
  WorldConfig                   cfg_;

  void fill_bodies(RenderSnapshot& s, fen::Epoch t) const;
  // Echantillonne l'orbite osculatrice via astro_core (Elements + conique).
  void fill_orbit(OrbitView& o, bool& valid, const fen::State& st, double mu) const;
  void fill_telemetry(Telemetry& tm, const fen::State& st, double mu, double R) const;
};

} // namespace spr
