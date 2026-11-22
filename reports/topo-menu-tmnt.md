# Le menu de TMNT (port C) : topo pour s'en inspirer

Écrit par la session TMNT. Tout le code est dans `C:\SOURCES\tmnt\port\src\` (dépôt public : https://github.com/naparuba-retro/tmnt-recomp). Les chemins ci-dessous sont relatifs à ce dossier sauf mention contraire. Version décrite : v1.7.1.

## 0. Vue d'ensemble

Un lanceur « bande dessinée » dessiné **entièrement par le processeur** (rendu logiciel dans un tampon RGBA, aucune API graphique pour l'interface), affiché par la plateforme sous forme d'une texture. Au lancement le menu s'ouvre **avant** le jeu ; `Esc` le rouvre par-dessus le jeu figé (RESUME). Les options sont une table de métadonnées (`options.c`), sauvegardée dans `tmnt.ini` à côté de l'exécutable.

Fichiers :

| Fichier | Rôle |
|---|---|
| `menu.c`, `menu.h` | pages, navigation, focus, dessin, écran « ROM manquante » |
| `ui.c`, `ui.h` | le moteur de dessin logiciel (quadrilatères, trame de points, texte, images) |
| `options.c`, `options.h` | la table des options, leurs valeurs, lecture et écriture de `tmnt.ini` |
| `frontend.c` | la boucle du jeu : `menu_frame()`, `present_game()`, `apply_display_options()`, `choose_ini_path()` |
| `platform_win.c`, `platform_sdl.c` | fenêtre, clavier, manette, souris, présentation d'une image haute définition (`platform_present_hd`) |
| `help_screen.c` | écran de secours simple quand les polices manquent |
| `third_party/stb_truetype.h`, `stb_image.h` | polices TrueType et PNG (domaine public) |
| `replay_main.c` (`menu_shots`, `menu_check`) | captures d'écran du menu et 40 tests automatiques |

Documentation : `C:\SOURCES\tmnt\port\README.md` (usage), `TECHNICAL.md` (architecture), images du menu dans `images\`.

## 1. Structure du menu

**Écrans** (4 onglets + 2 écrans annexes) :

1. **PLAY** : deux grands panneaux, *Original* et *Whole Pizza* (le préréglage d'options), plus un bloc « EXTRAS » (invincible, voir les boîtes de collision), un sélecteur de niveau (6 boutons) et le bouton PLAY (ou RESUME si un jeu tourne dessous). Chaque ligne de Whole Pizza a une case à cocher ; une bulle jaune en bas à droite affiche la description de la ligne qui a le focus. L'édition qui n'est pas choisie est en **niveaux de gris** (`ui_desaturate_quad`).
2. **SOUND** : volume (curseur) et coupure du son.
3. **GRAPHICS** : plein écran, zoom entier, compteur d'images par seconde, filtre CRT (+ 3 curseurs), « Atmosphere » (éclairage et effets), etc. Les pages d'options utilisent `draw_options_page()` avec une liste d'identifiants : ajouter une option = ajouter son identifiant dans la liste.
4. **CONTROLS** : une ligne par bouton de manette (A, B, Select, Start, haut, bas, gauche, droite), **deux touches par bouton**, un bouton « RESET KEYS ». On clique une case, puis on appuie sur la touche voulue (`menu_capturing()`, `platform_capture_key()`).
- **Fenêtre modale** « quitter ? » (OUI / NON) : `draw_modal()`.
- **ROM manquante** : `menu_draw_rom_missing()`, dans le même style, qui dit où mettre la ROM.

**Navigation** : tout passe par une seule structure `MenuInput` (`menu.h`) remplie dans `frontend.c`, fonction `menu_frame()` : `up / down / left / right`, `confirm` (A ou Start de la manette), `back` (B), `escape`, `start`, `tab_prev` / `tab_next` (épaules de la manette), souris (position, clic, bouton enfoncé), touche capturée. La logique est dans `menu_update()` / `menu_update_inner()` de `menu.c`.

- **Clavier** : les touches du jeu servent de manette (flèches + 2 touches configurables, voir §2). `Esc` ouvre/ferme le menu.
- **Manette** : XInput sous Windows (`platform_win.c`), SDL_GameController sous Linux (`platform_sdl.c`, y compris la Steam Deck). Croix directionnelle ou stick pour bouger, A/Start pour valider, B/X pour revenir, épaules gauche/droite pour changer d'onglet.
- **Souris** : survol = focus, clic = action ; `platform_mouse()` convertit les coordonnées de la fenêtre en pixels de l'image du menu (elles diffèrent sur un écran à haute densité, et quand le menu est dessiné plus petit que la fenêtre).
- **Wiimote** : non géré. Aucun code pour ça.

**Modèle de focus** : chaque image, le dessin « enregistre » ses éléments cliquables dans un tableau `Item items[MAX_ITEMS]` (genre : onglet, édition, option, niveau, touche, etc., avec un quadrilatère). Le focus clavier/manette (`focus_kind`, `focus_arg`) se déplace entre ces éléments ; la souris les teste avec `ui_quad_contains`. Un deuxième tableau (`last_items`) garde ceux de l'image précédente pour les tests.

## 2. Options et persistance

Table dans `options.c` : chaque option est un `OptInfo { clé du fichier, section, étiquette, description, valeur par défaut, min, max, ..., }`. Les valeurs vivent soit dans le moteur concerné (les correctifs, le masque d'apparitions...), soit dans un tableau local. `opt_get()` / `opt_set()` aiguillent ; `opt_set_user()` marque la valeur « à écrire ».

Options (identifiants de `options.h`) :
- **[play]** `edition` (0 Original, 1 Whole Pizza).
- **[whole_pizza]** 10 lignes : glissade au plafond, courant de l'eau, boîtes de collision, caméra intelligente, masque d'apparitions, équilibrage des dégâts, portée de Raphaël, « les ennemis restent morts », grande vue, pas de scintillement.
- **[extras]** invincible, voir les boîtes de collision, niveau de départ (1 à 6).
- **[sound]** `volume` (0-100), `mute`.
- **[graphics]** `fullscreen`, `integer_scale` (le rapport est **toujours** conservé, jamais d'image étirée), `show_fps`, `crt` + `crt_scanlines` / `crt_mask` / `crt_curve` (0-100), `atmosphere` (éclairage, brume, lianes, cascades, fumée ; ancien nom `illumination` toujours lu).
- **[keys]** 16 options `key_<bouton>_1/_2` : 0 = touche par défaut, -1 = aucune, sinon le code de touche de la plateforme.
- Pas de langue (tout est en anglais), pas de système de sauvegarde : seulement des instantanés en mémoire (F9) et des captures de debug (F10).
- « Triche » : seulement `invincible` (touche I).

**Format de `tmnt.ini`** (INI classique) :

```ini
; tmnt.ini: only the values that differ from the defaults are written here (the game menu does it).
[play]
edition=1
[whole_pizza]
raph_reach=1
[graphics]
fullscreen=1
crt_scanlines=25
```

Règles (code : `options_load()`, `options_save()` dans `options.c`) :
- **On n'écrit que ce qui diffère du défaut** ; une valeur remise au défaut disparaît du fichier.
- Les sections et clés **inconnues sont conservées** (on réécrit le fichier en gardant les lignes qu'on ne comprend pas), et une valeur invalide est ignorée avec un message.
- L'écriture est **différée** : `menu_flush()` écrit après environ 30 images sans action, ou à la fermeture, ou à la sortie, pour ne pas écrire à chaque clic.
- Emplacement : à côté de l'exécutable (`choose_ini_path()` dans `frontend.c`), sinon `%APPDATA%` quand le dossier est en lecture seule.
- Les options de la ligne de commande passent **après** le fichier et gagnent.

## 3. Rendu de l'interface

**Moteur logiciel** `ui.c` / `ui.h` (658 lignes) : un tampon `uint32_t` (ARGB), rempli par :
- `ui_fill_quad`, `ui_stroke_quad` (contours à coins biseautés), `ui_halftone_quad` (**trame de points** de bande dessinée, avec un dégradé), `ui_fill_burst` (étoile pointue de type « POW! »), `ui_fill_triangle` ;
- texte avec **stb_truetype** : `ui_text`, `ui_text_outlined` (contour noir épais), `ui_text_wrapped` (retour à la ligne), `ui_text_width` ;
- images PNG avec **stb_image** : `ui_image_from_rgba`, `ui_image_draw` (lissage au choix) ;
- `ui_desaturate_quad` (passage en gris, utilisé pour l'édition non choisie) et `ui_picture` (l'image du jeu figé en fond, assombrie).

**Composition** `menu.c` : on dessine dans un repère de **1280 × 720** (`DESIGN_W` / `DESIGN_H`), mis à l'échelle (`e.s`) et centré selon la fenêtre ; les panneaux sont des quadrilatères légèrement inclinés (effet « case de BD ») avec une ombre décalée.

**Performance** (point important, voir §5) :
- l'image du menu est **mise en cache** : `page_signature()` calcule une empreinte de l'état (page, option, focus, taille...), et le fond n'est redessiné que si elle change ; entre-temps on recopie l'image et on dessine seulement le contour du focus qui pulse ;
- si la fenêtre dépasse environ 1,5 million de pixels (`MENU_MAX_PIXELS`, `frontend.c`), le menu est dessiné **plus petit** et la plateforme l'étire (`platform_present_hd()`, interpolation linéaire).

**Police** : `data\fonts\KOMIKAH_.ttf` et `KOMIKAHB.ttf` (Komika Hand, licence dans `KOMIKA-LICENSE.txt`), chargées depuis le dossier `data` à côté de l'exécutable. Si elles manquent, `menu_has_fonts()` répond non et un écran de secours très simple (`help_screen.c`) prend le relais.

**SDL** : c'est du **SDL2** (pas SDL3) : une fenêtre redimensionnable, un `SDL_Renderer`, une texture en streaming qui reçoit le tampon de l'interface. Sous Windows, pas de SDL : GDI, waveOut et XInput directs (`platform_win.c`).

## 4. Habillage graphique

- **Style** : bande dessinée américaine : fond bleu nuit à **trame de points**, panneaux « papier » crème avec trame, **contours noirs épais**, onglets jaunes, bulles de description jaune pâle, ombres décalées, bouton rouge pour quitter, case à cocher blanche. Les couleurs sont des constantes en tête de `menu.c`.
- **Pas d'image de fond** : tout est dessiné par le code (trames, quadrilatères, étoiles). Les seuls actifs graphiques :
  - `data\ui\logo-shell.png` : le logo de l'édition Whole Pizza ;
  - **le logo du jeu**, qui n'est pas dans le dépôt : il est **découpé à l'exécution dans l'écran-titre de la ROM de l'utilisateur** (`cut_title_logo()` dans `frontend.c`, puis `menu_set_logo_picture()`), et affiché dans le panneau Original. Avec `--blur-logo` il est flouté (`menu_set_logo_blur()`), ce qui sert aux captures d'écran publiques.
- **Captures de l'interface** (README) : `TMNT_MENU_SHOT=<dossier> tmnt_replay.exe` écrit une image BMP par page (fonction `menu_shots()` de `replay_main.c`), sans fenêtre.

## 5. Pièges rencontrés, et ce que je referais autrement

1. **Le rendu logiciel est lent au début** : la première version gelait le jeu 250 ms à chaque changement. Corrigé par le cache de la couche de base, des contours précalculés, des tuiles de trame, et la limite de pixels. *Je referais* : dessiner dans une texture GPU (SDL3 `SDL_GPU` ou le moteur de rendu de SDL) à partir d'une description, et ne garder le logiciel que pour les captures.
2. **Mise en page codée en dur** (coordonnées en pixels dans `menu.c`) : une ligne de plus dans la page Graphiques a demandé de resserrer toutes les lignes (`draw_options_page`). *Je referais* : une mise en page par colonnes et lignes (une grille qui s'adapte au nombre d'éléments), ou des fichiers de description.
3. **Coordonnées de la souris** : fenêtre et pixels diffèrent (écrans à haute densité, menu dessiné plus petit). Il faut convertir à un seul endroit (`platform_mouse`). Sous Windows, appeler `SetProcessDPIAware()` pour avoir de vrais pixels.
4. **Touches du clavier** : les touches sont enregistrées dans le fichier avec le **code de la plateforme** (SDL ou Windows), donc un `tmnt.ini` n'est pas portable d'un système à l'autre. *Je referais* : un nom de touche portable.
5. **Disposition AZERTY / QWERTY** : les touches par défaut sont désignées par leur **position** (scancode), pas par leur lettre ; l'étiquette affichée est calculée (`platform_default_key`, `platform_key_name`).
6. **Écriture du fichier** : différée, et sans écriture atomique. *Je referais* : écrire dans un fichier temporaire puis renommer.
7. **Polices manquantes** : un plantage quand le menu essayait d'écrire sans police. Il y a maintenant un contrôle des pointeurs nuls et un écran de secours.
8. **Pas de traduction** : toutes les chaînes sont en dur dans les fichiers C. *Je referais* : une table de chaînes indexée par langue dès le départ, avec une police qui a les accents.
9. **Pas de graphe de focus** : le déplacement manette/clavier est écrit à la main pour chaque page. Un petit graphe (voisins haut/bas/gauche/droite par élément) éviterait des cas particuliers.
10. **À garder** : les **tests du menu** (`replay_main.c`, 40 vérifications, script `C:\SOURCES\tmnt\tools\menu_check.sh`) : chaque élément expose sa position (`menu_item_centre`) pour qu'un test clique dessus ; c'est ce qui permet de modifier les pages sans les casser.
11. **Piège d'outillage** : écrire des chaînes C contenant `\n` via un script de génération les transforme en vrais retours à la ligne ; utiliser un outil d'édition qui ne réinterprète pas.

## 6. Par où commencer pour ton menu

1. Lire `menu.h` (l'interface), puis `ui.h` (les primitives), puis `menu.c` : `draw_base()`, `draw_options_page()`, `draw_edition()`, `menu_update_inner()`.
2. Lire `options.h` et `options.c` pour la table d'options et `tmnt.ini`.
3. Lire `frontend.c` : `menu_frame()` (comment les entrées arrivent), `present_game()`, `choose_ini_path()`.
4. Pour un **sélecteur de jeux** : une page de plus dont chaque élément est une vignette (`ui_image_draw` ou `ui_picture`) enregistrée dans le tableau des éléments cliquables ; le reste (focus, souris, manette, cache) est déjà générique.
