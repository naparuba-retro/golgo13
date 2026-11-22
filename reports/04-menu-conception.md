# Conception du menu Golgo 13 (sélecteur de jeu + options)

Statut : **implémenté (v1)** : sélecteur, options (6 onglets), pause, commandes, Wiimote, langue. Écarts avec la proposition : pas de téléchargement (polices cuites depuis les polices du système par `tools/ui/bake_font.py`, pas de stb : images au format brut `G13I` produites par `tools/ui/make_art.py`), clés `settings.ini` en noms de touches SDL, lignes inconnues du .ini non conservées. Maquettes : `work/ui/mock/*.png` (générées par `python tools/ui/mockup.py` ; elles contiennent des planches de manga de ton dossier perso, donc elles ne sont pas versionnées).
Inspiration : topo de la session TMNT (`reports/topo-menu-tmnt.md`).

## 1. Idée directrice

Un lanceur « planche de manga » : fond nuit à trame de points (comme l'écran *Story Select* du jeu), bandeau rouge incliné pour le titre, panneaux papier crème à bord noir épais et ombre dure, bulles de BD pour les explications, onglets jaunes. Les jeux sont des **cartes-cases de manga** : une case de la vraie BD (virage en bichromie par jeu), titre en Impact, tampon « ROMS OK / ROMS MANQUANTES ».

Maquettes :
1. `1_selection.png` : « Choisis ta mission » : 3 cartes (une par jeu), focus qui remonte et grossit, bulle « Entrée / A : JOUER ».
2. `2_options_image.png` : onglets à gauche (IMAGE, SON, COMMANDES, WIIMOTE, LANGUE, JEU), feuille d'options à droite, bulle d'aide pour la ligne qui a le focus.
3. `3_commandes_wiimote.png` : table d'actions (clavier + manette), carte Wiimote (état, barre, lissage, calibrer/appairer).
4. `4_pause.png` : menu de pause par-dessus le jeu figé et assombri.

## 2. Écrans et navigation

| Écran | Contenu |
|---|---|
| **Sélecteur** (au lancement, et via « Changer de jeu ») | cartes des 3 jeux, dernier jeu joué présélectionné, jeux sans ROM grisés (clic = écran d'aide « où mettre les ROM »), accès Options / langue |
| **Options** | 6 onglets (ci-dessous) |
| **Pause** (Échap en jeu) | Reprendre, Options, Changer de jeu, Réinitialiser, Quitter (confirmation) |
| **Aide ROM** | dit quel dossier / zip placer et les CRC attendus (déjà dans `loader.c`) |
| **Calibrage Wiimote** | reprend l'écran de calibrage existant, dans le même habillage |

Entrées : une seule structure `MenuInput` (haut/bas/gauche/droite, valider, retour, onglet précédent/suivant, souris, touche capturée) remplie par la boucle principale, pour que clavier, manette, souris **et Wiimote (curseur = viseur)** donnent les mêmes actions. Modèle de focus : chaque image le dessin enregistre ses éléments cliquables (rectangle/quadrilatère + action) ; le focus clavier/manette saute entre éléments voisins (petit graphe haut/bas/gauche/droite par page, leçon n°9 de TMNT), la souris teste directement les quadrilatères.

## 3. Options (et clés de `settings.ini`)

Le fichier existe déjà (`settings.ini`, dossier de données de l'utilisateur, clé `lang`). On passe au format INI complet de TMNT : **seulement les valeurs différentes du défaut**, clés inconnues conservées, écriture différée + atomique (fichier temporaire puis renommage), les options en ligne de commande gagnent.

| Onglet | Options | Clés |
|---|---|---|
| IMAGE | plein écran, échelle entière, format (4:3 original / 16:9 étiré / remplir), filtre (net / lissé), lignes de balayage CRT 0-100, FPS, vsync | `[video] fullscreen integer_scale aspect filter crt_scanlines show_fps vsync` |
| SON | volume général, coupure, volume musique MP3 / effets (jeu 3 : pistes MP3 séparées de la SPU) | `[audio] volume mute music sfx` |
| COMMANDES | 2 touches par action (tir, lunette, valider, haut, bas, start, pièce, service, test), manette (boutons + sticks), souris, « Réinitialiser » | `[keys] fire_1 fire_2 …` avec des **noms de touche portables** (`Space`, `F1`…, pas des codes SDL) ; positions physiques par défaut pour AZERTY/QWERTY |
| WIIMOTE | activer, appairer, calibrer, lissage (0-100), zone morte, viseur affiché | `[wiimote] enable smoothing deadzone` (+ `wiimote.cal` existant) |
| LANGUE | Original / Français / autres dossiers de `translations/` | `[game] lang` |
| JEU | dernier jeu, démarrer directement le dernier jeu, enregistrer les sessions (`.g13r`), dossier des ROM, afficher le viseur du jeu 3 (dip `S10XHAIR`) | `[game] last start_last record roms xhair` |

Options « triche/outils » (cachées derrière un onglet DEV, activé par `--dev`) : bot, replay, balayage mémoire : ne concernent pas le joueur.

Réglages **par jeu** possibles plus tard (ex. mode de visée, langue) : section `[g13knd]`, etc.

## 4. Technique

* **Rendu** : logiciel dans un tampon RGBA 1280×720 (repère fixe, mis à l'échelle et centré), présenté par `plat_present()` comme une image de jeu (déjà letterbox 4:3 : à étendre en « plein 16:9 » pour le menu). Pas de dépendance graphique de plus. Moteur `runtime/ui/ui.c` : quadrilatères remplis/contourés, **trame de points** (halftone), traits de vitesse, étoile « POW », texte (stb_truetype), images (stb_image), désaturation (jeu non disponible).
* **Cache** : la couche de base n'est redessinée que quand l'état change (empreinte), le contour de focus pulse seul (leçon n°1 de TMNT : le logiciel gelait le jeu sans ça). Limite de pixels si la fenêtre est énorme.
* **Mise en page** : colonnes/lignes (listes d'options pilotées par une table), pas de coordonnées en dur par ligne (leçon n°2).
* **Table d'options** `options.c` : `{clé, section, libellé (id de chaîne), description, défaut, min, max, type}` ; les pages sont des listes d'identifiants.
* **Textes** : table de chaînes par langue dès le départ (`ui_fr.ini` / `ui_en.ini`), police avec accents (leçon n°8). La langue de l'interface suit la langue de traduction du jeu.
* **Polices** : à choisir (voir §6). Le jeu utilise déjà des polices système pour les traductions (Arial, Impact) mais **ne les distribue pas**.
* **Tests** (leçon n°10) : `--menu-shots dir` (une capture BMP par page, sans fenêtre) + tests de navigation scriptés (chaque élément expose sa position) ; branchés sur le test automatique existant.
* **Intégration** : `game_menu()` et `language_menu()` de `runtime/core/main.c` deviennent des appels à `menu_run()` ; Échap en jeu ouvre la pause (la machine est figée proprement entre deux frames, l'audio coupé).

## 5. Images

Règle du dépôt : aucune image du jeu ni du manga n'est versionnée. Trois sources, toutes produites **chez l'utilisateur** :

1. **Écrans du jeu** : au premier lancement, capture de l'écran-titre de chaque jeu (déjà possible, comme `cut_title_logo()` de TMNT) mise en cache dans le dossier de données. Sert de vignette de secours et de fond « jeu figé » de la pause.
2. **Planches de manga perso** : un outil `tools/ui/make_art.py <dossier de scans>` recadre des cases, applique contraste + bichromie + trame, et écrit `data/ui/cards/<jeu>.png` (ignoré par git). Sans ces images, la carte retombe sur la capture du jeu, puis sur un aplat à trame : le menu marche toujours.
3. **Décor** : tout est dessiné par le code (trame, traits de vitesse, silhouette de ville, quadrilatères) : rien à distribuer.

Les planches utilisées dans les maquettes (livre « Le choix de l'auteur ») : p. 696 (visage de Golgo, jeu 1), p. 776 (regards, jeu 2), p. 156 (centrale nucléaire de nuit, jeu 3, cohérent avec l'histoire de la centrale de Motos).

## 6. Décisions à prendre (à ton retour)

1. **Police** : proposition = une police libre de droits (OFL) de style BD avec accents, **embarquée** dans `data/fonts/` (ex. Komika Hand comme TMNT, ou Bangers/Anton). Il faut la télécharger : je n'ai rien téléchargé en ton absence. Alternative sans téléchargement : générer un atlas depuis Impact/Comic Sans de ta machine à la compilation (non distribuable).
2. **Bibliothèques** : `stb_truetype.h` + `stb_image.h` (domaine public) à télécharger dans `third_party/` (comme minimp3).
3. **Jeux sans ROM** : les cacher ou les afficher grisés avec le tampon (maquette) ? (je propose grisés).
4. **Démarrage** : toujours le sélecteur, ou directement le dernier jeu avec Échap pour changer (option `start_last`) ?
5. **Plein écran 16:9** pour le menu même quand le jeu est en 4:3 (je propose oui : menu en 16:9, jeu letterboxé).

## 7. Plan de réalisation

1. `ui.c` (primitives, texte, trame) + `--menu-shots` : on voit les maquettes en vrai, sans toucher au jeu.
2. `options.c` + `settings.ini` v2 (migration de `lang=`), lecture au démarrage, application (plein écran, échelle, son).
3. Sélecteur + Pause branchés sur `main.c` ; souris/clavier/manette.
4. Onglet Commandes (capture de touches) + Wiimote (appairer/calibrer/lissage depuis le menu).
5. Cartes (capture du jeu, `make_art.py`), tests de navigation, README + captures.

## 8. Réalisation (v1)

* Code : `runtime/ui/{ui,options,menu}.c`, plateforme : `runtime/platform/platform_sdl3.c` (événements de menu, `plat_present_ui`, options vidéo/audio/touches, lissage Wiimote), intégration : `runtime/core/main.c`.
* Assets locaux (non versionnés) : `python tools/ui/bake_font.py` (polices Impact + Comic cuites dans `data/ui/`), `python tools/ui/make_art.py` (cartes à partir de tes scans). Sans polices, l'ancien menu texte reste utilisé ; sans cartes, une carte à trame est dessinée.
* Captures sans fenêtre : `golgo13 --menu-shots dossier` (une image par page). Tests : variable `GOLGO13_UIEVENTS="F1,n8,n3,kG,..."` injecte des événements (n<k> navigation, k<touche>) ; `GOLGO13_UISHOT=fichier.ppm` enregistre la dernière image du menu.
* Échap en jeu ouvre la pause (il quittait la fenêtre avant) ; F1 ouvre les options depuis le sélecteur ; F4 ouvre les options depuis le jeu.
* « Changer de jeu » relance le programme sur le sélecteur (`GOLGO13_NOLAST`).
