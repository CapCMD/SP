// tests/test_contenu_gdd.cpp — ORACLES DU CONTENU NOMMÉ PAR LE GDD
//
// Décision de cadrage : on remplit tout ce que le CORPS du GDD nomme, et rien
// de ce que son chapitre 20 renvoie explicitement à une version ultérieure
// (coûts et durées de recherche unitaires, matrice mission × technos, table
// TRL par rang). Ces oracles vérifient donc la CONFORMITÉ AUX TABLES DU GDD,
// pas des valeurs de game design inventées.
//
//   . filières de propulsion  -> tableau 6.4, à la ligne près
//   . sources d'énergie pures -> paliers 1, 2 et 4 de 5.12.3
//   . modules Novellus        -> les dix de 11.2 et leurs effets de 11.6
//
// STANDALONE UNIQUEMENT : compile avec /DSP_STANDALONE_TESTS.
#ifdef SP_STANDALONE_TESTS

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <set>
#include <string>

#include "app/ares.hpp"
#include "app/vehicle_design.hpp"
#include "fen/rel/Relativity.hpp"
#include "fen/reliability/AdvancedFilieres.hpp"
#include "fen/station/Novellus.hpp"
#include "fen/vehicle/PartsCatalog.hpp"
#include "fen/vehicle/Propulsion.hpp"

using namespace fen;

static int g_ok = 0, g_ko = 0;
#define CHECK(cond, nom)                                                     \
  do {                                                                       \
    if (cond) { ++g_ok; }                                                    \
    else { ++g_ko; std::printf("ECHEC : %s (ligne %d)\n", nom, __LINE__); }  \
  } while (0)
#define CHECK_NEAR(a, b, tol, nom)                                           \
  do {                                                                       \
    const double d_ = std::fabs((a) - (b));                                   \
    const double s_ = std::fabs(b) > 1e-30 ? d_ / std::fabs(b) : d_;          \
    if (s_ <= (tol)) { ++g_ok; }                                              \
    else { ++g_ko; std::printf("ECHEC : %s — %.6g vs %.6g (ligne %d)\n",       \
                               nom, (double)(a), (double)(b), __LINE__); }    \
  } while (0)

int main() {
  // ═══════════ FILIÈRES DE PROPULSION — TABLEAU 6.4 À LA LIGNE PRÈS ═══════════
  {
    using vehicle::PropFamily;
    using vehicle::prop_family;
    const int n = static_cast<int>(std::size(vehicle::PROP_FAMILIES));
    CHECK(n == 8, "6.4 : les huit lignes du tableau sont presentes");

    // Chaque ligne, avec ses bornes EXACTES telles que le GDD les écrit.
    struct Attendu { PropFamily f; double isp_lo, isp_hi; bool continu; };
    const Attendu table[] = {
        {PropFamily::ChemicalSolid,    250.0,   280.0, false},
        {PropFamily::ChemicalLiquid,   300.0,   460.0, false},
        {PropFamily::ElectricHall,    1500.0,  3000.0, true},
        {PropFamily::ElectricGridded, 3000.0, 10000.0, true},
        {PropFamily::Ntp,              850.0,  1000.0, false},
        {PropFamily::Nep,             2000.0, 10000.0, true},
        {PropFamily::Fusion,          1.0e4,   1.0e6,  true},
        {PropFamily::Antimatter,      1.0e5,   1.0e7,  true},
    };
    for (const auto& a : table) {
      const auto* c = prop_family(a.f);
      CHECK(c != nullptr, "6.4 : filiere presente");
      if (!c) continue;
      CHECK_NEAR(c->isp_min_s, a.isp_lo, 1e-12, "6.4 : borne basse d Isp");
      CHECK_NEAR(c->isp_max_s, a.isp_hi, 1e-12, "6.4 : borne haute d Isp");
      CHECK((c->regime == vehicle::BurnRegime::Continuous) == a.continu,
            "6.4 : regime impulsionnel ou continu conforme");
      CHECK(c->isp_min_s < c->isp_max_s, "6.4 : fourchette ordonnee");
      CHECK(c->thrust_min_n < c->thrust_max_n, "6.4 : poussee ordonnee");
      CHECK(std::string(c->limiting_factor).size() > 0,
            "6.4 : facteur limitant dominant renseigne");
    }

    // LECTURE DE DESIGN [6.4] : « plus on descend, plus l Isp monte » est une
    // TENDANCE, pas un ordre strict — le tableau du GDD place le NTP APRÈS
    // l electrique alors que son Isp est dix fois moindre. C est voulu : le NTP
    // est classe par maturite, et il est L EXCEPTION qui troque l Isp contre la
    // poussee. Un oracle qui exigerait la monotonie stricte serait donc FAUX.
    // On verifie la progression sur la chaine des filieres a haut rendement...
    const PropFamily chaine[] = {PropFamily::ChemicalSolid, PropFamily::ChemicalLiquid,
                                 PropFamily::ElectricHall, PropFamily::ElectricGridded,
                                 PropFamily::Fusion, PropFamily::Antimatter};
    for (std::size_t i = 1; i < std::size(chaine); ++i)
      CHECK(prop_family(chaine[i])->isp_max_s >= prop_family(chaine[i - 1])->isp_max_s,
            "6.4 : Isp croissant le long de la chaine a haut rendement");
    // ... et on verifie EXPLICITEMENT que le NTP est l exception documentee :
    // Isp bien inferieur a l electrique, mais poussee de quatre ordres au-dessus.
    CHECK(prop_family(PropFamily::Ntp)->isp_max_s <
          prop_family(PropFamily::ElectricHall)->isp_min_s,
          "6.4 : le NTP a un Isp bien inferieur a l electrique");
    CHECK(prop_family(PropFamily::Ntp)->thrust_min_n /
              prop_family(PropFamily::ElectricHall)->thrust_max_n >= 1.0e4,
          "6.4 : ... mais une poussee de quatre ordres au-dessus — c est LE compromis");
    // « Aucune filiere ne cumule Isp eleve, forte poussee, faible masse ET TRL
    // eleve : chacune paie AU MOINS un axe. » Une filiere qui offre les deux
    // premiers doit donc payer sur la masse : c est le cas des nucleaires, qui
    // trainent blindage et radiateurs. La fusion ne fait pas exception.
    for (const auto& c : vehicle::PROP_FAMILIES)
      CHECK(!(c.isp_min_s > 1500.0 && c.thrust_max_n > 1.0e3) || c.nuclear,
            "6.4 : haut Isp + forte poussee => paye en masse (nucleaire)");

    // INVARIANT 5.12.1 : seules les filieres ALIMENTEES ont besoin de puissance,
    // et le nucleaire traine blindage et radiateurs.
    CHECK(!prop_family(PropFamily::ChemicalLiquid)->needs_power,
          "5.12.1 : le chimique n a pas de source separee");
    CHECK(prop_family(PropFamily::ElectricGridded)->needs_power,
          "5.12.1 : l electrique est plafonne par la puissance");
    CHECK(!prop_family(PropFamily::Ntp)->needs_power,
          "5.12.9 : le NTP chauffe un ergol, il n est pas alimente electriquement");
    CHECK(prop_family(PropFamily::Ntp)->nuclear && prop_family(PropFamily::Nep)->nuclear,
          "5.12 : NTP et NEP sont nucleaires");
    CHECK(!prop_family(PropFamily::ElectricHall)->nuclear,
          "5.12.7 : l electrique seul n est pas nucleaire");

    // 6.3 : SEUL l impulsionnel peut decoller, atterrir ou inserer brutalement.
    CHECK(prop_family(PropFamily::ChemicalSolid)->regime == vehicle::BurnRegime::Impulsive &&
          prop_family(PropFamily::ChemicalLiquid)->regime == vehicle::BurnRegime::Impulsive &&
          prop_family(PropFamily::Ntp)->regime == vehicle::BurnRegime::Impulsive,
          "6.3 : chimique et NTP sont impulsionnels");
    CHECK(prop_family(PropFamily::Nep)->regime == vehicle::BurnRegime::Continuous,
          "6.3 : la NEP est continue — incapable d inserer brutalement");

    // 6.2 : LE COMPROMIS. À puissance fixée, monter l Isp FAIT BAISSER la
    // poussée. C est une identite, on la verifie comme telle.
    const double P = 100e3, eta = 0.6;
    const double f_hall = vehicle::powered_thrust(eta, P, 2000.0 * cst::G0);
    const double f_ion  = vehicle::powered_thrust(eta, P, 6000.0 * cst::G0);
    CHECK_NEAR(f_hall / f_ion, 3.0, 1e-12,
               "6.2 : F = 2.eta.P/ve — tripler l Isp divise la poussee par trois");
  }

  // ═══════════ SOURCES D'ÉNERGIE PURES — PALIERS 1, 2, 4 de 5.12.3 ═══════════
  {
    using vehicle::PropTier;
    CHECK(std::size(vehicle::ENERGY_SOURCES) == 3,
          "5.12.3 : les trois sources pures (solaire, RTG, fission)");
    // 5.12.1 : ces paliers ne POUSSENT pas. `prop_class` doit le refuser.
    CHECK(vehicle::prop_class(PropTier::Solar) == nullptr &&
          vehicle::prop_class(PropTier::Rtg) == nullptr &&
          vehicle::prop_class(PropTier::Fission) == nullptr,
          "5.12.1 : une source d energie n est pas un propulseur");
    CHECK(vehicle::energy_source(PropTier::Solar) != nullptr &&
          vehicle::energy_source(PropTier::Rtg) != nullptr &&
          vehicle::energy_source(PropTier::Fission) != nullptr,
          "5.12.3 : les trois sources sont decrites");
    CHECK(vehicle::energy_source(PropTier::Chemical) == nullptr,
          "5.12.1 : le chimique n est pas une source d energie");

    // 5.12.5 : le solaire DÉCROÎT en 1/d². C est ce qui le rend inutilisable
    // dans le systeme externe.
    const auto* sol = vehicle::energy_source(PropTier::Solar);
    CHECK(sol->falls_off_with_distance, "5.12.5 : le solaire depend de la distance");
    CHECK_NEAR(vehicle::solar_power_at(1000.0, cst::AU), 1000.0, 1e-12,
               "5.12.5 : puissance de reference a 1 UA");
    CHECK_NEAR(vehicle::solar_power_at(1000.0, 2.0 * cst::AU), 250.0, 1e-12,
               "5.12.5 : loi en 1/d2");
    // À Jupiter (5,2 UA) il ne reste que ~3,7 % : d'où le RTG.
    CHECK(vehicle::solar_power_at(1000.0, 5.2 * cst::AU) < 40.0,
          "5.12.5 : marginal au-dela de la ceinture principale");

    // 5.12.6 : le RTG NE décroît PAS avec la distance mais avec le TEMPS.
    const auto* rtg = vehicle::energy_source(PropTier::Rtg);
    CHECK(!rtg->falls_off_with_distance, "5.12.6 : le RTG ignore la distance au Soleil");
    CHECK_NEAR(rtg->half_life_years, 87.7, 1e-12, "5.12.6 : demi-vie du Pu-238");
    CHECK_NEAR(vehicle::rtg_power_after(100.0, 87.7), 50.0, 1e-12,
               "5.12.6 : moitie de la puissance apres une demi-vie");
    CHECK_NEAR(vehicle::rtg_power_after(100.0, 0.0), 100.0, 1e-12,
               "5.12.6 : pas de perte a t=0");
    CHECK(vehicle::rtg_power_after(100.0, 14.0) > 88.0,
          "5.12.6 : ~89 % apres 14 ans (ordre de grandeur Voyager)");

    // 5.12.2 : la MASSE de la source entre au budget. Elle n est jamais gratuite.
    const double m_rtg = vehicle::source_mass_kg(PropTier::Rtg, 1000.0);
    const double m_fis = vehicle::source_mass_kg(PropTier::Fission, 1000.0);
    CHECK(m_rtg > 0.0 && m_fis > 0.0, "5.12.2 : toute source a une masse");
    CHECK(m_rtg > m_fis,
          "5.12.6 : a puissance egale, le RTG est bien plus lourd qu un reacteur");
    CHECK_NEAR(vehicle::source_mass_kg(PropTier::Fission, 2000.0), 2.0 * m_fis,
               1e-12, "5.12.2 : masse proportionnelle a la puissance");
    CHECK(vehicle::source_mass_kg(PropTier::Fission, 1000.0, true) < m_fis,
          "5.12.2 : la fourchette optimiste allege, elle ne supprime pas");
    CHECK(vehicle::source_mass_kg(PropTier::Chemical, 1000.0) == 0.0,
          "5.12.1 : pas de masse de source pour une filiere non alimentee");
    CHECK(rtg->nuclear && vehicle::energy_source(PropTier::Fission)->nuclear &&
          !sol->nuclear, "5.12 : nature nucleaire correctement declaree");
  }

  // ═══════════ NOVELLUS — LES DIX MODULES DE 11.2 ET LEURS EFFETS DE 11.6 ════
  {
    using station::ModuleType;
    using station::Station;
    using station::StationModule;

    // 11.2 : DIX modules, chacun une fonction DISTINCTE — aucun doublon de nom.
    std::set<std::string> noms;
    for (int i = 0; i < 10; ++i)
      noms.insert(station::module_name(static_cast<ModuleType>(i)));
    CHECK(noms.size() == 10, "11.2 : dix modules aux fonctions distinctes");

    // 11.4 : la catégorisation exacte du GDD.
    const ModuleType obligatoires[] = {ModuleType::CommandCore, ModuleType::DockingNode,
                                       ModuleType::LifeSupport, ModuleType::Power,
                                       ModuleType::CrewHabitat, ModuleType::Storage};
    for (auto m : obligatoires)
      CHECK(station::module_category(m) == station::ModuleCategory::Mandatory,
            "11.4 : module obligatoire correctement categorise");
    CHECK(station::module_category(ModuleType::ScienceLab) == station::ModuleCategory::Advanced &&
          station::module_category(ModuleType::Workshop) == station::ModuleCategory::Advanced,
          "11.4 : laboratoire et atelier sont operationnels avances");
    CHECK(station::module_category(ModuleType::Medical) == station::ModuleCategory::Robustness &&
          station::module_category(ModuleType::EvaAirlock) == station::ModuleCategory::Robustness,
          "11.4 : medical et sas EVA relevent de la robustesse");

    // 11.2 : SANS NOYAU, NOVELLUS N EST PAS OPÉRATIONNELLE. C est une
    // definition, pas un equilibrage.
    {
      Station st;
      st.modules.push_back({ModuleType::ScienceLab, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Power, true, 1, 100.0, 0.0});
      const auto e = station::effects(st);
      CHECK(!e.operational, "11.2 : pas de noyau = station non operationnelle");
      CHECK(e.research_speed == 1.0,
            "11.2 : sans noyau, le laboratoire n apporte RIEN");
    }

    // 11.6 : chaque module produit un effet CONCRET. On les verifie un a un.
    {
      Station st;
      st.modules.push_back({ModuleType::CommandCore, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Power, true, 1, 100.0, 0.0});
      auto e = station::effects(st);
      CHECK(e.operational, "11.6 : le noyau rend la station operationnelle");
      CHECK(e.docking_slots == 0 && !e.can_expand,
            "11.6 : sans noeud d amarrage, aucune extension possible");

      st.modules.push_back({ModuleType::DockingNode, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.docking_slots > 0 && e.can_expand,
            "11.6 : le noeud d amarrage ouvre accueil et extension");

      st.modules.push_back({ModuleType::ScienceLab, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.research_speed > 1.0, "11.6 : le laboratoire accelere la recherche");

      st.modules.push_back({ModuleType::Workshop, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.maintenance_quality < 1.0,
            "11.6 : l atelier ameliore la qualite de maintenance");

      st.modules.push_back({ModuleType::CrewHabitat, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::LifeSupport, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.crew_capacity > 0.0, "11.6 : l habitat porte la capacite d equipage");
      CHECK(e.sustainable_days > 0.0, "11.6 : le support-vie porte la duree soutenable");

      const double avant = e.sustainable_days;
      st.modules.push_back({ModuleType::Storage, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.sustainable_days > avant, "11.6 : le stockage prolonge le sejour");

      st.modules.push_back({ModuleType::Medical, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.medical_risk_factor < 1.0, "11.6 : le medical reduit le risque sanitaire");

      st.modules.push_back({ModuleType::EvaAirlock, true, 1, 0.0, 0.0});
      e = station::effects(st);
      CHECK(e.eva_ops, "11.6 : le sas ouvre les operations externes");
      CHECK(st.tier() == 4, "11.3 : les dix modules donnent le palier 4");
    }

    // 11.6 : LE MODULE ÉNERGÉTIQUE CONDITIONNE LES FONCTIONS ÉNERGIVORES.
    {
      Station st;
      st.modules.push_back({ModuleType::CommandCore, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::ScienceLab, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Workshop, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Power, true, 1, 5.0, 0.0});   // trop peu
      auto e = station::effects(st);
      CHECK(st.power_demand_kw() > 0.0, "11.6 : les modules consomment");
      CHECK(!e.power_sufficient, "11.6 : puissance insuffisante detectee");
      CHECK(e.research_speed == 1.0 && e.maintenance_quality == 1.0,
            "11.6 : sans marge, labo et atelier ne tournent pas");

      st.modules.back().power_supply_kw = 200.0;                       // de quoi tout tenir
      e = station::effects(st);
      CHECK(e.power_sufficient && e.power_margin_kw > 0.0,
            "11.6 : une generation superieure debloque les fonctions");
      CHECK(e.research_speed > 1.0 && e.maintenance_quality < 1.0,
            "11.6 : labo et atelier reprennent");
    }

    // 11.3 : les quatre paliers, dans l ordre exact du GDD.
    {
      Station st;
      CHECK(st.tier() == 0, "11.3 : station vide = palier 0");
      st.modules.push_back({ModuleType::CommandCore, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::DockingNode, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Power, true, 1, 100.0, 0.0});
      CHECK(st.tier() == 1, "11.3 : palier 1 = fondation structurelle et energetique");
      st.modules.push_back({ModuleType::LifeSupport, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::CrewHabitat, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Storage, true, 1, 0.0, 0.0});
      CHECK(st.tier() == 2, "11.3 : palier 2 = mise en habitabilite");
      st.modules.push_back({ModuleType::ScienceLab, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::Workshop, true, 1, 0.0, 0.0});
      CHECK(st.tier() == 3, "11.3 : palier 3 = exploitation avancee");
      st.modules.push_back({ModuleType::Medical, true, 1, 0.0, 0.0});
      st.modules.push_back({ModuleType::EvaAirlock, true, 1, 0.0, 0.0});
      CHECK(st.tier() == 4, "11.3 : palier 4 = autonomie et robustesse");
    }
  }

  // ═══════════ ARBRE TECHNOLOGIQUE — SOUS-BRANCHES DE 5.7–5.13 ═══════════
  {
    tech::TechTree t;
    app::AresLayer::seed_arbre(t);
    const auto& tous = t.all();

    // 5.2 : les SIX branches sont peuplées. Une branche vide serait un chapitre
    // du GDD sans contenu.
    int par_branche[6] = {0, 0, 0, 0, 0, 0};
    int transverses = 0;
    for (const auto& n : tous) {
      par_branche[static_cast<int>(n.branch)]++;
      if (n.transverse) ++transverses;
    }
    for (int b = 0; b < 6; ++b)
      CHECK(par_branche[b] >= 5, "5.2 : chaque branche porte des noeuds");
    CHECK(tous.size() >= 60, "5.7-5.13 : l arbre couvre les sous-branches nommees");

    // 5.13 : les technologies transverses sont DISTRIBUÉES, pas regroupées en
    // une septième branche déguisée.
    CHECK(transverses >= 8, "5.13 : les transverses nommees sont presentes");
    std::set<int> branches_transverses;
    for (const auto& n : tous)
      if (n.transverse) branches_transverses.insert(static_cast<int>(n.branch));
    CHECK(branches_transverses.size() >= 4,
          "5.13 : les transverses sont REPARTIES sur plusieurs branches");

    // Chaque sous-branche que le GDD NOMME doit exister. Liste tirée
    // littéralement de 5.7 à 5.13.
    const char* nommees[] = {
        // 5.7
        "lanceur_leger", "lanceur_moyen", "lanceur_lourd", "lanceur_super_lourd",
        "rentree_capsule", "reutilisation", "rdv_automatise", "transfert_ergols",
        "cadence_industrielle",
        // 5.8
        "sondes", "orbiteurs_cartographie", "edl_robotique", "rovers",
        "prelevement", "retour_echantillons", "robotique_orbitale",
        // 5.9
        "capsule_habitee", "eva", "amarrage_habite", "sauvetage_habite",
        "station_modulaire", "logistique_leo",
        // 5.10
        "recyclage_partiel", "eclss_habite", "medecine_embarquee",
        "facteurs_humains", "maintenance_locale",
        // 5.11
        "hohmann_ops", "gravity_assist", "multi_survols", "nav_profonde",
        "capture_orbitale", "aerofreinage", "aerocapture", "prepositionnement",
        // 5.12
        "solaire", "rtg", "electrique_avancee", "fission_spatiale", "ntp",
        "nep_megawatt", "fusion", "antimatiere",
        // 5.13
        "materiaux_ht", "radioprotection", "thermique_radiateurs", "avionique",
        "informatique_bord", "capteurs_navigation", "communications",
        "robotique", "qualification_essais", "fabrication_metrologie",
    };
    for (const char* id : nommees)
      CHECK(t.find(id) != nullptr, "5.7-5.13 : sous-branche nommee par le GDD presente");

    // 5.1 : « une mission avancée ne dépend jamais d'une technologie isolée mais
    // de la CONVERGENCE de plusieurs maturités. » Les nœuds de fin d'arbre
    // doivent donc avoir plusieurs prérequis.
    CHECK(t.find("nep_megawatt")->prereqs.size() >= 3,
          "5.1 : la NEP megawatt exige la convergence de plusieurs filieres");
    CHECK(t.find("fission_spatiale")->prereqs.size() >= 3,
          "5.12.8 : le reacteur exige thermique, materiaux, blindage, essais");

    // 5.13, exemple LITTÉRAL du GDD : « une NEP mégawatt reste inaccessible tant
    // que les filières thermique/radiateurs et matériaux haute température ne
    // sont pas mûres, même si le réacteur lui-même l'est. »
    {
      const auto& p = t.find("nep_megawatt")->prereqs;
      const bool thermique = std::find(p.begin(), p.end(), "thermique_radiateurs") != p.end();
      const bool materiaux = std::find(p.begin(), p.end(), "materiaux_ht") != p.end();
      CHECK(thermique && materiaux, "5.13 : l exemple du GDD est cable tel quel");
    }

    // 5.3 : le DAG est cohérent — tout prérequis existe, et aucun cycle.
    for (const auto& n : tous)
      for (const auto& p : n.prereqs)
        CHECK(t.find(p) != nullptr, "5.3 : tout prerequis designe un noeud existant");

    // 19.2 : le rang ne remplace jamais la science — un nœud ne peut pas exiger
    // un rang INFÉRIEUR à celui de ses prérequis (ce serait un contournement).
    for (const auto& n : tous)
      for (const auto& p : n.prereqs) {
        const tech::TechNode* pre = t.find(p);
        if (pre) CHECK(n.min_rank >= pre->min_rank,
                       "19.2 : un noeud n abaisse jamais le rang de ses prerequis");
      }

    // 5.6 : ce que le monde sait DÉJÀ faire est opérationnel au départ ; ce qui
    // reste à chercher ne l'est pas.
    CHECK(t.find("lanceur_moyen")->operational(), "5.6 : lanceur moyen disponible au depart");
    CHECK(t.find("sondes")->operational(), "5.6 : sondes disponibles au depart");
    CHECK(t.find("capsule_habitee")->operational(), "5.6 : capsule habitee au depart");
    CHECK(!t.find("fusion")->operational(), "5.6 : la fusion reste a chercher");
    CHECK(!t.find("antimatiere")->operational(), "5.6 : l antimatiere reste a chercher");
    CHECK(t.find("antimatiere")->min_rank == career::Rank::Directeur,
          "5.12.12 : l antimatiere est reservee au Directeur, fin d arbre");

    // GDD 20 : les coûts et durées sont DÉCLARÉS PROVISOIRES, jamais présentés
    // comme du design validé.
    int chiffres = 0, provisoires = 0;
    for (const auto& n : tous) {
      if (n.research_days > 0.0 || n.research_cost_musd > 0.0) {
        ++chiffres;
        if (n.costs_provisional) ++provisoires;
      }
    }
    CHECK(chiffres > 0 && chiffres == provisoires,
          "GDD 20 : tout cout ou duree chiffre est marque PROVISOIRE");

    // 4.3 : plus la percée est lointaine, plus elle est longue. On le vérifie
    // sur la chaîne que le GDD ordonne lui-même en 5.12.3.
    CHECK(t.find("antimatiere")->research_days > t.find("fusion")->research_days,
          "4.3 : l antimatiere est plus longue que la fusion");
    CHECK(t.find("fusion")->research_days > t.find("nep_megawatt")->research_days,
          "4.3 : la fusion est plus longue que la NEP");
    CHECK(t.find("nep_megawatt")->research_days > t.find("electrique_avancee")->research_days,
          "4.3 : la NEP est plus longue que l electrique");
  }

  // ═══════════ CATALOGUE DE MISSIONS — LES TYPES DE 10.1 ═══════════
  {
    mission::MissionCatalog c;
    app::AresLayer::seed_catalogue(c);
    const auto& e = c.entries();
    CHECK(e.size() >= 10, "10.1 : les grands types de mission sont couverts");

    // 10.2 : un contrat ne peut PAS exister sans son corps de mail, puisque
    // c'est le seul canal par lequel il entre dans la partie.
    for (const auto& x : e) {
      CHECK(!x.contract.mail_body.empty(), "10.2 : tout contrat porte son mail");
      CHECK(!x.contract.id.empty() && !x.contract.title.empty(),
            "10.2 : tout contrat est identifie et titre");
      CHECK(!x.contract.family.empty(),
            "10.4 : tout contrat a une famille (suspension par filiere)");
      CHECK(!x.contract.prerequisites.required_tech.empty(),
            "4.2 : aucune mission n est jouable sans prerequis techniques");
    }

    // 4.2 : « une mission avancee ne depend JAMAIS d une seule technologie mais
    // d un ENSEMBLE coherent de maturites ». L exemple litteral du GDD est la
    // mission habitee martienne : lanceur lourd, propulsion adaptee, support-vie
    // longue duree, protection radiative, systeme de rentree ou d insertion.
    const mission::CatalogEntry* mars = nullptr;
    for (const auto& x : e) if (x.contract.id == "CAT-09") mars = &x;
    CHECK(mars != nullptr, "10.1 : la mission habitee martienne est au catalogue");
    if (mars) {
      const auto& t = mars->contract.prerequisites.required_tech;
      CHECK(t.size() >= 5, "4.2 : la mission martienne exige une CONVERGENCE");
      auto exige = [&t](const char* id) {
        return std::find(t.begin(), t.end(), std::string(id)) != t.end();
      };
      CHECK(exige("lanceur_super_lourd"), "4.2 : ... un lanceur lourd");
      CHECK(exige("ntp"), "4.2 : ... une propulsion adaptee");
      CHECK(exige("sejour_long"), "4.2 : ... un support-vie longue duree");
      CHECK(exige("radioprotection"), "4.2 : ... une protection radiative credible");
      CHECK(exige("aerocapture"), "4.2 : ... un systeme d insertion approprie");
      CHECK(mars->contract.crewed, "10.1 : elle est habitee");
      CHECK(mars->contract.prerequisites.min_rank == career::Rank::Directeur,
            "3.2 : reservee au Directeur de Programme");
    }

    // 19.3 : la mission relativiste est en FIN d arbre et n existe que par
    // l antimatiere.
    const mission::CatalogEntry* rel = nullptr;
    for (const auto& x : e) if (x.contract.id == "CAT-11") rel = &x;
    CHECK(rel != nullptr, "10.1 : la mission a propulsion extreme est au catalogue");
    if (rel) {
      const auto& t = rel->contract.prerequisites.required_tech;
      CHECK(std::find(t.begin(), t.end(), std::string("antimatiere")) != t.end(),
            "19.3/19.4 : seule l antimatiere ouvre le regime relativiste");
      CHECK(rel->contract.prerequisites.infra.station_tier == 4,
            "5.4 : elle exige l infrastructure la plus complete");
    }

    // 3.2 : les rangs sont ETAGES — il existe des missions a chaque niveau.
    std::set<int> rangs;
    for (const auto& x : e)
      rangs.insert(static_cast<int>(x.contract.prerequisites.min_rank));
    CHECK(rangs.size() >= 4, "3.2 : le catalogue s etage sur les rangs de carriere");
    CHECK(rangs.count(static_cast<int>(career::Rank::Stagiaire)) > 0,
          "3.2 : un Stagiaire a de quoi commencer");
  }

  // ═══════════ CATALOGUE DE PIÈCES — [GDD 12.1] ═══════════
  {
    const auto& moteurs = vehicle::engine_catalog();
    CHECK(moteurs.size() >= 15, "12.1 : le catalogue de moteurs est fourni");

    for (const auto& p : moteurs) {
      // 12.3.1 : aucune donnee sans provenance.
      CHECK(std::string(p.source).size() > 0, "12.1 : toute piece porte sa source");
      CHECK(std::string(p.lineage).size() > 0, "12.1 : toute piece porte sa LIGNEE reelle");
      // 6.4 : Isp = fourchette DURE ; poussee = ORDRE de grandeur (une decade).
      CHECK(vehicle::engine_within_family_envelope(p),
            "6.4 : la piece reste dans l enveloppe de sa filiere");
      const vehicle::PropFamilyClass* fam = vehicle::prop_family(p.family);
      CHECK(fam && p.isp_vac_s >= fam->isp_min_s && p.isp_vac_s <= fam->isp_max_s,
            "6.4 : l Isp respecte STRICTEMENT la fourchette du tableau");
      // 12.5 : pas de fausse precision sur du speculatif.
      CHECK(vehicle::part_confidence_consistent(p),
            "12.5 : confiance, TRL et incertitude coherents");
      // Physique elementaire : Isp et poussee positifs, masse positive.
      CHECK(p.isp_vac_s > 0.0 && p.thrust_vac_n > 0.0 && p.mass_kg > 0.0,
            "12.1 : performances physiquement valides");
      if (p.isp_sl_s > 0.0)
        CHECK(p.isp_sl_s < p.isp_vac_s,
              "physique : l Isp au sol est TOUJOURS inferieur a l Isp dans le vide");
    }

    // 12.1 : « les composants speculatifs ne sont introduits que TARDIVEMENT ».
    int voles = 0, speculatifs = 0;
    for (const auto& p : moteurs) {
      if (p.status == vehicle::QualStatus::Flown) ++voles;
      if (p.status == vehicle::QualStatus::Speculative) ++speculatifs;
    }
    CHECK(voles > speculatifs,
          "12.1 : le catalogue est domine par des pieces REELLES, pas des concepts");
    CHECK(speculatifs > 0, "12.1 : le speculatif existe, mais marque comme tel");

    // Ancrages sur des valeurs publiques verifiables.
    const auto* rs25 = vehicle::find_engine("RS-25");
    CHECK(rs25 != nullptr, "12.1 : le RS-25 est au catalogue");
    if (rs25) {
      CHECK_NEAR(rs25->isp_vac_s, 452.3, 1e-9, "RS-25 : Isp vide publie");
      CHECK(rs25->confidence == vehicle::PartConfidence::A,
            "RS-25 : donnee de niveau A (135 vols)");
    }
    const auto* nstar = vehicle::find_engine("NSTAR");
    CHECK(nstar != nullptr && nstar->thrust_vac_n < 1.0,
          "6.4 : un ion a grilles pousse moins d un newton");
    const auto* f1 = vehicle::find_engine("F-1");
    CHECK(f1 != nullptr && f1->thrust_vac_n > 7.0e6,
          "12.1 : le F-1 reste le plus puissant du catalogue");

    // LE MATÉRIEL RÉEL DÉBORDE PARFOIS DU TABLEAU, et on doit le savoir.
    // Le SPT-100 pousse 83 mN quand 6.4 ecrit 0,1 N pour le Hall ; le NERVA NRX
    // poussait 334 kN quand 6.4 ecrit 10-100 kN pour le NTP. Entre une donnee
    // mesuree et un ordre de grandeur redactionnel, la donnee gagne [12.3.1].
    const auto* spt = vehicle::find_engine("SPT-100");
    const auto* nerva = vehicle::find_engine("NERVA-NRX");
    CHECK(spt && vehicle::engine_outside_literal_thrust_band(*spt),
          "12.3.1 : le SPT-100 sort de la bande litterale, et c est signale");
    CHECK(nerva && vehicle::engine_outside_literal_thrust_band(*nerva),
          "12.3.1 : le NERVA NRX sort de la bande litterale, et c est signale");
    CHECK(spt && vehicle::engine_within_family_envelope(*spt),
          "6.4 : ... mais il reste dans l ORDRE de grandeur");
    // La majorite des pieces, elle, doit tomber dans la bande litterale : si ce
    // n etait pas le cas, ce serait la CLASSIFICATION qui serait fausse.
    int hors_bande = 0;
    for (const auto& p : moteurs)
      if (vehicle::engine_outside_literal_thrust_band(p)) ++hors_bande;
    CHECK(hors_bande * 4 < static_cast<int>(moteurs.size()),
          "6.4 : les debordements restent des exceptions, pas la regle");

    // Le catalogue DECRIT, Vehicle.hpp CALCULE : la conversion doit conserver
    // la physique, pas la reinventer.
    if (rs25) {
      const vehicle::Engine e = vehicle::to_engine(*rs25);
      CHECK_NEAR(e.ve(), rs25->isp_vac_s * cst::G0, 1e-9,
                 "conversion : ve = Isp.g0, aucune valeur reinventee");
      CHECK_NEAR(e.mdot(), e.thrust_vac / e.ve(), 1e-9,
                 "conversion : mdot = F/ve");
      CHECK(e.heritage > 0.9, "conversion : une piece volee a un heritage fort");
    }
    const auto* fus = vehicle::find_engine("FUSION-DD");
    if (fus) CHECK(vehicle::to_engine(*fus).heritage < 0.2,
                   "12.5 : une piece speculative n herite d aucune confiance");

    // Reservoirs et capsules : memes exigences.
    CHECK(vehicle::tank_catalog().size() >= 5, "12.1 : reservoirs au catalogue");
    for (const auto& t : vehicle::tank_catalog()) {
      CHECK(t.dry_fraction > 0.0 && t.dry_fraction < 0.5,
            "12.1 : fraction seche physiquement plausible");
      CHECK(t.density_kg_m3 > 0.0, "12.1 : densite renseignee");
      CHECK(std::string(t.source).size() > 0, "12.1 : reservoir source");
    }
    CHECK(vehicle::capsule_catalog().size() >= 4, "12.1 : capsules au catalogue");
    for (const auto& c : vehicle::capsule_catalog()) {
      CHECK(c.nose_radius_m > 0.0 && c.cd_hypersonic > 0.0 && c.area_m2 > 0.0,
            "7.6 : une capsule est un CORPS DE RENTREE parametre");
      CHECK(c.max_entry_g > 0.0, "8.5 : chaque capsule porte sa limite structurale");
      CHECK(std::string(c.lineage).size() > 0, "12.1 : capsule de lignee reelle");
    }
    // Le module Apollo doit redonner le coefficient balistique attendu.
    const auto* apollo = vehicle::find_capsule("APOLLO-CM");
    if (apollo) {
      const double B = apollo->dry_mass_kg / (apollo->cd_hypersonic * apollo->area_m2);
      CHECK(B > 300.0 && B < 360.0, "12.1 : B du module Apollo ~ 340 kg/m2");
    }
  }

  // ═══════════ SITES DE LANCEMENT — GÉOMÉTRIE RÉELLE [GDD 13.3] ═══════════
  {
    using economy::launch_sites;
    const auto& sites = launch_sites();
    CHECK(sites.size() >= 3, "13.3 : Kourou, Cape Canaveral, Baikonour");

    const economy::LaunchSite* kourou = nullptr;
    const economy::LaunchSite* cap = nullptr;
    const economy::LaunchSite* baik = nullptr;
    for (const auto& s : sites) {
      if (s.name == "Kourou") kourou = &s;
      if (s.name == "Cape Canaveral") cap = &s;
      if (s.name == "Baikonour") baik = &s;
    }
    CHECK(kourou && cap && baik, "13.3 : les trois sites nommes sont presents");

    // 1) L INCLINAISON MINIMALE EST LA LATITUDE. On ne descend jamais dessous
    // sans dog-leg — c est de la trigonometrie spherique, pas un reglage.
    CHECK(!cap->reachable(20.0), "13.3 : Cap Canaveral (28,5 deg) ne peut PAS viser 20 deg");
    CHECK(cap->reachable(28.5), "13.3 : ... mais atteint sa propre latitude");
    CHECK(!baik->reachable(40.0), "13.3 : Baikonour (46 deg) ne descend pas a 40 deg");
    CHECK(baik->reachable(51.6), "13.3 : ... et sert bien l ISS a 51,6 deg");

    // 2) L AZIMUT REQUIS SUIT cos(i) = sin(beta).cos(phi).
    // A i = phi, le tir est PLEIN EST (90 deg).
    const auto az_eq = kourou->required_azimuth_deg(kourou->latitude_deg);
    CHECK(az_eq.has_value(), "13.3 : azimut defini a l inclinaison minimale");
    if (az_eq) CHECK(std::fabs(*az_eq - 90.0) < 1e-6,
                     "13.3 : i = latitude => tir plein est (azimut 90 deg)");
    // Une orbite polaire (i = 90) se tire plein NORD (azimut 0), depuis tout site.
    const auto az_pol = cap->required_azimuth_deg(90.0);
    CHECK(az_pol.has_value() && std::fabs(*az_pol) < 1e-6,
          "13.3 : orbite polaire => tir plein nord (azimut 0)");
    // Sous la latitude : AUCUN azimut ne convient.
    CHECK(!baik->required_azimuth_deg(30.0).has_value(),
          "13.3 : pas d azimut sous la latitude du site");

    // 3) LE COULOIR D AZIMUT BORNE LE RESTE. Cape Canaveral, couloir 35-120 deg,
    // ne peut pas tirer plein nord : pas de polaire depuis la Floride (survol
    // des cotes habitees) — contrainte geographique reelle.
    CHECK(!cap->azimuth_allowed(0.0),
          "13.3 : pas de tir plein nord depuis Cape Canaveral (couloir 35-120)");
    CHECK(cap->azimuth_allowed(90.0), "13.3 : tir est autorise depuis la Floride");
    // Kourou, couloir large chevauchant le nord, atteint le polaire.
    CHECK(kourou->reachable(90.0), "13.3 : Kourou peut viser une orbite polaire");

    // 4) LA ROTATION TERRESTRE : « GTO roi » pour Kourou. Le gain plein est a
    // l equateur vaut omega.R = 465 m/s ; a 5 deg de latitude, ~463 m/s.
    const double g_kourou = kourou->rotation_velocity_gain(kourou->latitude_deg);
    CHECK(g_kourou > 455.0 && g_kourou < 466.0,
          "13.3 : Kourou tire ~463 m/s de la rotation terrestre (GTO roi)");
    // Plus la latitude monte, moins la rotation aide : c est ce qui degrade
    // Baikonour pour le GTO malgre son cout logistique plus bas.
    const double g_baik = baik->rotation_velocity_gain(baik->latitude_deg);
    CHECK(g_baik < g_kourou,
          "13.3 : un site plus haut en latitude tire MOINS de la rotation");
    // Le gain plein est a l equateur est le maximum theorique.
    CHECK_NEAR(cst::OMEGA_EARTH * cst::R_EARTH, 465.1, 0.01,
               "13.3 : gain equatorial plein est = omega.R ~ 465 m/s");
    // Viser plus haut que la latitude reduit le gain (l azimut s ecarte de l est).
    CHECK(kourou->rotation_velocity_gain(60.0) <
          kourou->rotation_velocity_gain(kourou->latitude_deg),
          "13.3 : viser une inclinaison plus haute reduit l aide de la rotation");

    // 5) COUTS RELATIFS — arbitrage technique ET budgetaire [GDD 13.3].
    CHECK(baik->cost_factor < kourou->cost_factor,
          "13.3 : Baikonour moins cher, mais moins bien place");
  }

  // ═══════════ DÉGRADATION DES FILIÈRES AVANCÉES [GDD 12.4] ═══════════
  {
    using namespace fen::reliability;

    // 1) CŒURS NUCLÉAIRES : la fiabilite decroit avec le burnup et le temps.
    CHECK_NEAR(nuclear_core_reliability(0.0, 0.0), 1.0, 1e-12,
               "12.4 : un coeur neuf part de 1");
    CHECK(nuclear_core_reliability(0.5, 0.0) < nuclear_core_reliability(0.1, 0.0),
          "12.4 : plus de burnup = moins fiable");
    CHECK(nuclear_core_reliability(0.3, 10.0) < nuclear_core_reliability(0.3, 0.0),
          "12.4 : le vieillissement calendaire degrade aussi");
    // Le NTP se degrade PLUS VITE : cyclage thermique de l ergol dans le coeur.
    CHECK(nuclear_core_reliability(0.5, 0.0, true) <
          nuclear_core_reliability(0.5, 0.0, false),
          "12.4 : le NTP fatigue plus vite que la fission de puissance");
    CHECK(nuclear_core_reliability(2.0, 0.0) > 0.0,
          "12.4 : la fiabilite reste positive, jamais un verdict binaire");

    // 2) RADIATEURS : perces par les debris, ils perdent leur capacite. On
    // BRANCHE sur l environnement de debris deja modelise.
    env::DebrisEnvironment env_prop;               // couloir propre
    const env::Corridor leo{600.0, 1000.0, "LEO haute"};
    RadiatorWear rad{200.0, 0.15};                 // grand radiateur NEP
    CHECK_NEAR(radiator_capacity_fraction(rad, env_prop, leo, 365.0), 1.0, 1e-9,
               "12.4 : un couloir propre n erode pas le radiateur");
    // Un couloir pollue erode : la capacite chute avec le temps d exposition.
    env::DebrisEnvironment env_sale;
    env_sale.add_breakup("VOL-X", 800.0, 8000.0, env::BreakupKind::Collision, 0.0);
    const double c1 = radiator_capacity_fraction(rad, env_sale, leo, 100.0);
    const double c2 = radiator_capacity_fraction(rad, env_sale, leo, 400.0);
    CHECK(c1 < 1.0 && c2 < c1, "12.4 : le radiateur s erode avec l exposition");
    CHECK(c2 > 0.0, "12.4 : erosion continue, jamais un mur");
    // Un GRAND radiateur (NEP/fusion) est PLUS vulnerable qu un petit : c est le
    // point precis du GDD (facteur dominant de vulnerabilite).
    RadiatorWear petit{20.0, 0.15};
    CHECK(radiator_capacity_fraction(petit, env_sale, leo, 400.0) >
          radiator_capacity_fraction(rad, env_sale, leo, 400.0),
          "12.4 : un grand radiateur est plus vulnerable — critique pour NEP/fusion");
    // Verrou thermique : si la capacite tombe sous la charge, la mission est bloquee.
    CHECK(thermal_still_ok(1.0, 1000.0, 800.0), "12.4 : radiateur neuf couvre la charge");
    CHECK(!thermal_still_ok(0.5, 1000.0, 800.0),
          "12.4 : radiateur a moitie erode ne couvre plus 800 W — mission bloquee");

    // 3) CONFINEMENT ANTIMATIERE : perte = CATASTROPHE, donc on modelise la
    // survie, pas une performance qui grignote.
    CHECK_NEAR(antimatter_confinement_survival(0.0, 1.0), 1.0, 1e-12,
               "12.4 : survie certaine a t=0");
    CHECK(antimatter_confinement_survival(100.0, 1.0) < 1.0,
          "12.4 : le risque de perte croit avec la duree");
    // Un confinement de MAUVAISE qualite echoue bien plus vite.
    CHECK(antimatter_confinement_survival(100.0, 0.2) <
          antimatter_confinement_survival(100.0, 1.0),
          "12.4 : un confinement immature perd bien plus souvent");
    // Survie + perte = 1 (c est un complement, pas deux modeles independants).
    CHECK_NEAR(antimatter_confinement_survival(200.0, 0.5) +
               antimatter_confinement_loss_prob(200.0, 0.5), 1.0, 1e-12,
               "12.4 : survie et perte sont complementaires");
    // Sur une mission relativiste de plusieurs annees, meme un bon confinement
    // porte un risque NON negligeable : c est ce qui fait de l antimatiere un
    // changement de REGIME, pas un meilleur moteur [GDD 19.3].
    CHECK(antimatter_confinement_loss_prob(3.0 * 365.0, 1.0) > 0.5,
          "19.3 : sur 3 ans, la perte de confinement est un risque majeur");
  }

  // ═══════════ ATELIER D'ASSEMBLAGE — TSIOLKOVSKY [GDD 12.2] ═══════════
  {
    using namespace fen::app;
    // La conception de départ : deux étages chimiques, doit être faisable.
    VehicleDesign d = VehicleDesign::starter();
    DesignSummary s = evaluate_design(d);
    CHECK(s.valid, "12.2 : la conception de depart est valide");
    CHECK(s.converged, "12.2 : le sizing converge");
    CHECK(s.liftoff_capable, "6.3 : l etage du bas (chimique) peut decoller");
    CHECK(s.warning.empty(), "12.2 : aucune alerte sur une conception saine");
    CHECK(s.stages.size() == 2, "12.2 : deux etages lus dans le tableau");

    // Le Δv total est la SOMME des Δv confiés — pas une valeur inventee.
    CHECK_NEAR(s.total_dv_ms, d.stages[0].dv_target_ms + d.stages[1].dv_target_ms,
               1e-9, "12.2 : Delta-v total = somme des etages");

    // TSIOLKOVSKY : demander PLUS de Δv coute EXPONENTIELLEMENT plus d ergols.
    VehicleDesign d2 = d;
    d2.stages[1].dv_target_ms += 2000.0;
    DesignSummary s2 = evaluate_design(d2);
    CHECK(s2.liftoff_mass_kg > s.liftoff_mass_kg,
          "6.1 : plus de Delta-v = plus de masse au decollage");
    // La croissance est SUR-lineaire (exponentielle) : doubler la marge ne
    // double pas la masse, il l'exponentie.
    const double ratio_dv = s2.total_dv_ms / s.total_dv_ms;
    const double ratio_m = s2.liftoff_mass_kg / s.liftoff_mass_kg;
    CHECK(ratio_m > ratio_dv, "6.1 : la masse croit plus vite que le Delta-v");

    // Une charge utile plus lourde alourdit tout le lanceur (effet cascade).
    VehicleDesign d3 = d;
    d3.payload_kg += 2000.0;
    CHECK(evaluate_design(d3).liftoff_mass_kg > s.liftoff_mass_kg,
          "6.1 : une charge utile plus lourde alourdit tout le lanceur");

    // Une capsule AJOUTE sa masse seche a la charge utile.
    VehicleDesign d4 = d;
    d4.capsule = 0;   // module Apollo
    CHECK(evaluate_design(d4).payload_kg > s.payload_kg,
          "12.1 : la capsule s ajoute a la charge utile");

    // VALIDATION 6.3 : un etage du bas ELECTRIQUE ne peut pas decoller. Le
    // modele le DIT (il ne bloque pas — c est une validation, pas un mur).
    VehicleDesign elec = d;
    elec.stages[0].engine = VehicleDesign::index_moteur("NSTAR");  // ion a grilles
    DesignSummary se = evaluate_design(elec);
    CHECK(!se.liftoff_capable, "6.3 : un ion a grilles ne decolle pas");
    CHECK(!se.warning.empty(), "6.3 : l infaisabilite au decollage est signalee");

    // Retirer tous les etages sauf un reste valide ; zero etage est invalide.
    VehicleDesign vide;
    CHECK(!evaluate_design(vide).valid, "12.2 : aucun etage = conception invalide");

    // Le partage du Δv est au JOUEUR : le modele n optimise pas. Deux partages
    // differents du meme Δv total donnent des masses differentes (l etagement
    // compte), et c est a lui de trancher [anti-feature 1.5].
    VehicleDesign a = d, b = d;
    a.stages[0].dv_target_ms = 2000.0; a.stages[1].dv_target_ms = 6100.0;
    b.stages[0].dv_target_ms = 6100.0; b.stages[1].dv_target_ms = 2000.0;
    const double ma = evaluate_design(a).liftoff_mass_kg;
    const double mb = evaluate_design(b).liftoff_mass_kg;
    CHECK(std::fabs(ma - mb) > 1.0, "1.5 : le partage du Delta-v change la masse (choix du joueur)");
  }

  // ═══════════ RELATIVITÉ v1.2 : verrou aller-retour + antimatière [GDD 6.7] ══
  {
    using namespace fen::rel;
    const double ve = VE_ANTIMATTER_EFF;   // c/3

    // Le seuil narratif est bien plus haut que le seuil mesurable.
    CHECK(BETA_NARRATIVE > BETA_THRESHOLD, "6.7.2 : seuil narratif > seuil mesurable");
    CHECK(BETA_NARRATIVE >= 0.7 - 1e-9, "6.7.2 : perceptible vers beta >= 0,7");

    // VERROU DE L'ALLER-RETOUR [6.7.4] : ratio unitaire puissance 4.
    const double r1 = mass_ratio(0.5, ve);
    const double r4 = round_trip_mass_ratio(0.5, ve);
    CHECK_NEAR(r4, r1 * r1 * r1 * r1, 1e-6 * r4, "6.7.4 : aller-retour = ratio unitaire^4");
    // Ancrages du tableau du GDD : beta=0,5 -> unitaire ~5,2, aller-retour ~730.
    CHECK(r1 > 4.5 && r1 < 6.0, "6.7.4 : ratio unitaire ~5,2 a beta=0,5");
    CHECK(r4 > 500.0 && r4 < 1100.0, "6.7.4 : aller-retour ~730 a beta=0,5");
    // beta=0,9 : l'aller-retour explose (~4,7e7) — hors de portee sans ravito.
    CHECK(round_trip_mass_ratio(0.9, ve) > 1.0e7,
          "6.7.4 : aller-retour a beta=0,9 physiquement hors de portee");
    // Monotonie : plus vite = plus cher, et l'aller-retour toujours pire que l'aller.
    CHECK(round_trip_mass_ratio(0.6, ve) > round_trip_mass_ratio(0.5, ve),
          "6.7.4 : plus de beta = ratio aller-retour plus grand");
    CHECK(round_trip_mass_ratio(0.5, ve) > mass_ratio(0.5, ve),
          "6.7.4 : l aller-retour coute toujours plus que l aller simple");

    // CHAÎNE ANTIMATIÈRE [5.12.12] : masse <-> beta, inversion coherente.
    const double mdry = 10000.0;   // 10 tonnes de vehicule sec
    const double a_needed = antimatter_needed_g(mdry, 0.3);
    CHECK(a_needed > 0.0, "5.12.12 : atteindre beta demande de l antimatiere");
    // Inversion : cette masse redonne bien ~beta=0,3.
    const double b_back = beta_from_antimatter(mdry, a_needed);
    CHECK_NEAR(b_back, 0.3, 0.02, "5.12.12 : masse -> beta -> masse coherent");
    // LE POINT DU GDD : quelques grammes ne donnent qu'un beta derisoire.
    CHECK(beta_from_antimatter(mdry, 1.0) < 1.0e-4,
          "19.3 : 1 g d antimatiere sur 10 t = beta minuscule");
    // ... et viser beta=0,3 demande des TONNES d'antimatiere (hors echelle).
    CHECK(a_needed > 1.0e6, "19.3 : viser beta=0,3 demande des tonnes d antimatiere");

    // LE MODÈLE DE PRODUCTION est le levier : debit faible => accumulation
    // pluriannuelle ; confinement plafonne le stock utile.
    AntimatterProduction prod;
    CHECK(prod.years_to_accumulate(1.0) >= 1.0,
          "5.12.12 : produire un gramme est pluriannuel");
    CHECK(prod.max_usable_stock_g() <= prod.confinement_capacity_g + 1e-9,
          "5.12.12 : le confinement plafonne le stock utile");
    CHECK(prod.cost_to_produce_me(1.0) > 1.0e5,
          "5.12.12 : le cout par gramme est hors echelle");
    CHECK(prod.stock_survival(0.0) == 1.0 && prod.stock_survival(1e9) < 1.0,
          "5.12.12 : le confinement perd du stock avec le temps (risque permanent)");
    // Un meilleur debit reduit le temps d'accumulation (le vrai levier).
    AntimatterProduction fast; fast.production_g_per_yr = 1.0;
    CHECK(fast.years_to_accumulate(1.0) < prod.years_to_accumulate(1.0),
          "5.12.12 : ameliorer le debit raccourcit l accumulation");
  }

  std::printf("\nCONTENU GDD (6.4, 5.12, 11, arbre, 10.1, 12.1, 13.3, 12.4, 12.2, 6.7) : %d OK, %d en echec.\n",
              g_ok, g_ko);
  return g_ko == 0 ? 0 : 1;
}

#endif // SP_STANDALONE_TESTS
