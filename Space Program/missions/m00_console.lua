-- missions/m00_console.lua — LA MISSION M00, RESOLUE ENTIEREMENT DEPUIS LA CONSOLE.
-- (La version JOUABLE, avec choix du niveau de poursuite, est missions/m00.lua.)
--
-- Pas une ligne de C++. C'est le critere d'acceptation de la console (GDD §3.5).
--
-- CE QUE LE JOUEUR N'A PAS :
--   la position VRAIE du vaisseau. Elle n'existe pas dans cette API.
-- CE QU'IL A :
--   S:observe()  -> un ESTIME + sa covariance, issu des passes qu'il a PAYEES
--   fen.forces() -> SON modele de forces, distinct de la verite. Il paie l'ecart.
--
-- Toute la boucle est donc : OBSERVER -> PREDIRE (avec mon modele) -> CORRIGER.
-- Le jeu ne corrige jamais le joueur. Il propage.

local seed  = tonumber(arg[1]) or 4071
local doc   = fen.load_fpl("missions/m00_geo_solution.fpl")
local S     = fen.session(doc, seed)
local t0    = doc.epoch0
local MU    = fen.MU_EARTH

print(("=========================================================="))
print((" M00 — LEO 200 km / 28,5 deg  ->  GEO.  Graine %d"):format(seed))
print((" Resolu depuis la CONSOLE. Zero ligne de C++."))
print(("=========================================================="))

-- ---------------------------------------------------------------------------
-- 1. J'ACHETE DE LA NAVIGATION. Trois stations, deux revolutions.
--    Ce n'est pas gratuit, et ce n'est pas decoratif : sans ca, P(succes) = 6 %.
-- ---------------------------------------------------------------------------
-- LA LECON, APPRISE EN LA RATANT : il faut un arc de poursuite APRES CHAQUE
-- MANOEUVRE. Sans lui, l'OD ajuste sur des mesures d'avant la brulure, et rend
-- une orbite absurde (mesure : a = -1400 km, e = 461). Ce n'est pas un bug du
-- moteur : c'est le prix de la navigation, et il se paie a l'avance.
-- ET LA DEUXIEME LECON, PLUS DURE : l'arc doit etre COMPLET.
-- Un arc de 15 000 s sur une orbite de 24 h ne CONDITIONNE PAS l'OD : la
-- geometrie ne tourne pas assez, la matrice normale devient quasi singuliere,
-- et Gauss-Newton DIVERGE (mesure : a = -1 220 km, e = 711). Ca ne coute pas
-- plus cher par heure — mais ca coute des HEURES. Le temps est une ressource.
local ARCS = { {3600, 15000},        -- avant l'AMF
               {21000, 58000},       -- apres l'AMF   : arc COMPLET
               {65000, 103000} }     -- apres l'AMF2  : arc COMPLET
for st = 0, 2 do
  for _, a in ipairs(ARCS) do
    S:schedule_pass{ station = st, t_start = t0 + a[1], t_end = t0 + a[2], sample_dt = 60 }
  end
end

-- ---------------------------------------------------------------------------
-- 2. MON MODELE. Pas celui du monde : le MIEN. Je choisis ce que j'y mets.
-- ---------------------------------------------------------------------------
local G = fen.forces()
G:central(MU)
G:third_body("Sun",  "Earth")
G:third_body("Moon", "Earth")

-- Predire une apside AVEC MON MODELE, a partir d'un ESTIME.
local function predict(o, want, skip)
  local el = fen.rv_to_elements(o.r, o.v, MU)
  local T  = fen.orbital_period(el.a, MU)
  if not (T > 0) then return nil end
  local mid = fen.propagate(G, o.t, o.r, o.v, o.m, o.t + skip * T)      -- on saute
  local r   = fen.propagate(G, o.t + skip * T, mid.r, mid.v, mid.m,
                            o.t + (skip + 1.1) * T,
                            { {kind="periapsis", mu=MU}, {kind="apoapsis", mu=MU} })
  for _, ev in ipairs(r.events) do
    if want == nil or ev.name == want then return ev end
  end
  return nil
end

-- La vitesse VISEE a une apside : celle qui met l'autre apside sur le rayon cible.
local function target_v(r, r_other)
  local a  = 0.5 * (r:norm() + r_other)
  local vm = fen.vis_viva(r:norm(), a, MU)
  return fen.unit(fen.cross(fen.vec3(0,0,1), r)) * vm     -- prograde, equatorial
end

local function burn(id, ev, v_target)
  local dv = v_target - ev.v                              -- calcule sur l'ETAT PREDIT
  local rep = S:commit_burn{ id = id, t = ev.t, dv = dv, frame = "inertial", stage = 0 }
  print(("  [%-5s] t0+%7.0f s | dv commande %8.2f | realise %8.2f | ergols %6.1f kg")
        :format(id, ev.t - t0, rep.dv_commanded, rep.dv_achieved, rep.propellant))
  return rep.dv_commanded
end

-- ---------------------------------------------------------------------------
-- 3. LA BOUCLE. Injection -> observer -> predire -> corriger. Trois fois.
-- ---------------------------------------------------------------------------
local b0 = fen.plan_burn(doc, 1)
S:commit_burn(b0)                                          -- injection LEO -> GTO
if not S:alive() then print("  *** perdu a l'injection") ; os.exit(1) end

local dv_corr = 0
-- Le TRIM attend 2 revolutions de plus : plus d'arc = meilleure OD.
-- On PAIE en temps ce qu'on ne peut pas acheter en argent.
local steps = { {"AMF", 15200, 0.00, 0}, {"AMF2", nil, 0.02, 0}, {"TRIM", nil, 0.02, 2} }

for k, st in ipairs(steps) do
  local id, wait, skip, revs = st[1], st[2], st[3], st[4]
  if wait then
    S:advance_to(t0 + wait)
  else
    local o  = S:observe()
    local el = fen.rv_to_elements(o.r, o.v, MU)
    S:advance_to(S:t() + (0.38 + revs) * fen.orbital_period(el.a, MU))
  end
  local o = S:observe()
  print(("  [OBS %d] %-28s sigma_pos = %8.1f m | %d mesures")
        :format(k, o.source, o.sigma_pos, o.n_measurements))
  local ev = predict(o, (k == 1) and "APOAPSIS" or nil, skip)
  if not ev then print("  *** pas d'apside predite") ; os.exit(1) end
  local vt
  if k < 3 then vt = target_v(ev.r, fen.R_GEO)             -- viser GEO a l'autre apside
  else          vt = fen.unit(fen.cross(fen.vec3(0,0,1), ev.r))
                     * fen.v_circular(ev.r:norm(), MU)      -- circulariser
  end
  local d = burn(id, ev, vt)
  if not S:alive() then print("  *** perdu pendant "..id) ; os.exit(1) end
  if k > 1 then dv_corr = dv_corr + d end
end

-- ---------------------------------------------------------------------------
-- 4. LE VERDICT. Sur l'ESTIME — je n'ai rien d'autre. C'est ca, le jeu.
-- ---------------------------------------------------------------------------
local o  = S:observe()
local el = fen.rv_to_elements(o.r, o.v, MU)
local da = (el.a - fen.R_GEO) / 1000
print("")
print(("  a  = %10.1f km   (GEO : %.1f km, ecart %+.1f km)")
      :format(el.a/1000, fen.R_GEO/1000, da))
print(("  e  = %10.6f      (cible : 0)"):format(el.e))
print(("  i  = %10.4f deg  (cible : 0)"):format(el.i / fen.DEG))
print(("  Delta-v de CORRECTION paye : %.1f m/s"):format(dv_corr))
print(("  navigation achetee         : %.1f M$ (%d mesures)")
      :format(S:tracking_cost_musd(), S:n_measurements()))
print(("  Delta-v restant en soute   : %.1f m/s"):format(S:dv_remaining(0)))

local ok = math.abs(da) < 50 and el.e < 0.001 and math.abs(el.i / fen.DEG) < 0.1
print("")
print(("  >>> %s"):format(ok and "MISSION REUSSIE" or "MISSION RATEE"))
os.exit(ok and 0 or 1)
