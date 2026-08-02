// tests/test_gdd_manques.cpp — ORACLES des deux systèmes GDD qui manquaient
// au portage : DÉBRIS ORBITAUX [GDD 7.8, 10.5] et BOÎTE MAIL ARES [GDD 4.1,
// 10.2]. Plus l'assemblage dans GameState (persistance, conséquences).
//
// Ce que ces oracles verrouillent, en propre :
//   . un couloir LEO SE NETTOIE, un couloir haut NON — c'est la règle du GDD,
//     pas un réglage ;
//   . la fragmentation est CALCULÉE (modèle NASA), jamais décrétée ;
//   . un contrat n'existe pour le joueur QUE s'il a été notifié par mail ;
//   . la pollution et le courrier SURVIVENT à la sauvegarde.
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS.
#ifdef SP_STANDALONE_TESTS

#include <cmath>
#include <cstdio>
#include <string>

#include "fen/env/Debris.hpp"
#include "fen/env/Micrometeoroid.hpp"
#include "fen/game/GameState.hpp"
#include "fen/mission/Mail.hpp"

using namespace fen;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)

int main() {
  // ═══════════════ DÉBRIS [GDD 7.8] ═══════════════

  // ---- 1. atmosphère : décroissante, jamais négative ----------------------
  {
    const double a = 0.5;
    CHECK(env::atmospheric_density(200.0, a) > env::atmospheric_density(400.0, a),
          "atmo : densite decroit avec l altitude");
    CHECK(env::atmospheric_density(400.0, a) > env::atmospheric_density(800.0, a),
          "atmo : decroit encore plus haut");
    CHECK(env::atmospheric_density(1200.0, a) > 0.0, "atmo : jamais nulle");
    // L'activité solaire GONFLE la haute atmosphère [GDD 7.7] : plus de traînée.
    CHECK(env::atmospheric_density(400.0, 1.0) > env::atmospheric_density(400.0, 0.0),
          "atmo : maximum solaire = atmosphere plus dense");
  }

  // ---- 2. durée de vie : LE point du GDD ----------------------------------
  // « en orbite basse, la traînée finit par nettoyer le couloir ; en orbite
  //   haute, la pollution est quasi permanente »
  {
    const double a = 0.5, B = env::B_FRAGMENT_DEFAULT;
    const double t300 = env::orbital_lifetime_days(300.0, B, a);
    const double t600 = env::orbital_lifetime_days(600.0, B, a);
    const double t900 = env::orbital_lifetime_days(900.0, B, a);
    CHECK(t300 < t600 && t600 < t900, "duree de vie : croit avec l altitude");
    CHECK(t300 < 365.0, "LEO basse (300 km) : nettoyee en moins d un an");
    CHECK(t900 > 20.0 * 365.25, "LEO haute (900 km) : au-dela de 20 ans");
    CHECK(env::orbital_lifetime_days(110.0, B, a) == 0.0, "sous 120 km : rentree immediate");
    // Un objet dense tombe plus lentement qu'un fragment léger.
    CHECK(env::orbital_lifetime_days(400.0, env::B_ROCKET_BODY, a) >
          env::orbital_lifetime_days(400.0, env::B_FRAGMENT_DEFAULT, a),
          "duree de vie : croit avec le coefficient balistique");
    // Le maximum solaire nettoie PLUS VITE (atmosphère gonflée).
    CHECK(env::orbital_lifetime_days(500.0, B, 1.0) <
          env::orbital_lifetime_days(500.0, B, 0.0),
          "duree de vie : le maximum solaire accelere le nettoyage");
  }

  // ---- 3. fragmentation : modèle NASA, pas un chiffre au doigt mouillé ----
  {
    const double n_expl = env::fragment_count(1000.0, env::BreakupKind::Explosion);
    const double n_coll = env::fragment_count(1000.0, env::BreakupKind::Collision);
    CHECK(n_coll > n_expl, "rupture : une collision fragmente plus qu une explosion");
    CHECK(n_expl > 0.0, "rupture : une explosion produit des fragments");
    // Loi de puissance en M^0.75 : 10x la masse -> ~5.6x les fragments.
    const double r = env::fragment_count(10000.0, env::BreakupKind::Collision) / n_coll;
    CHECK(std::fabs(r - std::pow(10.0, 0.75)) < 1e-9, "rupture : loi en M^0.75");
    // Loi en Lc^-1.71 : compter plus petit, c'est compter beaucoup plus.
    CHECK(env::fragment_count(1000.0, env::BreakupKind::Collision, 0.01) >
          10.0 * n_coll, "rupture : loi en Lc^-1.71");
    CHECK(env::fragment_count(0.0, env::BreakupKind::Collision) == 0.0,
          "rupture : pas de masse, pas de debris");
  }

  // ---- 4. l'environnement : nettoyage et permanence ----------------------
  {
    env::DebrisEnvironment env_bas, env_haut;
    env_bas.add_breakup("VOL-A", 300.0, 2000.0, env::BreakupKind::Collision, 0.0);
    env_haut.add_breakup("VOL-B", 900.0, 2000.0, env::BreakupKind::Collision, 0.0);
    const env::Corridor leo_bas{200.0, 600.0, "LEO basse"};
    const env::Corridor leo_haut{600.0, 1000.0, "LEO haute"};
    const double n0 = env_bas.population(leo_bas);
    CHECK(n0 > 0.0, "environnement : le nuage est enregistre dans son couloir");
    CHECK(env_bas.population(leo_haut) == 0.0,
          "environnement : un nuage ne pollue QUE son couloir");

    // Cinq ans plus tard, le couloir bas est nettoyé, le haut ne l'est pas.
    for (int j = 0; j < 5 * 365; ++j) { env_bas.tick(1.0, 0.5); env_haut.tick(1.0, 0.5); }
    CHECK(env_bas.population(leo_bas) == 0.0, "5 ans : LEO basse nettoyee");
    CHECK(env_haut.population(leo_haut) > 0.5 * 2.0,
          "5 ans : LEO haute encore polluee");
    CHECK(!env_bas.corridor_permanently_polluted(leo_bas, 0.5),
          "LEO basse : jamais declaree definitivement polluee");
    CHECK(env_haut.corridor_permanently_polluted(leo_haut, 0.5),
          "LEO haute : pollution durable a l echelle d une carriere");
  }

  // ---- 5. risque de collision : Poisson, croissant, borné ----------------
  {
    env::DebrisEnvironment e;
    const env::Corridor c{600.0, 1000.0, "LEO haute"};
    CHECK(e.collision_probability(c, 10.0, 30.0) == 0.0,
          "collision : couloir propre = risque nul");
    e.add_breakup("VOL-C", 800.0, 5000.0, env::BreakupKind::Collision, 0.0);
    const double p30 = e.collision_probability(c, 10.0, 30.0);
    const double p365 = e.collision_probability(c, 10.0, 365.0);
    CHECK(p30 > 0.0 && p30 < 1.0, "collision : probabilite dans [0,1[");
    CHECK(p365 > p30, "collision : croit avec la duree d exposition");
    CHECK(e.collision_probability(c, 100.0, 30.0) > p30,
          "collision : croit avec la section du vehicule");
    CHECK(e.collision_probability(c, 10.0, 0.0) == 0.0, "collision : duree nulle = nulle");
  }

  // ═══════════════ BOÎTE MAIL [GDD 10.2] ═══════════════

  // ---- 6. un contrat n'existe que notifié --------------------------------
  {
    mission::MailInbox in;
    mission::MissionContract c;
    c.id = "M00"; c.title = "GEO-SAT 1";
    c.mail_body = "ARES vous confie la mise a poste d un relais geostationnaire.";

    CHECK(!in.contract_notified("M00"), "mail : un contrat non notifie n existe pas");
    CHECK(in.unread_count() == 0, "mail : boite vide au depart");

    in.notify_contract(c, 10.0);
    CHECK(in.contract_notified("M00"), "mail : notifie apres reception");
    CHECK(in.unread_count() == 1, "mail : un non-lu");
    CHECK(in.pending_contracts().size() == 1, "mail : un contrat en attente de reponse");
    // Le corps vient DU CONTRAT : aucune UI n'invente le texte d'ARES.
    CHECK(in.find("MAIL-M00") != nullptr &&
          in.find("MAIL-M00")->body == c.mail_body, "mail : le corps vient du contrat");

    in.notify_contract(c, 20.0);
    CHECK(in.messages().size() == 1, "mail : jamais deux fois le meme contrat");

    in.mark_read("MAIL-M00");
    CHECK(in.unread_count() == 0, "mail : lu");
    CHECK(in.pending_contracts().size() == 1, "mail : lu ne veut pas dire repondu");
    in.mark_answered("MAIL-M00");
    CHECK(in.pending_contracts().empty(), "mail : repondu sort de l attente");
    CHECK(in.messages().size() == 1, "mail : le courrier reste en memoire du programme");
  }

  // ---- 7. le facteur ne livre QUE ce qui a franchi les quatre verrous -----
  {
    mission::MailInbox in;
    mission::MissionCatalog cat;
    career::CareerState carriere;            // Stagiaire
    tech::TechTree arbre;

    mission::CatalogEntry facile;
    facile.contract.id = "M00";
    facile.contract.title = "Relais GEO";
    facile.contract.prerequisites.min_rank = career::Rank::Stagiaire;
    cat.add(facile);

    mission::CatalogEntry verrouille;
    verrouille.contract.id = "M99";
    verrouille.contract.title = "Vol habite martien";
    verrouille.contract.prerequisites.min_rank = career::Rank::Directeur;
    cat.add(verrouille);

    const int n = mission::deliver_unlocked_contracts(in, cat, carriere, arbre,
                                                      1000.0, nullptr, 0.0);
    CHECK(n == 1, "facteur : un seul contrat livre");
    CHECK(in.contract_notified("M00"), "facteur : le contrat accessible est notifie");
    CHECK(!in.contract_notified("M99"),
          "facteur : le contrat hors rang reste invisible [verrou le plus fort]");

    // Rejouer ne redistribue rien.
    CHECK(mission::deliver_unlocked_contracts(in, cat, carriere, arbre, 1000.0,
                                              nullptr, 1.0) == 0,
          "facteur : idempotent");

    // Promu, le second s'ouvre — et arrive par mail, pas par le catalogue.
    carriere.rank = career::Rank::Directeur;
    CHECK(mission::deliver_unlocked_contracts(in, cat, carriere, arbre, 1000.0,
                                              nullptr, 2.0) == 1,
          "facteur : la promotion ouvre le contrat verrouille");
    CHECK(in.contract_notified("M99"), "facteur : notifie apres promotion");
  }

  // ═══════════════ ASSEMBLAGE [GDD 10.4, 18] ═══════════════

  // ---- 8. une anomalie orbitale POLLUE, et le modèle qualifie le palier ---
  {
    game::WorldEpoch ep;
    game::GameState gs(ep, 12345u);
    gs.treasury.balance_musd = 500.0;
    gs.treasury.target_musd = 500.0;

    mission::Mission m;
    m.contract.id = "M42";
    m.contract.family = "geo";

    // Rupture d'un étage en orbite haute : peu d'objets mais couloir durable.
    mission::AnomalyEvent ev;
    ev.mission_id = "M42";
    ev.what = "rupture de reservoir apres extinction";
    ev.severity = mission::Severity::Moderate;
    ev.breakup_mass_kg = 1500.0;
    ev.breakup_alt_km = 850.0;
    ev.breakup_is_collision = false;

    const std::size_t avant = gs.debris.clouds().size();
    gs.apply_anomaly(m, ev);
    CHECK(gs.debris.clouds().size() == avant + 1,
          "consequence : la fragmentation cree un nuage");
    CHECK(gs.debris.clouds().back().origin == "M42",
          "consequence : le nuage est trace jusqu a sa mission");
    // Le modificateur « debris massifs » a été DÉDUIT du calcul, pas coché.
    CHECK(m.anomalies.back().modifiers.massive_debris,
          "consequence : debris massifs deduits du modele");
    CHECK(m.anomalies.back().severity > mission::Severity::Moderate,
          "consequence : le palier est aggrave par les debris [GDD 10.3]");

    // Un échec au SOL ne pollue rien.
    mission::Mission m2;
    m2.contract.id = "M43";
    mission::AnomalyEvent sol;
    sol.mission_id = "M43";
    sol.severity = mission::Severity::Major;
    const std::size_t n_av = gs.debris.clouds().size();
    gs.apply_anomaly(m2, sol);
    CHECK(gs.debris.clouds().size() == n_av, "consequence : un echec au sol ne pollue pas");
    CHECK(!m2.anomalies.back().modifiers.massive_debris,
          "consequence : pas de debris coches sans fragmentation");
  }

  // ---- 9. pollution et courrier survivent à la sauvegarde ----------------
  {
    game::WorldEpoch ep;
    game::GameState a(ep, 777u);
    a.debris.add_breakup("VOL-Z", 780.0, 3000.0, env::BreakupKind::Collision, 5.0);
    mission::MissionContract c;
    c.id = "M07"; c.title = "Sonde"; c.mail_body = "corps du mail";
    a.inbox.notify_contract(c, 5.0);
    a.inbox.mark_answered("MAIL-M07");

    save::Writer w;
    a.save(w);
    save::Reader r(w.bytes().data(), w.bytes().size());
    game::GameState b(ep, 0u);
    CHECK(b.load(r), "sauvegarde : relecture reussie");
    CHECK(b.debris.clouds().size() == a.debris.clouds().size(),
          "sauvegarde : les nuages de debris survivent");
    CHECK(b.debris.clouds().front().origin == "VOL-Z",
          "sauvegarde : la tracabilite du nuage survit");
    CHECK(std::fabs(b.debris.clouds().front().n_objects -
                    a.debris.clouds().front().n_objects) < 1e-9,
          "sauvegarde : la population est restituee exactement");
    CHECK(b.inbox.messages().size() == 1, "sauvegarde : le courrier survit");
    CHECK(b.inbox.contract_notified("M07"),
          "sauvegarde : un contrat notifie le reste apres rechargement");
    CHECK(b.inbox.pending_contracts().empty(),
          "sauvegarde : un contrat repondu ne se rouvre pas");
    CHECK(a.hash() == b.hash(), "sauvegarde : hash d etat identique");
  }

  // ═══════════════ MICROMÉTÉOROÏDES ET PERFORATION [GDD 12.4, 6.5] ═══════════
  // La population que `env::Debris` ne porte PAS. Débris = objets catalogués issus
  // d'une fragmentation, donc une COLLISION ; micrométéoroïdes = fond naturel
  // permanent, donc une PERFORATION. Deux populations, deux conséquences.
  {
    using namespace fen::env;

    // ---- A. LE RECOUPEMENT D'ABORD, ET SANS LE FLATTER ---------------------
    // SSP-30425B publie le flux de météoroïdes à l'orbite de la Station. C'est le
    // seul point de comparaison indépendant disponible, et il ne donne PAS raison
    // à Grün : il est plus haut partout. On mesure l'écart au lieu de l'ignorer.
    struct Ancre { const char* nom; double d_um, rho, ssp; };
    const Ancre ancres[] = {
        {"10 um",  10.0,   2.0, 7.91e2},
        {"100 um", 100.0,  2.0, 8.78e0},
        {"1 mm",   1000.0, 0.5, 5.22e-3},
    };
    std::printf("\n     RECOUPEMENT Grun 1985 / SSP-30425B (flux cumule, /m2/an) :\n");
    double pire_ratio = 0.0;
    for (const Ancre& a : ancres) {
      const double m = meteoroid_mass_g(a.d_um * 1.0e-4, a.rho);
      const double f = grun_flux_per_m2_year(m);
      const double ratio = a.ssp / f;
      if (ratio > pire_ratio) pire_ratio = ratio;
      std::printf("       >= %-7s (m=%9.3e g) : Grun %10.4e | SSP %10.4e"
                  " -> SSP est %5.2f fois plus haut\n", a.nom, m, f, a.ssp, ratio);
      CHECK(f > 0.0 && ratio > 1.0,
            "12.4 : SSP-30425B est majorant sur chaque ancre (ecart declare)");
    }
    // L'écart se resserre vers les tailles qui percent une paroi réelle. C'est
    // l'énoncé qu'on verrouille, PAS un accord qu'on n'a pas.
    const double m_1mm = meteoroid_mass_g(0.1, 0.5);
    const double m_10um = meteoroid_mass_g(1.0e-3, 2.0);
    CHECK(5.22e-3 / grun_flux_per_m2_year(m_1mm)
              < 7.91e2 / grun_flux_per_m2_year(m_10um),
          "12.4 : l ecart avec SSP se resserre vers le millimetre");
    CHECK(pire_ratio < 10.0,
          "12.4 : l ecart avec SSP reste sous un ordre de grandeur");

    // ---- B. le flux est cumulé : décroissant, jamais négatif ---------------
    CHECK(grun_flux_per_m2_year(1.0e-9) > grun_flux_per_m2_year(1.0e-6),
          "Grun : flux cumule decroissant en masse");
    CHECK(grun_flux_per_m2_year(1.0e-6) > grun_flux_per_m2_year(1.0e-3),
          "Grun : decroissant encore plus haut");
    CHECK(grun_flux_per_m2_year(0.0) == 0.0, "Grun : masse nulle = flux nul");
    // Le facteur 3,15576e7 EST le nombre de secondes d'une année julienne.
    CHECK(std::fabs(grun_flux_per_m2_year(1.0e-6)
                    / grun_flux_per_m2_s(1.0e-6) - 3.15576e7) < 1.0,
          "Grun : an et seconde sont le meme flux, au facteur 3,15576e7");
    // HORS DOMAINE : on borne, on n'extrapole pas.
    CHECK(grun_flux_per_m2_year(10.0) == grun_flux_per_m2_year(GRUN_MASS_MAX_G),
          "Grun : au-dela de 1 g on borne (surestimation declaree)");

    // ---- C. masse et diamètre s'inversent exactement -----------------------
    for (double d : {1.0e-3, 1.0e-2, 0.1}) {
      const double m = meteoroid_mass_g(d, 1.0);
      CHECK(std::fabs(meteoroid_diameter_cm(m, 1.0) - d) < 1e-12 * d,
            "geometrie : masse et diametre sont inverses l un de l autre");
    }
    CHECK(meteoroid_density_g_cm3(1.0e-9) == 2.0
          && meteoroid_density_g_cm3(1.0e-4) == 1.0
          && meteoroid_density_g_cm3(1.0) == 0.5,
          "SSP-30425B : les trois paliers de densite, dans l ordre");

    // ---- D. limite balistique de Cour-Palais ------------------------------
    // Une paroi plus épaisse demande un projectile plus gros. Une cible plus dure
    // aussi. Un projectile plus dense perce mieux, donc son diamètre critique est
    // PLUS PETIT — c'est le sens qui compte, et il est facile à écrire à l'envers.
    CHECK(critical_diameter_cm(0.2, wall_al_6061(), 1.0)
              > critical_diameter_cm(0.05, wall_al_6061(), 1.0),
          "Cour-Palais : paroi plus epaisse = projectile critique plus gros");
    CHECK(critical_diameter_cm(0.15, wall_ti_6al4v(), 1.0)
              > critical_diameter_cm(0.15, wall_al_6061(), 1.0),
          "Cour-Palais : titane plus dur = projectile critique plus gros");
    CHECK(critical_diameter_cm(0.15, wall_al_6061(), 2.0)
              < critical_diameter_cm(0.15, wall_al_6061(), 0.5),
          "Cour-Palais : projectile dense = perce avec un diametre plus petit");
    CHECK(critical_diameter_cm(0.15, wall_al_6061(), 1.0, 40.0)
              < critical_diameter_cm(0.15, wall_al_6061(), 1.0, 10.0),
          "Cour-Palais : plus vite = perce avec un diametre plus petit");
    // k = 1,8 perforation, 2,2 ecaillage detache, 3,0 naissant : il faut un
    // projectile PLUS GROS pour un mode de defaillance plus severe.
    CHECK(critical_diameter_cm(0.15, wall_al_6061(), 1.0, 20.0, SPALL_INCIPIENT_K)
              < critical_diameter_cm(0.15, wall_al_6061(), 1.0, 20.0, PERFORATION_K),
          "Cour-Palais : k plus grand = seuil atteint par un projectile plus petit");
    // AUCUNE PAROI N'EST PAS AUCUN IMPACT (piege n°84 : le modele sans consequence).
    CHECK(perforation_flux_per_m2_year(0.0, wall_al_6061()) > 1.0e3,
          "12.4 : sans paroi, tout passe — le flux n est pas nul");

    // ---- E. le gradient de conception, MESURÉ ------------------------------
    std::printf("     PERFORATION Al 6061-T6 a 20 km/s, circuit de %.2f m2 (ISS HRS) :\n",
                RADIATOR_SEGMENT_AREA_M2);
    const double parois[] = {0.5, 1.0, 1.5, 2.0, 3.0};
    double flux_prec = 1.0e9, cap_prec = 0.0;
    for (double mm : parois) {
      const double phi = perforation_flux_per_m2_year(mm * 0.1, wall_al_6061());
      const double cap = radiator_capacity_after(900.0, mm);
      std::printf("       paroi %4.1f mm : Phi = %10.4e /m2/an, capacite a 900 j = %6.4f\n",
                  mm, phi, cap);
      CHECK(phi < flux_prec, "12.4 : epaissir la paroi reduit le flux de perforation");
      CHECK(cap > cap_prec, "12.4 : epaissir la paroi preserve la capacite");
      flux_prec = phi; cap_prec = cap;
    }
    // LA CAPACITÉ DÉCROÎT AVEC LE TEMPS, jamais l'inverse.
    CHECK(radiator_capacity_after(0.0, 1.5) == 1.0, "12.4 : duree nulle = intact");
    CHECK(radiator_capacity_after(1826.0, 1.5) < radiator_capacity_after(900.0, 1.5),
          "12.4 : la capacite decroit avec l exposition");

    // ---- F. CE QUI VALAIT LE DÉTOUR : LA SURFACE TOTALE N'EST PAS LE LEVIER --
    // Chaque circuit meurt de SA première perforation. La fraction survivante est
    // donc la probabilité qu'un circuit soit intact — elle ne dépend pas du nombre
    // de circuits. Mille m² découpés en mille circuits vieillissent comme un seul
    // m². Le levier, c'est la SEGMENTATION, et c'est contre-intuitif.
    CHECK(radiator_capacity_after(900.0, 1.5, 1.0)
              == radiator_capacity_after(900.0, 1.5, 1.0),
          "12.4 : la capacite ne depend pas de la surface totale");
    CHECK(radiator_capacity_after(900.0, 1.5, 0.25)
              > radiator_capacity_after(900.0, 1.5, 1.0),
          "12.4 : segmenter plus finement preserve la capacite");

    // ---- G. LE FORFAIT 1,15 VALAIT QUOI ? ---------------------------------
    // La marge de surface était posée à 1,15 avec pour toute justification
    // « perforations tolérées ». On la DÉRIVE maintenant. La question honnête
    // n'est pas « avait-il tort » mais « à quoi son chiffre correspondait » — et
    // la réponse est qu'il correspondait à UN cas, là où la statistique en exige
    // deux : le forfait unique ENCADRE les deux marges dérivées, trop généreux
    // pour une grande aile, trop chiche pour une petite. C'est le 1/√N.
    const double m_grande = radiator_redundancy_margin(900.0, 1.5, 1000.0);
    const double m_petite = radiator_redundancy_margin(900.0, 1.5, 10.0);
    std::printf("     LE FORFAIT 1,15 : a 900 j et 1,5 mm, la marge derivee vaut"
                " %5.3f sur 1000 m2 et %5.3f sur 10 m2 — un seul chiffre pour deux\n",
                m_grande, m_petite);
    CHECK(m_grande < 1.15 && m_petite > 1.15,
          "12.4 : le forfait unique encadre les marges derivees (statistique en 1/racine N)");

    // ---- H. la marge dérivée, et le sens de sa dépendance à la surface ----
    const double m900 = m_grande;
    const double m1826 = radiator_redundancy_margin(1826.0, 1.5, 1000.0);
    const double m900_petit = radiator_redundancy_margin(900.0, 1.5, 100.0);
    std::printf("     MARGE DERIVEE (paroi 1,5 mm) : 900 j sur 1000 m2 = %5.3f |"
                " 1826 j = %5.3f | 900 j sur 100 m2 = %5.3f\n",
                m900, m1826, m900_petit);
    // LE POINT FIXE EST EXACT, pas itéré : on le vérifie en le réinjectant.
    // perte tolérée = q + k√(q·a/A) doit redonner exactement 1 − 1/M.
    {
      const double q = 1.0 - radiator_capacity_after(900.0, 1.5);
      const double A = 1000.0 * m900;
      const double perte = q + RADIATOR_DESIGN_SIGMA
                             * std::sqrt(q * RADIATOR_SEGMENT_AREA_M2 / A);
      CHECK(std::fabs((1.0 - 1.0 / m900) - perte) < 1e-12,
            "12.4 : la marge est le point fixe EXACT, pas une iteration tronquee");
    }
    CHECK(m900 > 1.0 && m1826 > m900,
          "12.4 : voler plus longtemps demande plus de surface excedentaire");
    // 3σ/N décroît en 1/√N : une GRANDE aile moyenne ses pertes et exige
    // RELATIVEMENT moins de marge. Statistique, pas arbitraire.
    CHECK(m900_petit > m900,
          "12.4 : une petite aile exige relativement plus de marge (3 sigma / racine N)");
    CHECK(radiator_redundancy_margin(0.0, 1.5, 1000.0) == 1.0,
          "12.4 : duree nulle = aucune marge de perforation");
    CHECK(radiator_redundancy_margin(900.0, 3.0, 1000.0) < m900,
          "12.4 : une paroi plus epaisse achete de la marge de surface");

    // ---- I. UN REFUS DOIT NOMMER LA DIRECTION ------------------------------
    // `required_wall_mm` est ce que le joueur peut FAIRE, pas un constat.
    const double req900 = required_wall_mm(900.0, 0.13);
    const double req1826 = required_wall_mm(1826.0, 0.13);
    std::printf("     PAROI REQUISE a 13 %% de perte : 900 j = %.2f mm | 1826 j = %.2f mm"
                " (croissance en T^1/3, pas lineaire)\n", req900, req1826);
    CHECK(req1826 > req900, "12.4 : une mission plus longue demande plus de paroi");
    CHECK(radiator_capacity_after(900.0, req900) > 0.86
              && radiator_capacity_after(900.0, req900 * 0.9) < 0.88,
          "12.4 : la paroi requise est le SEUIL, pas une marge de confort");
    // Le blindage se paie au kilo — sinon personne n'aurait de raison de ne pas
    // mettre 5 mm partout.
    CHECK(radiator_armour_kg_per_m2(RADIATOR_WALL_BASELINE_MM) == 0.0,
          "12.4 : la paroi de reference est deja payee dans la densite surfacique");
    CHECK(radiator_armour_kg_per_m2(2.0) > radiator_armour_kg_per_m2(1.0),
          "12.4 : epaissir la paroi coute de la masse");
    CHECK(radiator_armour_kg_per_m2(2.0, RADIATOR_TUBE_COVERAGE, &wall_ti_6al4v())
              > radiator_armour_kg_per_m2(2.0),
          "12.4 : le titane blinde mieux mais pese plus");
  }

  std::printf("\nGDD (debris + mail) : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
