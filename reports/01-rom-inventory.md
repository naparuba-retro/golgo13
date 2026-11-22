# Golgo 13 (arcade) — Rapport d'inventaire ROM (étape 0, aucun code de portage)

Sources : `golgo13.zip` (46 165 385 o) extrait dans `roms/golgo13/` ; hashes calculés par `work/hashes.py` ;
ROM_START lu dans `src/mame/namco/namcos12.cpp` (master, HTTP 200) ; régions reconstruites par `work/analyze.py`
dans `work/regions/*.bin`.

## Verdict
- **17/17 fichiers : taille, CRC32 et SHA1 identiques au ROM_START `golgo13`.** Romset complet et sain.
- Pas de parent, pas de BIOS séparé (`ROM_DEFAULT_BIOS` absent, parent 0), pas de CHD (aucune ligne `ROM_DISK` dans le bloc). Init = `empty_init`.
- Pas de KEYCUS pour ce jeu (le driver n'en mentionne pas ; commentaire `/* KC054 */` = n° de carte).
- **Licence de `namcos12.cpp` : BSD-3-Clause, copyright smf** (lignes 1-2). Réutilisation possible en conservant la notice ; on évite quand même la traduction ligne à ligne.

## Fichiers → régions MAME
Entrelacement `ROM_LOAD16_BYTE` : suffixe `l` = octets pairs, `u` = octets impairs ; reconstruction en LE 16 bits.

| Fichier | Taille | CRC32 | Région MAME (offset) | Contenu |
|---|---|---|---|---|
| glg1vera.2l / .2p | 2 Mo ×2 | aa15abfe / 37a4cf90 | `maincpu:rom` 4 Mo (0x0 pair/impair) | BIOS Sony modifié + code du jeu |
| glg1rom0l.10 / 0u.14 | 8 Mo ×2 | 2837524e / 4482deff | `bankedroms` 0x0000000 (16 Mo) | données (graphismes etc.) |
| glg1rom1l.11 / 1u.15 | 8 Mo ×2 | 080f3550 / b49e8613 | `bankedroms` 0x1000000 | idem |
| glg1rom2l.12 / 2u.16 | 8 Mo ×2 | e1a79a87 / cda22852 | `bankedroms` 0x2000000 | idem |
| glg1fl3l.4 / 3u.5 | 2 Mo ×2 | 16ddc2cb / d90d72d1 | `bankedroms` 0x3000000 (4 Mo) | flash (SIMM) |
| glg1fl4l.6 / 4u.7 | 2 Mo ×2 | 820d8133 / 02d78160 | `bankedroms` 0x3400000 | flash |
| glg1fl5l.8 / 5u.9 | 2 Mo ×2 | 090b2508 / 2231a07c | `bankedroms` 0x3800000 | flash |
| glg1vera.11s | 512 Ko | 5c33f240 | `sub` (H8/3002), `ROM_LOAD16_WORD_SWAP` | firmware du MCU I/O |
| glg1wave0.1 / wave1.13 | 8 Mo ×2 | 672d4f7c / 480b0a1a | `c352` 0x0 / 0x800000 (16 Mo) | échantillons audio |

Total banked = 0x3c00000 (60 Mo) ; SHA1 complets : `work/hashes.tsv`.

## Ce que révèle la ROM principale (4 Mo, reconstruite)
- Premiers mots LE : `3c080013 3508246f 3c011f80 ac281010` = code MIPS valide (lui/ori/sw) : vecteur de reset à 0x1fc00000 OK, sens des octets confirmé.
- Chaînes : « PS-X Realtime Kernel Ver.2.5 », « PS-X ROM monitor Ver.2.4b », « COH-100 by T.H. », « SYSTEM.CNF » (0x4310), « Execute the boot file %s », « DIP SWITCH : %02X », tests pad 6 boutons, « GOLGO » (0x2a008, 0x2da88).
- **C'est un BIOS Sony PS1 adapté (Namco COH-100/COH-700) qui contient aussi le code du jeu.** Le code MIPS occupe grosso modo 0x000000–0x0D0000 (~830 Ko, ~2 650 `jr ra`). 0x0D0000–0x1F0000 : données/tables ; 0x200000–0x2FFFFF : 0xFF (vide) ; 0x300000–0x38xxxx : données ; fin à zéro.
- Pas de « PS-X EXE » trouvé en clair : le jeu n'est pas un exécutable PS1 standard dans un système de fichiers ; le BIOS pointe sur un « boot file » (à retrouver en désassemblant autour de 0x4310). À vérifier : le code est peut-être chargé/décompressé en RAM depuis la ROM banquée.
- Chaînes « BOOT » dans la banked ROM (0xe29333, 0xe63a4c) et « GLG » (0xbd3ab…) : candidats pour tables de fichiers ou tests ; non investigués.

## Régions banquées (observations brutes, non interprétées)
- 0x0000000 : début `4a000000 00000000 c81e0000…` ressemble à une table d'index/offsets ; 0x1000000 débute par un bloc 0xFF.
- 0x3000000–0x3bfffff = les 3 paires « fl » (flash) ; 0x3a00000+ vide (0xFF).
- Les entropies (7,2–7,4 bits/octet pour rom0-2) suggèrent des données **compressées** (à identifier : LZ ?) ou de la texture 16 bits ; wave0/1 ~5,6–6,0 = PCM/ADPCM C352 plausible.

## Matériel (driver, vérifié)
- Config `golgo13` = `coh700b` : CPU `CXD8661R` (XTAL 100 MHz → R3000A + GTE, c'est la valeur du driver, à confronter au « 48 MHz » du brief), RAM 4 Mo, `m_boot_hack = true` (« 12 bytes in ram are checked early in the boot sequence, how it gets into memory is not currently known »), H8/3002 @ 16,9344 MHz, C352 @ 25,4 MHz (/288).
- Carte mémoire main : `0x1f000000` w = bank (`bankoffset_w`), `0x1f080000-0x1f083fff` RAM partagée H8, `0x1f140000` EEPROM AT28C16, `0x1f700000` dmaoffset, `0x1fa00000-0x1fbfffff` fenêtre banquée 2 Mo, `0x1fc00000` ROM BIOS 4 Mo. La ROM banquée est aussi lue par DMA canal 5 (`namcos12_rom_read`).
- **Banking « altbank » (golgo13/g13knd)** : si `(data & 8) != 0` → `bankoffset = (data − 8) << 2`, sinon `bankoffset = (bankoffset & ~7) | (data & 7)` ; la fenêtre est `mainbank` (entrée = bankoffset, pages de 2 Mo). *(Formule telle que relevée par l'agent de lecture ; à re-vérifier dans le source avant de coder.)*
- H8 (`sub`) : ROM 0x000000-0x07ffff, RAM partagée 0x080000, C352 0x280000, entrées 0x300000 (`in_r`) ; ADC1 = viseur X : `(gun0*0x240)/0x1ff + 0x7a` ; ADC2 = viseur Y : `(gun1*0x1bf)/0x1ff + 0x1f`. Les ports GUN0/1 sont `IPT_LIGHTGUN_X/Y` (pointeur absolu, valeurs 0..0x1ff) → mapping souris/gyro direct.
- Contrôle du H8 : le vecteur de reset est `0x100`, la ROM contient des chaînes « namco ltd. », « I/O CYBER LEAD », « TSS-I/O », « MIU-I/O » → c'est le firmware I/O standard Namco (même firmware que d'autres S12 à adapter), donc option (a) interpréteur H8 réaliste.

## Corrections au brief
- Chemin du driver confirmé : `src/mame/namco/namcos12.cpp`, BSD-3-Clause.
- Année : 1999 (GAME 1999, `golgo13`), g13knd = 2000.
- CPU : le driver donne XTAL 100 MHz (diviseur PSX) ; le « 48 MHz » reste à trancher, sans importance pour une recomp.
- Aucun CHD, aucun BIOS externe, pas de KEYCUS pour ce set.

## Prochaines étapes proposées (pas commencées)
1. Cloner les sources MAME **dans `third_party/mame`** (shallow) pour lire `namcos12.cpp`, `c352.cpp`, `h8` et `psx` ; vérifier la formule de banking.
2. Désassembler (venv local + capstone) le BIOS de 0x0 à 0xD0000 : trouver le point d'entrée du jeu, ce qu'il charge depuis `bankedroms` et comment (DMA, décompression).
3. Identifier les formats des banques (index, compression, textures) pour décider quoi recompiler vs interpréter.
4. Choisir la base de recomp (PSXRecomp vs écriture maison) après lecture de leur licence et de leur capacité à gérer ce BIOS-with-game.

## Rien n'a été installé hors du dossier
Python système 3.11 utilisé en `-I`, bibliothèque standard seulement ; aucun venv encore créé.

## Addendum — sources MAME clonées et banking vérifié
Clone sparse + shallow dans `third_party/mame` (28 Mo : `src/mame/namco`, `src/devices/{sound,cpu/h8,video}`, `src/mame/shared`).
Vérifié dans `namcos12.cpp` (lignes citées) :
- **Banking confirmé** (l.1520-1556) : `if (data & 8) bankoffset = (data-8) << 2; else bankoffset = (bankoffset & ~7) | (data & 7); mainbank.set_entry(bankoffset)`. Les entrées sont des pages de **2 Mo** de `bankedroms` (l.1232). Le jeu écrit `((val>>0x17)&6)+8` puis `(val>>0x15)&7` : l'espace banqué est donc linéaire (adresse = page×2 Mo + offset dans la fenêtre `0x1fa00000-0x1fbfffff`).
- **Chargement par DMA canal 5** (`namcos12_rom_read`, l.1310-1360) : l'offset DMA (`0x1f700000`) a le bit 31 à 1 → ROM principale (masque 0xffffff) ; sinon ROM banquée avec `offset += bankoffset * 0x200000` (mode altbank). Lecture 16 bits LE, copie directe en RAM invité. **Le chargeur du portage = reproduire cette fonction + la fenêtre mappée.**
- **Boot hack** (l.1250-1258) : à chaque reset, 12 octets de `mainrom + 0x20280` sont copiés en RAM invité à `0x10000` (physique ; RAM de 4 Mo).
- Conséquence : le code du jeu n'est probablement pas tout dans le BIOS ; il peut être DMA-é depuis les banques (d'où les overlays). À confirmer en traçant les DMA canal 5 au démarrage (prochaine étape : trace via un petit émulateur R3000 maison ou le MAME cloné si on accepte de le compiler localement).

## Addendum 2 — MAME 0.289 portable + dump RAM (résultats de trace)
Outil : MAME 0.289 (`mame0289b_x64.exe`, SHA256 vérifié) extrait dans `tools/mame/` ; `-verifyroms golgo13` = « good » avec `-rompath roms` ; 0 `<disk>` dans `-listxml` (pas de CHD, confirmé).
Scripts : `work/trace/trace.lua` (dump RAM invité 4 Mo à la frame 900 → `work/trace/ram_f900.bin`), `work/locate.py`, `work/extent.py`.
Le jeu tourne dans MAME (écran titre « CREDIT 0/2 » atteint, snapshot `work/snap/shot.png`).

**Constats :**
- **Le programme du jeu est dans la ROM principale 4 Mo, à l'offset `0x28000`, copié en RAM invité à `0x300000` (KSEG0 `0x80300000`)**. Correspondance octet pour octet sur les 0x9ed30 premiers octets, ≥96 % jusqu'à ~0x3f0000 (le reste est .data/.bss modifié à l'exécution). Les PC observés (`0x8038xxxx`) sont dans cette image.
- Octets `0x28000-0x28008` : `f1160e08 00000000` (en-tête propre au loader), puis la chaîne de build « Jan 12 2000 17:02:53 » (ROM 0x28008). Les 0x28000 premiers octets de la ROM = BIOS « System12 Hot Start! » + « Namco Ltd. » (0x20280).
- La RAM `0x000000-0x2fffff` est vide (zéro) après le boot : pas d'autre code chargé dans cette zone.
- **Les DMA canal 5 observés en jeu sont du streaming de données** (en attract : 2048 transferts de 2 Ko vers `0x803ff768`, CHCR=`0x01000201`, appelés depuis `0x8038d854..d878`). Les chargements de boot n'ont pas pu être captés (les points d'arrêt s'installent après le boot ; les « taps » Lua n'ont rien remonté, cause non élucidée).
- Pas de code MIPS chargé depuis les ROM banquées dans cette session de 900 frames : le code principal est donc **statique** (1 image ≈ 0xE8000+ octets) ; les ROM banquées contiennent les données (graphismes/modèles/etc.). Overlays éventuels (modes de jeu/niveaux) à rechercher en jouant plus longtemps.

## Addendum 3 — Recherche d'overlays (session jouée, ~11 700 images, 200 s)
Script `work/trace/overlay.lua` (pièce, start, tir et Enter injectés) ; journal `work/trace/overlay_log.txt` ; captures `work/snap2/golgo13/` (écran titre puis scènes de jeu avec cible/munitions : la partie démarre réellement).
- **Aucun overlay de code observé** : la zone texte RAM `0x300000-0x39ed30` reste identique à la ROM à chaque point de contrôle (1 seul mot diffère à partir de l'image ~6900, offset `0x9ed08` = donnée en fin d'image, pas du code).
- Pas de nouvelle page de code hors `0x300000-0x3f0000` : les seules pages avec des `jr ra` sont `0x000000-0x008fff` (noyau PS1 du BIOS recopié en RAM basse, stable de la frame 300 à la fin).
- Limite : une seule séquence de jeu (début de niveau + attract). Les autres niveaux/modes (boss, continue, test mode, ranking) n'ont pas été exercés ; si l'un charge du code, cela se verra par un `textdiff` croissant ou de nouvelles pages de code dans ce même journal.
- Observation à vérifier pour le GPU : la scène de jeu capturée (foule en vue aérienne) ressemble à de l'image/vidéo précalculée plutôt qu'à de la 3D temps réel ; il faudra déterminer si les fonds sont des textures statiques ou un flux vidéo.

## Addendum 4 — Désassemblage de l'image du jeu
Outils : venv local `.venv/` (capstone 5.0.7) ; scripts `work/disasm/analyze_image.py`, `work/disasm/disasm_image.py` ; sortie `work/disasm/game_text.s` (6,3 Mo, balayage linéaire annoté avec `func_XXXXXXXX` pour les cibles de `jal` et `sub_XXXXXXXX` pour les prologues).
- **Image** : ROM `0x28000` → RAM `0x80300000`, ~0xF0000 octets. **Point d'entrée = `0x80385bc4`** (le premier mot de l'image est `j 0x80385bc4`).
- **crt0** : efface la BSS `0x803bbc58 → 0x803e13d0`, fixe `$sp` à partir d'une table en `0x80385c70`, puis démarre le jeu.
- **Code** ≈ `0x8030b000 – 0x803a0000` (~620 Ko) ; `0x80300000-0x8030afff` : en-tête + données/chaînes (Shift-JIS) ; `0x803a0000-0x803bbc58` : données (les `jal` apparents y sont de fausses détections).
- ~1 300 débuts de fonction par cibles de `jal` (+ ~1 190 prologues `addiu sp,sp,-N`) ; ~6 300 `jal`.
- Compteurs sur la plage de balayage (inclut quelques faux positifs en zones de données) :
  - **GTE** : ~2 100 instructions COP2 + ~1 000 `lwc2/swc2` → **le GTE doit être implémenté en C (fidèle, saturation/flags)** ; la 3D est calculée côté CPU.
  - ~2 090 accès non alignés (`lwl/lwr/swl/swr`) → à émuler exactement.
  - ~165 `jalr` + ~200 `jr` non-ra → appels indirects/switch : tables à résoudre ou repli sur interpréteur/dispatch dynamique.
  - 3+9+3 stubs d'appel noyau A0/B0/C0 (`addiu t2,zero,0xa0/b0/c0`), ~18 `syscall`, ~54 `break` (probablement les contrôles de division).
  - ~27 instructions COP0 (SR/interruptions), quelques `cache`-like.
- Conséquence : le noyau PS1 en RAM basse (copié du BIOS) reste nécessaire : soit on le recompile aussi (ROM `0x0-0x28000`), soit on le remplace par du HLE (A0/B0/C0, événements, exceptions).
- Limite : balayage linéaire sans analyse de flot ; frontières code/données non finalisées.

## Addendum 5 — Analyse de flot (descente récursive + validation dynamique)
Scripts : `work/disasm/flow.py` (analyse), `work/disasm/check_targets.py` (comparaison avec la trace MAME), `work/trace/gen_bps.py` + `work/trace/jr.txt` (points d'arrêt sur les `jr`/`jalr` indirects, journal `tools/mame/debug.log`), `work/trace/input.lua` (pièce/start/tir). Sorties : `work/disasm/flow.json` (code, fonctions, tables de saut, `jr` non résolus), `work/disasm/dyn_targets.json`, `work/disasm/kernel_calls.txt`.

Méthode : racine = entrée `0x80385bc4` ; suivi des `j/jal/branches` (créneaux de délai inclus) ; tables de saut par propagation de constantes (`lui`/`addiu`/`ori` + `addu` + `lw` + borne `sltiu`) ; tableaux de pointeurs de fonctions dans les données ; prologues orphelins ; puis **cibles indirectes réelles observées dans MAME** (315 000 transferts `jr/jalr`, 73 sites, 203 paires site→cible, 184 cibles) ajoutées comme racines.
- **Couverture : 91,7 % de la plage `0x80308000-0x803a0000`** (142 737 / 155 648 mots), **~1 900 fonctions**, **135 tables de saut résolues**. Le reste = données (en-tête `0x80308000` 11 Ko, tables/chaînes, `0x8039ea0c`…).
- **Validation** : avant intégration des cibles dynamiques, 96 des 181 cibles indirectes observées dans le texte n'étaient pas dans le code statique → le jeu utilise beaucoup d'appels par pointeurs de fonction/tâches ; après, 0 manquante, et **0 écart** entre les tables de saut résolues et les cibles réellement empruntées. 14 cibles sont au milieu de code déjà connu (entrées multiples/étiquettes).
- **`jr` non résolus : 20** → 15 sont des **stubs d'appel noyau BIOS** (`addiu t2,zero,0xa0/b0/c0 ; jr t2` avec n° de fonction en `t1`) ; 5 sont des `jr` calculés/retours non standard (`0x80367088, 0x80367140, 0x80367284, 0x80367ff4, 0x803781ac` ; le premier a pour cibles dynamiques `0x80345c18/28/38`, retour via `$t0`).
- **Surface d'API BIOS utilisée** (appelée par les stubs, noms indicatifs à vérifier) : A0 : 0x39, 0x49, 0x72 ; B0 : 0x07 DeliverEvent, 0x08 OpenEvent, 0x0a WaitEvent, 0x0b TestEvent, 0x0c EnableEvent, 0x17/0x18/0x19 (exceptions), 0x35 ; C0 : 0x02, 0x03 (SysEnq/DeqIntRP), 0x0a. C'est très petit : le HLE du noyau est envisageable, ou on recompile le noyau de la ROM `0x0-0x28000`.
- **Conséquences pour la recompilation** : (1) il faut un **dispatch dynamique** pour `jalr` et `jr` indirects (table adresse invité → fonction C générée) avec repli interpréteur ; (2) les 184 cibles dynamiques + 135 tables fournissent un ensemble de départ solide ; (3) la couverture dynamique n'a été exercée que sur ~90 s de jeu (début de niveau) : d'autres chemins peuvent exister (à compléter par d'autres sessions, y compris autres niveaux).

## Addendum 6 — Couverture sans rejouer : l'analyse statique suffit
Constat : les sessions de jeu automatisées (visée aléatoire) perdent vite et ne dépassent pas le début du jeu ; elles ont été arrêtées. À la place, l'analyse statique a été améliorée et **validée contre les 184 cibles indirectes observées lors de la session 0** (`work/trace/logs/run0.log`), sans jamais injecter ces cibles dans l'analyse.
- `flow_static.py` (entrée + `jal` + tables de saut + tableaux de pointeurs + prologues orphelins) : 91,3 % du texte, **16 cibles observées manquantes** (fonctions feuilles GTE sans prologue, adresses construites par `lui`+`addiu`).
- `flow_static3.py` = + **constantes `lui`+`addiu/ori` pointant dans le texte** (310 nouvelles racines) : **95,6 % de couverture, 0 cible observée manquante**. C'est l'analyse de référence (`work/disasm/flow_static3.json`).
- Reste ~4,4 % = principalement données/chaînes (ex. `0x80308000+`, tables) et quelques blocs isolés (`0x803827cc`, `0x80360d30`, `0x80367448`…) à classer.
- **Contrainte utilisateur : aucun interpréteur/émulateur R3000 dans le runtime, uniquement du code natif généré** (le « repli interpréteur » du brief est écarté). Conséquence : toute adresse de code potentiellement atteignable doit être traduite statiquement. Plan : (1) traduire en C **toute la plage de texte décodable** (y compris les ~4,4 % non couverts qui ressemblent à du code), avec une étiquette/bloc par adresse d'entrée plausible ; (2) `jr/jalr` indirects → table de dispatch adresse invité → bloc natif ; (3) cible inconnue = erreur explicite journalisée (jamais d'émulation), ce qui permet de compléter les tables ; (4) pas de code auto-modifiable observé (texte identique à la ROM pendant la partie). Il n'est pas nécessaire de « jouer » le jeu pour recompiler.

## Addendum 7 — Décision : H8/3002 aussi en natif
Décision utilisateur : tout en code natif, pas d'émulateur (CPU principal **et** MCU). Pour le H8/3002 on **recompile statiquement son firmware** (`glg1vera.11s`, 512 Ko, vecteur de reset `0x100`) en C, au lieu d'un interpréteur ou d'un HLE réécrit : le firmware contient le pilote son et le protocole d'E/S (RAM partagée `0x1f080000`, ADC viseur, C352), qu'on conserve ainsi sans les rétro-concevoir. Il faudra modéliser nativement les périphériques on-chip utilisés (ADC, timers, SCI, ports, contrôleur d'interruptions) et le C352 (échantillons des ROM `glg1wave0/1`).
Ressources : MAME fournit `src/devices/cpu/h8/h8d.cpp` (désassembleur) et `h8.lst` (sémantique, 407 entrées) en BSD-3-Clause ; capstone ne gère pas le H8 → décodeur H8/300H à écrire en Python à partir de ces fichiers.

## Addendum 8 — Phase 1 terminée (recompilateur R3000 + GTE)
Voir PLAN.md §10. Code : `tools/recomp_mips/`, `runtime/`, `tests/`, `tools/check.py`. Génération : 28 291 blocs / 149 233 instructions, compilation propre, 0 écart sur 71 700 exécutions vs Unicorn, 16/16 tests de sémantique R3000, GTE identique à MAME sur 300 000 commandes aléatoires.

## Addendum 9 — Phase 2 : démarrage natif complet (BIOS + noyau + jeu)
Voir PLAN.md §10 (Phase 2). Le BIOS, le noyau et le jeu s'exécutent en C natif sur le runtime ; l'état RAM coïncide avec MAME (noyau 100 %, code du jeu 100 %, données 99,89 %) jusqu'à l'attente du H8 (handshake 0x3163 -> 0x7601 en 0x1f083002). Prochaine étape : le H8 en natif.

## Addendum 10 — H8/3002 recompilé en natif ; le jeu tourne 3 600 images
Voir PLAN.md (Phase 5a). Firmware H8 -> C (15 240 blocs), périphériques on-chip modélisés d'après MAME ; le jeu franchit les handshakes H8 et dessine (5,6 M mots GP0 en 60 s émulées) ; RAM à 99,9 % de MAME (zone basse) aux images 3600/5400.

## Addendum 11 — GPU logiciel : le jeu s'affiche (écran titre, démo 3D)
Voir PLAN.md (Phase 3). `runtime/gpu/gpu.c`, test `tests/gpu_test.c`, captures par `--shot`.
