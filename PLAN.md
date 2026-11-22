# Plan — Portage natif de Golgo 13 (arcade, Namco System 12)

## 1. Objectif et contraintes (décidées avec l'utilisateur)
- Version **native et jouable** (Linux x86-64, cible Steam Deck ; Windows en option) de *Golgo 13* (GLG1/VER.A, 1999).
- **Recompilation statique** : pas besoin d'un binaire identique ni d'un code C lisible.
- **Aucun interpréteur/émulateur de CPU dans le runtime** (ni R3000, ni H8) : uniquement du code natif généré. Une cible de saut inconnue = erreur explicite journalisée, jamais d'émulation.
- Code livré en **C11** (CMake, SDL, OpenGL/Vulkan). **Python** réservé aux outils d'analyse/génération (non livrés). C++ seulement si une bibliothèque l'impose, en module isolé.
- **Tout reste dans `C:\SOURCES\golgo`** (venv `.venv/`, MAME portable `tools/mame/`, sources MAME en sparse dans `third_party/mame/`). Aucune installation globale.
- **ROM fournies par l'utilisateur, jamais redistribuées ni embarquées** (écran « fournis tes ROM »).
- Entrées : le jeu attend un pointeur absolu → souris / gyroscope / trackpad ; les light guns modernes arrivent comme périphérique de pointage absolu. Pas de pistolet disponible pour l'instant.

## 2. État actuel (analyse faite, aucun code de recompilation écrit)
Détails : `reports/01-rom-inventory.md` (Addendums 1-7).
- 17/17 ROM conformes au `ROM_START golgo13` ; pas de CHD, pas de BIOS séparé, pas de KEYCUS.
- **Programme du jeu** : dans la ROM principale 4 Mo, offset `0x28000`, copié en RAM invité à `0x80300000` (taille ~0xF0000), **entrée `0x80385bc4`**. Aucun overlay de code observé. Texte identique à la ROM en jeu (pas d'auto-modification vue).
- Les 0x28000 premiers octets de la ROM = BIOS Sony adapté (« System12 Hot Start! », noyau PS1) recopié en RAM basse.
- Ressources : ROM banquées (60 Mo de données, DMA canal 5, banking « altbank »), ROM son C352 (16 Mo), firmware H8/3002 (512 Ko, reset `0x100`).
- Analyse de flot : `work/disasm/flow_static3.py` → **95,6 % du texte** `0x80308000-0x803a0000`, ~1 900+ fonctions, 135 tables de saut, 0 des 184 cibles indirectes observées dans MAME manquante.
- Surface noyau BIOS utilisée : très petite (appels A0/B0/C0 : événements, exceptions, SysEnq/DeqIntRP…).
- Statistiques du code : ~2 100 instructions GTE + ~1 000 lwc2/swc2, ~2 090 accès non alignés (lwl/lwr/swl/swr), ~165 jalr, ~200 jr non-ra.

## 3. Architecture cible
```
tools/           outils Python (non livrés) : analyse, décodage, génération de C
  recomp_mips/   R3000 -> C
  recomp_h8/     H8/300H -> C
runtime/         bibliothèque C11 livrée
  core/          mémoire invité, bus, dispatch des sauts indirects, erreurs de saut inconnu
  gte/           GTE (d'après MAME gte.cpp, BSD-3, mention de copyright conservée)
  gpu/           GPU PS1 (VRAM 2 Mo), rasteriseur logiciel puis OpenGL
  dma/ irq/ rcnt/  DMA (dont canal 5 + banking altbank), interruptions, compteurs
  kernel/        noyau BIOS : recompilé ou remplacé par du C (A0/B0/C0)
  h8/ c352/      firmware H8 recompilé + périphériques on-chip ; modèle C352
  io/            entrées (souris/gyro -> ADC viseur ; boutons -> registre 0x300000)
  platform/      SDL : fenêtre, audio, entrées
generated/       C produit par les outils (non versionné si dérivé des ROM)
roms/            ROM utilisateur (jamais livrées)
```
Contrainte légale : le C généré dérive des ROM → **non redistribuable** ; l'utilisateur le génère localement à partir de ses ROM (le recompilateur est livré, pas ses sorties).

## 4. Phases (chacune finit par un test vérifiable)
1. **Recompilateur R3000** (Python → C). Un bloc de base = une étiquette ; créneaux de délai, branches « likely » absentes (R3000), accès non alignés exacts, GTE via `runtime/gte`, overflow/trap `add/addi`, `break` (division), `syscall`, COP0 minimal. Dispatch `jr/jalr` par table adresse invité → bloc natif (hachage + table dense). Étend la couverture au texte décodable restant (~4,4 %). *Test :* le C généré compile sans avertissement bloquant ; table de dispatch complète.
2. **Runtime minimal + démarrage.** Mémoire invité (4 Mo RAM, fenêtres ROM `0x1fc00000` et banque `0x1fa00000`), DMA canal 5 (`namcos12_rom_read` : bit 31 = ROM principale, sinon banque + `bankoffset*2 Mo`), copie de l'image du jeu, boot hack (12 octets de `mainrom+0x20280` → RAM `0x10000`), stubs GPU/son/H8. *Test :* le jeu atteint sa boucle principale ; comparaison de la RAM avec les dumps MAME (`work/trace/ram_f900.bin`, script `trace.lua`) à des images données.
3. **GPU.** Listes de primitives PS1 + transferts VRAM (2 Mo), d'après MAME `video/psx.cpp` (BSD-3). D'abord rasteriseur logiciel pour valider pixel-à-pixel contre captures MAME, puis OpenGL. *Test :* écran titre identique ; ensuite scènes de jeu. Question ouverte : fonds de niveau = textures statiques ou flux vidéo.
4. **Entrées.** Souris/gyro → ADC1/ADC2 (`x = gun0*0x240/0x1ff + 0x7a`, `y = gun1*0x1bf/0x1ff + 0x1f`), boutons → `0x300000`. *Test :* lancer une partie, viser et tirer.
5. **H8 natif.** Décodeur H8/300H (Python, d'après MAME `h8d.cpp`/`h8.lst`, BSD-3), analyse de flot du firmware `glg1vera.11s`, recompilation en C, périphériques on-chip utilisés (ADC, timers, SCI, ports, contrôleur d'interruptions) ; JVS, RTC et « settings » en stubs minimaux (hypothèse à vérifier sur le firmware) ; RAM partagée avec le jeu. Modèle C352 sur les échantillons `glg1wave0/1`. *Test :* sons et musique corrects.
6. **Noyau BIOS.** Selon ce que montre la phase 2 : recompiler le noyau de la ROM `0x0-0x28000` ou le remplacer par du C (A0/B0/C0, événements, exceptions, handlers d'IRQ).
7. **Finition et emballage.** Réglage Steam Deck, écran « fournis tes ROM » (vérification CRC/SHA1 du set), AppImage ou Flatpak, éventuellement Windows.

## 5. Validation transversale
- MAME 0.289 (`tools/mame/`) = **référence locale non livrée** : comparaison d'écrans, de RAM et d'événements (scripts Lua dans `work/trace/`).
- Tests unitaires du GTE contre MAME (mêmes vecteurs d'entrée).
- Le jeu n'a pas besoin d'être « rejoué » pour la couverture de code ; la couverture dynamique ne sert qu'à contrôler.

## 6. Risques
- **GPU** : VRAM 2 Mo, fidélité aux arrondis/dither PS1 ; partie la plus longue à régler visuellement.
- **Son** : fidélité du modèle C352 et du firmware H8 ; hypothèses sur les stubs JVS/RTC/settings.
- **Démarrage (phase 2)** : détails matériels encore inconnus (ex. « 12 octets en RAM » du boot hack dont l'origine exacte est inconnue dans MAME).
- **Code non couvert** : ~4 % du texte ; sauts inconnus à compléter itérativement (par erreur explicite, pas par émulation).
- **Appels indirects** : ~200 `jr/jalr` à surveiller ; 5 `jr` non standard à traiter (`0x80367088, 0x80367140, 0x80367284, 0x80367ff4, 0x803781ac`).

## 7. Licences
- Code MAME réutilisé : **BSD-3-Clause** (smf, O. Galibert) → conserver les mentions de copyright ; ne pas utiliser la marque « MAME ». Préférer réécrire à partir du comportement plutôt que traduire ligne à ligne.
- Outils de recomp PS1 externes (psxrecomp, ps1-recomp, Tekken3Recompiled) : **référence de lecture uniquement** (PolyForm Noncommercial / GPL-3 ; pas de copie de code ; aucun ne gère le System 12 et deux ont un interpréteur de secours).
- Ne pas redistribuer ROM ni sorties dérivées des ROM.

## 8. Prochaine action
Phase 1 : écrire `tools/recomp_mips/` (décodeur complet R3000 + GTE, générateur de blocs, table de dispatch), en partant de `work/disasm/flow_static3.json`.

## 9. Chaîne de compilation (vérifiée)
Windows : **MSVC Build Tools 2022 déjà présents** (`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`, via `vcvars64.bat`) → aucune installation. Wrapper : `tools/build/cl.bat <args cl>` (test C11 `/std:c11 /O2` OK). Un seul appel `cl` pour tous les `.c` ; chemins Windows (`C:/...`) et guillemets autour de `Program Files (x86)`. Linux/Steam Deck : gcc/clang + CMake. CMake/Ninja si besoin : venv ou `pip install --target` dans le projet (pas `--user`). Conseil de la session « Portage C du nes » : sous Windows éviter SDL si inutile (GDI/waveOut/XInput) — à reconsidérer ici (SDL prévu pour Linux/Deck).

## 10. État d'avancement
### Phase 1 — recompilateur R3000 : FAITE (2026-11-17)
- `tools/recomp_mips/recomp.py` : image du jeu -> C11. **28 291 blocs, 149 233 instructions**, 24 fichiers `generated/game_NNN.c` (~16 Mo) + table de dispatch `game_table.c` (`game_lookup(pc)`). Un bloc = une fonction `uint32_t b_XXXXXXXX(cpu_t*)` qui renvoie le prochain PC. Aucun interpréteur.
- Sémantique implémentée d'après MAME `psxcpu` : créneaux de délai de branche (condition/cible lues avant, lien écrit avant), **délais de chargement** (l'instruction suivante lit l'ancienne valeur, l'écriture de la même registre l'emporte, `lwl/lwr` voient la valeur en attente), accès non alignés, débordements `add/addi/sub`, `div/divu` (cas /0 et INT_MIN/-1), `break/syscall`, COP0, COP2.
- `runtime/gte/` : GTE en C (port de MAME `gte.cpp`, BSD-3, (c) smf).
- Contrat code généré / runtime : `runtime/include/rt.h` (mémoire, COP0, exceptions, GTE, `rt_unknown_pc`).
- **Vérifications (toutes passent ; script : `.venv/Scripts/python tools/check.py`)** :
  1. le C généré compile sans avertissement (MSVC `/std:c11 /O2`, `/W4` sur un échantillon) ;
  2. test différentiel contre **Unicorn (MIPS32)** : 71 700 exécutions de blocs réels sur états aléatoires, 0 écart (le test détecte bien un bug injecté : 220 écarts) ;
  3. 16 tests dirigés des particularités R3000 (délais de chargement, div, débordement, liens) ;
  4. GTE vs `gte.cpp` de MAME : 300 000 commandes aléatoires, 0 écart (13 953 avec un bug injecté).
- Limites connues : l'oracle Unicorn n'exerce pas les blocs avec COP0/COP2/syscall/break/div/overflow/dépendance de chargement (couverts seulement par les tests dirigés et le test GTE) ; 194 mots du « code » de l'analyse sont en fait des données et deviennent des exceptions RI (jamais exécutés) ; 47 cibles de saut hors code (`unknown_leaders`) ; 1 chargement en créneau de branche et 1 branche en créneau (signalés dans `generated/game_report.json`) ; les blocs sont reliés par une boucle de dispatch (pas encore de chaînage direct ni de cache de registres : optimisation ultérieure).
### Prochaine : phase 2 (runtime minimal + démarrage), dont le noyau BIOS (RAM basse) et le chargement ROM/DMA.

### Phase 2 — runtime minimal + démarrage : FAITE jusqu'à la première attente du H8 (2026-11-17)
- **Tout le code MIPS est natif, BIOS et noyau compris** : 3 images recompilées (`tools/recomp_mips/images.py`) : `game` (RAM 0x80300000, 28 260 blocs), `bios` (ROM 0xBFC00000..+0x28000, 40 960 points d'entrée, blocs plafonnés à 40 instr.), `kern` (noyau en RAM basse 0x80000000.., reconstitué à partir de segments de la ROM — la carte des recopies vient de la comparaison ROM / dump RAM MAME). Pour BIOS et noyau, **chaque mot de code est un point d'entrée possible** (blocs de repli) : zéro « cible inconnue » sur ces images. Analyse de flot générique : `tools/recomp_mips/flowlib.py` (+ `analyze.py`, `gen_all.py`).
- **Runtime C** (`runtime/core/`) : mémoire (RAM 4 Mo + fenêtres/miroirs selon `ram_config`, scratchpad, ROM BIOS en lecture seule, **banque ROM altbank**, RAM partagée H8, EEPROM, registre `dmaoffset`), COP0/exceptions/interruptions, contrôleur d'IRQ, **DMA** (canaux 2 GPU liste/bloc, 5 lecture ROM, 6 OTC ; MDEC ignoré), compteurs racine, état GPU (rendu = phase 3), chargeur de ROM **avec vérification CRC32** (`rom_load`), boucle principale par blocs, trace des derniers blocs.
- **Résultat** : `build/golgo13.exe --roms roms/golgo13 --frames 900` : le BIOS démarre, copie le noyau et le jeu, initialise tout, active les interruptions, puis le jeu attend la réponse du H8 — **sans aucune erreur ni cible inconnue**. 900 images en ~2,4 s.
- **Validation** (`tests/boot_test.py`, dans `tools/check.py`) : RAM à l'image 900 comparée à un dump MAME à la même image : noyau 100 % (23 862 octets non nuls), programme du jeu 100 % (474 004), données/BSS 99,89 % (66 482), piles 98,7 %. Dans MAME aussi le jeu attend longuement le H8 à ce stade, donc les états coïncident.
- **Bloquant pour avancer : le H8.** Le jeu écrit `0x3163` en `0x1f083002` (RAM partagée) et attend `0x7601` ; le firmware H8 écrit aussi une chaîne d'identification, des entrées, etc. (trace MAME : ~4 000 écritures H8 dans la zone partagée visible, 7 M lectures côté jeu en 6 s). Pas de raccourci raisonnable : **la phase 5 (H8 recompilé en natif) passe avant le GPU**.
- À traiter ensuite : **MDEC** (le jeu lance des DMA canal 0 : vidéo FMV, ex. fond de niveau), écriture 16 bits non mappée en `0x1f788000`, SIO stub, EEPROM non persistante.

### Phase 5a — H8/3002 en natif : FAITE (2026-11-17), le jeu démarre et tourne avec le vrai firmware H8
- **Outils** (`tools/recomp_h8/`) : `h8dec.py` (décodeur H8/300H d'après la table générée de MAME `h8make.py h8.lst d h`), `flow.py` (vecteurs d'interruption + descente récursive + constantes pointeurs ; racines supplémentaires dans `work/h8/extra_roots.json`, découvertes par `discover.py`), `gen.py` (H8 -> C ; chaque adresse paire de `0x100-0x7800` est un point d'entrée possible : **15 240 blocs**, plus aucune « adresse inconnue » possible dans le code H8). Sémantique des 120 instructions d'après `h8.lst` ; les fonctions ALU/décalages/rotations sont **le portage mécanique du `h8.cpp` de MAME** (`runtime/h8/h8_alu.h`, 67 fonctions, drapeaux H/N/Z/V/C identiques).
- **Runtime H8** (`runtime/h8/h8_machine.c`) : bus (ROM, RAM partagée en vue grand-boutiste, RAM interne, entrées `0x300000`, C352 en stub), **INTC** (IRQ0-7, priorités ICR, masque UE/I/UI), **ITU 0-4** (compteurs, comparaison, débordement, interruptions), **SCI 0/1** (temps d'émission, TXI/TEI), **ADC** (scan/simple, déclenchement externe par la fin du vblank), **ports 4/6/7/8/9/A/B**, registres génériques pour le reste (journalisés). Ordonnancement : le H8 (16,9344 MHz) est cadencé sur le CPU principal toutes les 400 cycles ; IRQ1 + ADTRG + bit 7 du port B = vblank.
- **Validation** : le jeu passe les deux handshakes avec le H8 (`0x3163`/`0x7601` en `0x1f083002`, puis `0x1f083078` à l'image ~834) et **tourne 3 600 images (60 s émulées) en 11 s sans erreur** ; GPU : 5,6 M mots GP0, 3 828 listes DMA. RAM comparée à MAME aux images 3600 et 5400 (`tests/boot_test.py`) : noyau 100 %, code du jeu 100 %, RAM basse 99,9 % / 97,9 %, données 82,7 % / 96,6 %, piles 99,6 % / 99,9 %. (À l'image 1800 la RAM basse diverge : chronologie de chargement différente, convergence ensuite.)
- **Non fait / à vérifier** : C352 (son) = stub qui stocke les registres ; entrées = valeurs neutres (`0xff`, ADC à 0) ; SCI1/RTC/« settings » sans périphérique ; JVS absent ; DMAC H8, watchdog, refresh = registres génériques ; **pas encore de test instruction par instruction du H8 contre MAME** (seulement le test de bout en bout) ; les 37 racines indirectes de `extra_roots.json` sont devenues inutiles avec « toutes les adresses paires » mais restent documentées.
- **Prochaines étapes** : GPU (phase 3) maintenant que le jeu dessine ; MDEC (vidéo) ; entrées souris/gyro (phase 4) ; C352 + sortie audio ; EEPROM persistante ; test H8 instruction par instruction.

### Phase 3 — GPU : FAITE (2026-11-17), le jeu s'affiche
- **Module** `runtime/gpu/gpu.c` : GPU « type 2 » du System 12 (VRAM 1024x1024x16 = 2 Mo) en logiciel. Décodeur GP0 (FIFO, listes DMA, blocs, polylignes), GP1 (modes d'affichage), **polygones** plats/Gouraud/texturés (4, 8 et 15 bits avec CLUT, fenêtre de texture, modulation, brut), quads en 2 triangles, **lignes**, **rectangles/sprites** (variable, 1, 8, 16 px, textures, **retournement X/Y** via les bits 12/13 du mot de mode, spécifiques System 12), **semi-transparence** (4 modes), bit de masque, zone de dessin/décalage, remplissage, copies VRAM (CPU->VRAM, VRAM->CPU via GPUREAD/DMA, VRAM->VRAM). Format du mot de mode de dessin, coordonnées 10 bits et paquets d'après MAME `video/psx.cpp` ; le rasteriseur est une implémentation indépendante (fonctions d'arête, règle haut-gauche).
- **Affichage** : lecture de la zone d'affichage (15 ou 24 bits, largeurs 256/320/368/512/640, 240/480 lignes, plages H/V) vers RGBA ; `--shot N fichier.ppm` capture l'écran à l'image N (`tools/ppm2png.py` convertit en PNG) ; `--primlog N`/`--tracepix x y` journalisent les primitives / un pixel pour déboguer.
- **Validation** : (1) test de couverture (`tests/gpu_test.c`, dans `tools/check.py`) : aucun pixel doublé ou manquant sur des maillages de triangles adjacents, boîte englobante exacte (haut/gauche inclus, bas/droite exclus) — il a révélé une règle inversée qui produisait des lignes d'un pixel entre tuiles ; (2) **captures** : écran titre complet (logo, « INSERT COIN(S) », logos namco / 8ing / Raizing, mentions légales, « CREDIT 0/2 »), scène 3D de démonstration (mannequin ombré, texte japonais), « SNIPE POINT » avec réticule — comparées visuellement à des captures MAME (mêmes types de scènes ; chronologie différente donc pas de comparaison pixel à pixel).
- **Perf** : 7 200 images (120 s de jeu) en 49 s sur la machine de dev (2,4x le temps réel) ; le rasteriseur par fonctions d'arête est à optimiser (spans par ligne) avant le Steam Deck.
- **Reste côté vidéo** : MDEC (vidéo décodée, DMA canal 0 toujours ignoré), dithering absent (comme MAME), fenêtre SDL/OpenGL (phase 7), comparaison pixel à pixel avec MAME à chronologie alignée.

### Phase 4 (+ part. 7) — fenêtre SDL3 et entrées : FAITE (2026-11-17)
- **Plateforme multiplateforme** : `runtime/platform/` (API `platform.h`, implémentation SDL3 `platform_sdl3.c` ; Windows/Linux/macOS/Steam Deck). SDL3 récupéré en local par `tools/setup_sdl3.py` (paquet officiel 3.4.20, SHA-256 vérifié, `third_party/SDL3`, non versionné). Fenêtre redimensionnable 4:3 avec bandes noires, plein écran `F11`, 60 images/s mesurées (`--autoquit N`).
- **Entrées** : souris/pointeur absolu = viseur (`SDL_MouseID` par appareil : un pistolet Sinden/Aimtrak/Gun4IR s'y présente comme une souris, sélection d'appareil à ajouter) ; clic gauche/Ctrl/Espace = tir, clic droit/Maj = capteur, `1` départ, `5` pièce, `Entrée`, flèches haut/bas, `F2` service, `F1` test, `Échap` quitter ; manette : stick droit = réticule, RT/A tir, LT/B capteur, croix, Start/Back. Conversion vers les registres d'entrée du H8 d'après `in_r` de MAME (JAMMA, actif bas) ; viseur -> ADC1/ADC2 (`gunx_r`/`guny_r`).
- **Tests sans fenêtre** : `--press image:durée:bouton` et `--aim x,y` (entrées scriptées). Vérifié : pièce (CREDIT 1/2, « MORE COIN(S) »), 2 pièces + départ -> la partie démarre (entraînement, stand de tir, cible, compteur, balles).
- **Reste** : sélection du périphérique pistolet + calibrage (UI), son, MDEC, configuration des touches, Linux/Steam Deck (CMake).

### Phase 5b — son (C352) : FAIT (2026-11-17)
- `runtime/audio/c352.c` : port en C de MAME `c352.cpp` (32 voix, PCM 8 bits / µ-law, bouclage avant/arrière/lien, bruit, interpolation, rampe de volume, filtre), 88 200 Hz stéréo (sorties avant G/D, comme la carte). Branchée au bus du H8 (`0x280000-0x287fff`, accès 16 bits et octets) ; les échantillons sortent des ROM `glg1wave0/1` (16 Mo). Le chip est avancé à la demande, au temps du H8, à chaque accès registre et à chaque image (1 470 échantillons/image).
- **Sortie** : SDL3 (`plat_audio_init/push`, flux S16 stéréo 88,2 kHz, latence bornée ~100 ms) ; `--wav fichier` enregistre l'audio en mode sans fenêtre.
- **Vérification** : `tests/audio_test.py` (dans `tools/check.py`) : silence pendant le démarrage lent du H8 puis signal audible (RMS ~600-800), pas d'écrêtage. Comparaison avec l'enregistrement audio de MAME (`-wavwrite`) : même niveau moyen et même allure spectrale ; le son démarre à ~44 s d'émulation chez nous contre ~58 s dans MAME (même décalage de chronologie que pour la vidéo). **Pas encore écouté par une personne** ; pas de test différentiel C352 contre `c352.cpp`.
- **Reste** : MDEC (vidéos), réglages (volume), équilibrage/latence audio réels (Steam Deck), test différentiel C352.

### MDEC (décodeur vidéo) : FAIT (2026-11-17)
- `runtime/video/mdec.c` : port en C de MAME `mdec.cpp` (flux run-length -> déquantification -> IDCT -> YUV vers RGB 15 ou 24 bits), branché sur les DMA canal 0 (entrée : tables de quantification, table cosinus, flux à décoder) et canal 1 (sortie) et sur les registres `0x1f801820/24`.
- **Vérification** : `tests/mdec_test.cpp` (dans `tools/check.py`) compare le port au `mdec.cpp` de MAME compilé tel quel (environnement minimal dans `tests/shim_mdec/`) : 600 flux aléatoires, modes 15 et 24 bits, 0 écart. Amplitudes réalistes : hors des tables d'écrêtage le comportement est indéfini dans les deux implémentations.
- **À noter** : au cours de ~12 000 images de démonstration/entraînement (avec pièces, départ, Entrée et tir scriptés) le jeu n'a envoyé que les tables au MDEC, **aucun macrobloc n'a été décodé** ; les séquences vidéo éventuelles (introduction, cinématiques, fins) n'ont donc pas été vues et restent à valider en jeu réel.

### Accélération (2026-11-17)
- **Rasteriseur** réécrit : fonctions d'arête incrémentales avec intervalle de x calculé analytiquement par ligne (règle haut-gauche conservée : test de couverture inchangé), plans d'attributs en virgule fixe 16.16 (couleur, u, v) au lieu d'une division par pixel, boucles de spans spécialisées (plat, Gouraud, texturé), copie directe des texels quand la modulation est neutre (128,128,128 ou texture brute), palette mise en cache par primitive. Sortie quasi identique à l'ancienne (0 à 0,04 % de pixels différents sur 3 images de référence ; `tools/ppm_diff.py`).
- **Dispatch** : une seule table plate (RAM + ROM BIOS) remplie au démarrage à partir des tables générées (`code_export_all`) au lieu de trois recherches par bloc ; accès RAM inlinés dans le code généré (`RT_INLINE_MEM`, neutre en vitesse mais plus propre).
- **Mesure** (7 200 images = 120 s de jeu, machine de dev, sans fenêtre) : 49,1 s -> 28,8 s (4,2x le temps réel). Répartition approximative sur ce parcours : CPU recompilé ~2,1 ms/image (2,5 ns par instruction, ~713 M blocs pour 3 000 images, 3,5 instructions/bloc), H8 ~0,6 ms, GPU ~1 ms en moyenne, audio négligeable. Interrupteurs de profilage : `--nodraw`, `--noh8`, `--noaudio`.
- **Pistes restantes** : saut des boucles d'attente du jeu (le jeu tourne à pleine vitesse dans ses attentes de vblank/H8), chemin de transparence et de modulation du rasteriseur, transferts VRAM groupés. Pas mesuré sur Steam Deck.

## Enregistrement et rejeu de sessions (nov. 2026)
* En mode fenêtre, chaque partie est enregistrée automatiquement dans `<dossier de données>/sessions/session_AAAAMMJJ_HHMMSS.g13r` (6 octets par image : visée x/y + boutons ; l'en-tête note la langue).
  Un changement de langue redémarre le jeu et ouvre un nouveau fichier.
* `golgo13 --replay session.g13r --headless --frames N --shot F out.ppm` rejoue la session exactement (vérifié : image identique à celle de la partie jouée) ;
  les outils de capture (`--shotseq`, `--quadcap`, `--texdump`, `--ramseq`, `--primlog`) s'utilisent avec le rejeu. Sans `--headless`, la fenêtre rejoue la partie.
* Le rejeu n'est exact qu'avec le même moteur : après une modification du code d'émulation, d'anciennes sessions peuvent diverger (les changements d'affichage / de traduction ne comptent pas).

## Calibration de la visée (nov. 2026)
Mesure par rejeu (session enregistrée + marqueur d'impact) : le point visé par le jeu était étiré par rapport à l'image
(impact_x = 1,107·x − 29,8 ; impact_y = 1,020·y − 2,3 sur l'image 512×480). La fenêtre applique maintenant la transformation inverse
(`runtime/platform/platform_sdl3.c`, avant l'enregistrement : les anciennes sessions restent exactes). Contrôle après correction : écart moyen 0,8 px (x) / 1,3 px (y).
Mesuré dans la vue de la lunette du tutoriel ; à confirmer en mission (la caméra de la lunette bouge pendant les tirs).
