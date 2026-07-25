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

  std::printf("\nGDD (debris + mail) : %d oracles OK, %d en echec.\n", g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
