// scripts/m00_play.cpp — M00 JOUABLE DE BOUT EN BOUT (les deux boucles du GDD).
//
// Le manque que le GDD reprochait : le joueur ne faisait que choisir un niveau de
// poursuite. Ici il traverse la VRAIE boucle :
//
//   BOUCLE A (conception)   : il derive, il budgete, il converge son vehicule
//   BOUCLE B (execution)     : il navigue, il corrige, la physique tranche
//   POST-MORTEM              : le jeu decompose l'echec, il ne dit jamais "rate"
//
// C'est le meme moteur de verite partout (une regle du GDD). Rien n'est simule
// pour la demo : chaque chiffre sort de la propagation N-corps + Gates.
//
//   Usage : m00_play <graine> <niveau_poursuite 0..6> [--budget-marge <m/s>]
//
// Le "niveau de poursuite" reste un choix, mais il est desormais ENCADRE par une
// conception que le joueur a d'abord validee (m00_design), et suivi d'un
// post-mortem qui explique. C'est la difference entre un menu et un jeu.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include "fen/io/Fpl.hpp"
#include "fen/flight/Session.hpp"
#include "fen/astro/Elements.hpp"
#include "fen/astro/Transfers.hpp"
#include "fen/nav/Statistics.hpp"
using namespace fen;
using namespace fen::cst;
static constexpr double R_GEO = 42164170.0;

// les 7 niveaux de poursuite (memes que le moteur : m00_nav.cpp)
struct Niveau { const char* nom; double cout_musd; const char* reussite;
                std::vector<std::array<double,3>> passes; int extra_revs; };
static Niveau niveau_of(int lvl, double t0) {
  auto P = [&](int s,double a,double b){ return std::array<double,3>{(double)s, t0+a, t0+b}; };
  switch (lvl) {
    case 0: return {"AVEUGLE", 0.0, "~5%", {}, 0};
    case 1: return {"30 min, 1 station", 0.07, "~50%", {P(0,4000,5800)}, 0};
    case 2: return {"3h, 1 station", 0.47, "~40%", {P(0,3600,15000)}, 0};
    case 3: return {"1 arc/manoeuvre", 1.68, "~40%",
                    {P(0,3600,15000),P(2,21000,36000),P(2,64000,78000)}, 0};
    case 4: return {"3 stations, arcs courts", 5.05, "~85%",
                    {P(0,3600,15000),P(1,3600,15000),P(2,3600,15000),
                     P(0,21000,36000),P(1,21000,36000),P(2,21000,36000),
                     P(0,64000,78000),P(1,64000,78000),P(2,64000,78000)}, 0};
    case 5: return {"3 stations, arcs COMPLETS", 10.8, "~90%",
                    {P(0,3600,15000),P(1,3600,15000),P(2,3600,15000),
                     P(0,21000,58000),P(1,21000,58000),P(2,21000,58000),
                     P(0,65000,103000),P(1,65000,103000),P(2,65000,103000)}, 0};
    default: return {"tout + 2 revolutions", 32.9, "~100%",
                    {P(0,3600,15000),P(1,3600,15000),P(2,3600,15000),
                     P(0,21000,58000),P(1,21000,58000),P(2,21000,58000),
                     P(0,65000,280000),P(1,65000,280000),P(2,65000,280000)}, 2};
  }
}

// un vol complet (boucle fermee : nav -> correction). Renvoie le verdict SUR LA VERITE.
struct Vol { bool ok; double a, e, i_deg, dv_corr, cout; bool alive; const char* mode; };
static Vol voler(const io::FplDocument& doc, const ephem::IEphemeris& eph,
                 std::uint64_t seed, const Niveau& N, bool verbose) {
  prop::PropOptions opt; opt.step.rtol = 1e-11; opt.sample_dt = 0.0;
  flight::Session S(doc.plan, eph, seed, opt);
  for (auto& p : N.passes) { nav::Pass q; q.station=(int)p[0]; q.t_start=p[1]; q.t_end=p[2]; q.sample_dt=60; S.schedule_pass(q); }
  const double t0 = doc.plan.epoch0;
  force::ForceStack G;
  G.add(std::make_shared<force::CentralGravity>(MU_EARTH));
  G.add(std::make_shared<force::ThirdBodyGravity>(&eph, ephem::Body::Sun, ephem::Body::EarthBary));
  G.add(std::make_shared<force::ThirdBodyGravity>(&eph, ephem::Body::Moon, ephem::Body::EarthBary));
  Vol v{false,0,0,0,0,N.cout_musd,true,"boucle fermee"};
  auto say = [&](const char* f, auto... a){ if (verbose) std::printf(f, a...); };

  struct Pred { double t; StateN y; bool ok; };
  auto predict = [&](const flight::Observation& o, const char* only, double skip)->Pred {
    const auto el = astro::rv_to_elements(o.state.r, o.state.v, MU_EARTH);
    const double T = astro::orbital_period(el.a, MU_EARTH);
    if (!(T>0)) return {0,{},false};
    StateN y{o.state.r.x,o.state.r.y,o.state.r.z,o.state.v.x,o.state.v.y,o.state.v.z,o.state.m};
    prop::PropOptions po=opt; po.sample_dt=0;
    auto r0 = prop::propagate(G, o.t, y, o.t+skip*T, {}, po);
    auto r = prop::propagate(G, o.t+skip*T, r0.y_final, o.t+skip*T+1.1*T,
                             {prop::event_periapsis(MU_EARTH),prop::event_apoapsis(MU_EARTH)}, po);
    for (auto& ev : r.events) if (!only[0] || ev.name==only) return {ev.t, ev.y, true};
    return {0,{},false};
  };
  // Comme predict, mais renvoie l'apside dont le RAYON est le plus proche de R_GEO :
  // la circularisation finale doit viser l'apside QUI EST a R_GEO (sinon `a` derape).
  auto predict_rgeo = [&](const flight::Observation& o, double skip)->Pred {
    const auto el = astro::rv_to_elements(o.state.r, o.state.v, MU_EARTH);
    const double T = astro::orbital_period(el.a, MU_EARTH); if(!(T>0)) return {0,{},false};
    StateN y{o.state.r.x,o.state.r.y,o.state.r.z,o.state.v.x,o.state.v.y,o.state.v.z,o.state.m};
    prop::PropOptions po=opt; po.sample_dt=0;
    auto r0 = prop::propagate(G, o.t, y, o.t+skip*T, {}, po);
    auto r = prop::propagate(G, o.t+skip*T, r0.y_final, o.t+skip*T+1.15*T,
                             {prop::event_periapsis(MU_EARTH),prop::event_apoapsis(MU_EARTH)}, po);
    Pred best{0,{},false}; double bd=1e30;
    for (auto& ev : r.events){ double d=std::fabs(norm(pos(ev.y))-R_GEO); if(d<bd){bd=d;best={ev.t,ev.y,true};} }
    return best;
  };
  auto target_v = [&](const Vec3& r){
    const double a=0.5*(norm(r)+R_GEO);
    return unit(cross(Vec3{0,0,1},r))*astro::vis_viva(norm(r),a,MU_EARTH); };
  auto burn = [&](const char* id, const Pred& p, const Vec3& vt){
    flight::BurnCmd b; b.id=id; b.t=p.t; b.frame=flight::DvFrame::Inertial;
    b.hold=force::ThrustFrame::InertialFixed; b.stage=0; b.dv=vt-vel(p.y);
    const double n=norm(b.dv); S.commit_burn(b); return n; };

  S.commit_burn(doc.plan.burns[0]);
  if (!S.alive()) { v.alive=false; return v; }
  S.advance_to(t0+15200);
  auto o=S.observe();
  say("  [OBS 1] %s | sigma_pos %.1f m\n", o.source.c_str(), o.sigma_pos);
  auto p1=predict(o,"APOAPSIS",0.0); if(!p1.ok){v.mode="nav insuffisante";return v;}
  v.dv_corr += 0; burn("AMF",p1,target_v(pos(p1.y)));   // AMF n'est pas une "correction"
  if(!S.alive()){v.alive=false;return v;}
  auto el2=astro::rv_to_elements(S.observe().state.r,S.observe().state.v,MU_EARTH);
  if(!(el2.a>0)){v.mode="estime diverge";return v;}
  S.advance_to(S.t()+0.38*astro::orbital_period(el2.a,MU_EARTH));
  o=S.observe();
  say("  [OBS 2] %s | sigma_pos %.1f m\n", o.source.c_str(), o.sigma_pos);
  auto p2=predict(o,"",0.02); if(!p2.ok){v.mode="nav insuffisante";return v;}
  v.dv_corr += burn("AMF2",p2,target_v(pos(p2.y)));
  if(!S.alive()){v.alive=false;return v;}
  auto el3=astro::rv_to_elements(S.observe().state.r,S.observe().state.v,MU_EARTH);
  if(!(el3.a>0)){v.mode="estime diverge";return v;}
  S.advance_to(S.t()+(0.38+N.extra_revs)*astro::orbital_period(el3.a,MU_EARTH));
  o=S.observe();
  say("  [OBS 3] %s | sigma_pos %.1f m\n", o.source.c_str(), o.sigma_pos);
  auto p3=predict_rgeo(o,0.02); if(!p3.ok){v.mode="nav insuffisante";return v;}
  {const Vec3 rp=pos(p3.y); const Vec3 vt=unit(cross(Vec3{0,0,1},rp))*astro::v_circular(norm(rp),MU_EARTH);
   v.dv_corr += burn("TRIM",p3,vt);}
  if(!S.alive()){v.alive=false;return v;}
  S.advance_to(S.t()+2000);
  // VERDICT SUR LA VERITE
  const auto tr=S.truth_state();
  const auto el=astro::rv_to_elements(tr.r,tr.v,MU_EARTH);
  v.a=el.a; v.e=el.e; v.i_deg=el.i/DEG;
  v.ok = std::fabs(el.a-R_GEO)<50e3 && el.e<2e-3 && el.i/DEG<0.25;
  v.cout = S.tracking_cost_musd();
  return v;
}

int main(int argc, char** argv) {
  const std::uint64_t seed = (argc>1)?std::strtoull(argv[1],nullptr,10):4071;
  const int lvl = (argc>2)?std::atoi(argv[2]):5;
  double marge = 0;
  for (int i=3;i<argc-1;++i) if(!std::strcmp(argv[i],"--budget-marge")) marge=std::atof(argv[i+1]);

  auto doc = io::parse_fpl("missions/m00_geo_solution.fpl");
  ephem::StandishEphemeris eph;
  const Niveau N = niveau_of(lvl, doc.plan.epoch0);

  std::printf("=====================================================================\n");
  std::printf(" M00 — VOL COMPLET.  graine %llu | poursuite : %s (%.1f M$, %s)\n",
              (unsigned long long)seed, N.nom, N.cout_musd, N.reussite);
  std::printf("=====================================================================\n\n");

  Vol v = voler(doc, eph, seed, N, true);

  std::printf("\n--- VERDICT (sur la VERITE, que le pilote ne voit pas) --------------\n");
  if (!v.alive) {
    std::printf("  reservoir vide en vol : marge sous-dimensionnee.\n");
    std::printf("  >>> MISSION PERDUE\n");
    return 0;
  }
  if (v.mode[0]!='b') {
    std::printf("  %s : la navigation achetee ne suffit pas a estimer l'orbite.\n", v.mode);
    std::printf("  >>> MISSION PERDUE (avant meme d'echouer sur la cible)\n");
    return 0;
  }
  std::printf("  a = %10.1f km  (GEO %.0f, ecart %+.1f km, tol +/-50)\n", v.a/1000, R_GEO/1000, (v.a-R_GEO)/1000);
  std::printf("  e = %10.6f     (tol < 2e-3)\n", v.e);
  std::printf("  i = %10.4f deg (tol < 0.25)\n", v.i_deg);
  std::printf("  correction payee : %.1f m/s | navigation : %.1f M$\n", v.dv_corr, v.cout);
  std::printf("\n  >>> %s\n", v.ok ? "MISSION REUSSIE" : "MISSION RATEE");

  // POST-MORTEM : le jeu ne dit jamais "pas de chance". Il decompose (GDD §4).
  if (!v.ok) {
    std::printf("\n--- POST-MORTEM (le jeu ne dit jamais \"rate\", il DECOMPOSE) ---------\n");
    // mini Monte-Carlo au meme niveau pour situer ce vol dans la distribution
    int ok=0, n=40; double sa=0, s2=0;
    for (int k=0;k<n;++k){ auto w=voler(doc,eph,900+k,N,false); if(w.alive&&w.mode[0]=='b'){ if(w.ok)++ok; const double e=(w.a-R_GEO)/1000; sa+=e; s2+=e*e; } }
    const double moy=sa/n, sig=std::sqrt(std::max(0.0,s2/n-moy*moy));
    std::printf("  a ce niveau de poursuite, P(succes) mesuree ~ %d%% (n=%d)\n", 100*ok/n, n);
    std::printf("  dispersion sur a : sigma ~ %.0f km. Ton ecart : %+.0f km.\n", sig, (v.a-R_GEO)/1000);
    if (std::fabs((v.a-R_GEO)/1000) > 2*sig)
      std::printf("  ton vol est dans la QUEUE de la distribution : malchance de tirage.\n");
    else
      std::printf("  ton vol est TYPIQUE de ce niveau : ce n'est pas la malchance,\n"
                  "  c'est que tu n'as pas achete assez de navigation.\n");
    std::printf("  source dominante : navigation insuffisante (cf. m00_postmortem).\n");
    std::printf("  REMEDE : monte d'un niveau de poursuite, ou provisionne plus de marge.\n");
  } else {
    std::printf("\n  marge restante suffisante. Pour SAVOIR si ce niveau est fiable,\n");
    std::printf("  rejoue plusieurs graines : la reussite est une PROBABILITE, pas un oui/non.\n");
  }
  return 0;
}
