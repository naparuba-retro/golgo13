# Golgo 13 : Juusei no Requiem (g13jnr, 2001) — analyse de faisabilité

Source : `roms/g13jnr.zip` (6 fichiers `glt1vera_*.{2j,1j,2h,1h,2f,1f}`, 17 301 504 o chacun), pilote MAME `namcos10.cpp` (classe `namcos10_memp3_state`, machine `ns10_g13jnr`), sources locales dans `third_party/mame/src/` (`ns10crypt.*`, `lc82310.*`, `spu.cpp`, `namcos10_exio.*`).

## Verdict
**Faisable avec la même méthode (recompilation statique, sans interpréteur), mais c'est un nouveau matériel presque entier** : seul le cœur PS1 (R3000 + GTE + GPU) est commun avec les deux autres jeux. À peu près 5 chantiers nouveaux, dont 2 lourds (décryptage du programme, MCU du lecteur MP3).

## Matériel (carte Namco System 10 « MEM(N) » + carte MP3 « MEMP3 »)
| Bloc | System 12 (jeux 1 et 2) | System 10 / g13jnr | Réutilisable ? |
|---|---|---|---|
| CPU principal | R3000 (PS1), BIOS Sony adapté en ROM | R3000, BIOS Namco « COH-100 » **dans la NAND** | recompilateur MIPS oui, BIOS à recompiler |
| GPU / GTE | GPU type PS1, GTE | idem (GPU PSX standard) | oui (à valider : tailles VRAM, registres) |
| Son | C352 + H8/3002 (firmware recompilé) | **SPU PlayStation** (24 voix ADPCM, RAM SPU, reverb) + **MP3** (LC82310) | non : SPU à écrire (~3 000 lignes dans MAME `spu.cpp`) ; décodeur MP3 à intégrer (domaine public type minimp3) |
| Stockage | ROM banquées 60 Mo | **6 NAND K9F2808U0B** (16 Mo, pages de 528 o = 512 + 16 d'extra, 1024 blocs) | nouveau contrôleur NAND (registres `0x1f400000`…) |
| Programme | clair en ROM | **chiffré** (décrypteur « type 2 » ns10crypt, logique propre au jeu, dans le CPLD) | à porter en C (outils + runtime) |
| Entrées | H8 : ADC du pistolet | **MCU TMP95C061 (TLCS-900)** de la carte MP3 : lit les axes du pistolet (`P3_ANALOG1/2`, 10 bits) et alimente le décodeur MP3 ; son firmware est téléversé par le jeu dans une RAM partagée (`0x1f320000`, banque `0x1f30000c`) | **nouveau CPU** : soit recompiler son firmware (nouveau recompilateur TLCS-900), soit émuler le protocole « en haut niveau » (HLE) |
| Capteur de lunette | bit `sensor` | bit « Scope Sensor » (`IN1` bit 27, actif haut) | oui |

## Ce que j'ai vérifié sur les données
* Le format NAND se lit : 32 768 pages de 528 octets par puce. Les pages `0x40`–`0x11f` du fichier 0 sont le BIOS : après dé-brouillage (permutation de bits du pilote, XOR `0xaaaa`, lecture en mots 16 bits) on obtient du code MIPS valide et des chaînes lisibles (« Borad Configuration: %02x (rev=%d,mem=%dM,smem=%cM,vmem=%dM) », « COH-100 by T.H. »). Les pages `0x120`+ (4 Mo, deuxième morceau du BIOS) restent illisibles : **chiffrées** (décrypteur de type 2 activé par le BIOS pour ces lectures).
* Le premier bloc de la NAND 0 contient la table de remplacement des blocs défectueux (`0001 03ff 0002 03fe…`, 16 bits big-endian, périphérique dans les 4 bits hauts) : nécessaire pour retrouver l'ordre réel des blocs.
* Les réglages / classements sont dans la première NAND (le pilote renvoie à l'offset `0x4200` pour MEM(N)).
* Pas de piste son stockée en MP3 dans une ROM séparée : les flux MP3 viennent de la NAND via le MCU.

## Plan proposé (ordre conseillé)
1. **Outils de données** : lecteur NAND (dé-brouillage, table de blocs défectueux, assemblage des 6 puces), décrypteur type 2 en Python puis C (port de `ns10crypt.cpp`, 376 lignes, avec la table de `ns10_g13jnr`). Résultat : BIOS complet et programme du jeu **en clair** hors ligne.
2. **BIOS + chargement** : recompiler le BIOS (MIPS) comme pour les autres jeux ; le runtime fournit le contrôleur NAND + décrypteur au moment où le BIOS charge le jeu en RAM ; une fois le jeu en RAM, extraire l'image pour la recompilation statique (même méthode que pour les deux premiers jeux : tout mot du texte = point d'entrée).
3. **Machine System 10** dans le runtime : cartes mémoire, registres d'E/S (`IN1`, `SYSTEM`, `ctrl_reg`), interruptions, GPU/GTE réutilisés.
4. **Son** : SPU PlayStation en C (le plus long à écrire proprement) ; **MP3** : décodeur + protocole du MCU.
5. **MCU / pistolet** : choix à faire avec toi — (a) HLE du protocole (il faut d'abord comprendre ce que le jeu écrit dans la RAM partagée), ou (b) recompiler le firmware TLCS-900 (nouveau recompilateur, mais conforme à la règle « tout natif »). Je recommande (a) d'abord, après avoir lu le firmware.
6. **Sélecteur de jeu** : passer `g13jnr` en « jouable » ; ROM déjà reconnue par le chargeur (`roms/g13jnr.zip`) — il restera à ajouter la table de fichiers NAND (taille/CRC32 des 6 puces, déjà connus).
7. **Traduction** : comme pour le second jeu (nouveau jeu japonais, nouveaux textes).

## Risques
* Le décrypteur type 2 est écrit pour ce jeu précis dans le pilote (tables `eMask/dMask`) ; il doit être reproduit au bit près (le pilote MAME est la seule référence).
* Le SPU de MAME est long ; un SPU « suffisant pour ce jeu » demandera de la validation à l'oreille.
* Le MCU du lecteur MP3 est une inconnue de protocole tant que son firmware n'est pas lu (il est dans les données du jeu, téléversé au démarrage).
* L'ordre exact de chargement des blocs par le BIOS (zones chiffrées, bascule `crypto_switch_w`) doit être lu dans le BIOS recompilé.

## Estimation grossière
Étapes 1-3 : un premier démarrage (logo, écrans) est réaliste ; l'étape 4-5 (son + pistolet) concentre le travail restant. Rien dans cette liste n'oblige à un interpréteur.

## État d'avancement (étapes 1 et 2 faites)
* **Étape 1 (données)** : `tools/jnr/nand.py` (NAND 6 puces, dé-brouillage, décrypteur type 2 en Python), `tools/jnr/prepare.py` (produit `work/g13jnr/maincpu.bin` = ROM du BIOS et `game.bin` = programme du jeu **déchiffré**, 693 248 octets à `0x80010000`, point d'entrée = cible du `j` du premier mot, `0x8008bf50`). Le programme est stocké en puce 0, blocs 1023→1015 puis 10→43, déchiffré en flux (iv 0) ; ordre trouvé en suivant le flux contre un cliché RAM de MAME (MAME 0.289 local : `tools/mame`, scripts dans `work/jnr/`).
* **Étape 2 (BIOS + chargement)** : outils de recompilation généralisés (`GOLGO_GAME=g13jnr`, images `game`/`bios`/`kern` dans `tools/recomp_mips/images.py`, la tranche noyau est la même qu'en System 12), code généré dans `generated_g13jnr/` (non versionné), machine System 10 dans `runtime/core/sys10.c` (contrôleur NAND + décrypteur C, registres d'E/S, I2C acquittant tout, interruption 100 Hz, table de blocs `0x1f500000`, fenêtre RAM de la carte MP3), SPU PlayStation côté bus dans `runtime/audio/spu.c` (RAM son, transferts manuels/DMA, IRQ9 ; **pas de synthèse**), SIO, DMA 4/5.
* **Résultat** : le BIOS (recompilé, natif) charge le jeu depuis la NAND en le déchiffrant, le jeu démarre et affiche le logo namco puis l'attente (« 年齢不明 / 名前不明 », « 総資産額 $1,100,000,000以上 »). Il se bloque ensuite à l'initialisation du pistolet (`GUN INITIALIZE`, boucle en `0x80029974` sur l'octet `gp+48`) : cette routine attend un échange avec le MCU de la carte MP3, absent pour l'instant (étape 5).
* Les deux jeux System 12 donnent exactement les mêmes sorties qu'avant (3000 et 6000 images).
* Pour essayer : `golgo13_dev.exe --game g13jnr --import roms/g13jnr.zip` puis `--game g13jnr`.

## Suite
Étape 3 (finir la machine : entrées, boucle d'attente du MCU), étape 5 (MCU : HLE du protocole, lecture de son firmware dans la RAM partagée `0x1f320000`), étape 4 (SPU voix + MP3).

## Mise à jour : le jeu est jouable (sans son)
Le blocage du pistolet n'en était pas un : la routine d'initialisation attend un rappel d'interruption du jeu, qui s'exécute normalement (les captures montrent l'attente, les classements, la démo, puis après une pièce et Start : tutoriel du fusil, choix d'histoire, briefing). Les valeurs du pistolet sont lues par le jeu dans la fenêtre de RAM partagée de la carte MP3, à `0x1f33fff0`–`fff6` (banque 63), où le firmware du MCU TLCS-900 écrit les 4 canaux ADC sous la forme `(v<<6)|(v>>4)` ; je les fournis directement (HLE, aucun interpréteur) : canal 0 = Y inversé, canal 1 = X. Vérifié avec le viseur interne du jeu (`S10XHAIR=1`) : la visée suit la souris à ±2 %.
Reste : son (voix SPU + décodeur MP3 alimenté par les données que le jeu envoie dans les banques de la RAM partagée), enregistrement des réglages (NAND bloc 1), traduction (textes japonais, comme le second jeu).

## Mise à jour : son
* **SPU** (`runtime/audio/spu.c`) : 24 voix ADPCM (filtres, interpolation linéaire), enveloppes ADSR, volumes, KON/KOFF, ENDX ; pas encore de réverbération, de modulation de pitch ni de bruit. Sorti à 44 100 Hz, doublé vers le flux 88 200 Hz du port.
* **MP3** (`runtime/audio/mp3.c`, minimp3 CC0 dans `third_party/minimp3/`) : le jeu envoie ses pistes dans la RAM partagée de la carte MP3 et la pilote par des commandes d'une lettre (mot 0 de la banque 63, arguments aux mots suivants, déclenchées par l'écriture de `0x1f300006`) : `V` volume, `C`, `L`, `A`, `P` lecture, `S` arrêt. Table des pistes au mot 8 : 12 entrées `{offset, taille}` (offsets dans la RAM de la carte). **Hypothèses à confirmer à l'oreille** : `A n` choisit la piste n (`A 0` = ne rien changer), `P` lance la piste choisie (0 par défaut), les pistes de plus de 0x70000 octets sont en boucle, `V 8` = volume plein.
