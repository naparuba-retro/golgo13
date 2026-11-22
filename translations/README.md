# Système de traduction

État de la traduction française et liste de ce qui n'est pas traité : voir `TODO.md`.

Tout est piloté par des fichiers de données : aucune image ni aucune langue n'est écrite en dur dans le code.

```
translations/
  styles.json            styles de texte partagés (police, taille, couleurs, contour...)
  source.ja.json         texte japonais d'origine de chaque image (référence pour traduire)
  manifest.json          liste de toutes les images traduisibles de la ROM (ids, tailles, palettes) – généré
  <langue>/strings.json  les textes de cette langue (fr, en, ...)
  <langue>/patch.g13t    patch binaire généré (NE PAS publier : il contient des pixels du jeu)
```

## Choisir la langue (dans le jeu)
* **F4** (ou **L1 + R1** à la manette) ouvre le menu « Language / Langue » : *Original (Japanese)* ou une langue trouvée dans `translations/`. Haut/Bas pour choisir,
  Entrée / clic / bouton A pour valider, F4 ou Y pour fermer. Le jeu redémarre quand la langue change (les images traduites sont chargées au démarrage d'un écran).
* Au tout premier lancement, si au moins une traduction est présente, le menu s'ouvre tout seul.
* Le choix est mémorisé dans `settings.ini` (dossier de données de l'utilisateur, ligne `lang=fr` ou `lang=original`).
* Une langue = un dossier `translations/<code>/` contenant `patch.g13t` et/ou `overlay.g13o` ; son nom affiché vient de `info.txt` (écrit par `build.py` d'après `meta.name`).
* Pour les tests seulement : `--lang <code>` ou la variable `GOLGO13_LANG` forcent une langue sans toucher au réglage.

## Fabriquer une langue
```
python tools/translation/build.py --lang fr        # construit translations/fr/{patch.g13t,overlay.g13o,info.txt} depuis votre ROM
```
Le jeu cherche les langues dans : `--translations DIR`, `./translations`, à côté de l'exécutable, puis le dossier de données de l'utilisateur.
Chaque enregistrement du patch ne s'applique que si les octets d'origine de la ROM ont le CRC32 attendu ; revenir à « Original » restaure ces octets.

## Ajouter ou corriger une traduction
1. Repérer l'image : `manifest.json` (id `pack/layout`) et `source.ja.json` pour le texte d'origine ; `tools/extract_images.py` produit des PNG à regarder.
2. L'ajouter dans `<langue>/strings.json` :
   `"030a9100/02": {"text": "Crise des semi-conducteurs", "style": "story_title"}`
   ou `{"lines": ["ligne 1", "ligne 2"], ...}`, ou `{"image": "mon_image.png"}` (image prête, à la taille exacte de l'original).
3. Reconstruire : `python tools/translation/build.py --lang fr --preview` (les aperçus sont dans `<langue>/preview/`).

## Ajouter une langue
Créer `translations/<code>/strings.json` (copier celui d'une autre langue) et lancer le build. Les polices sont cherchées dans le dossier de la langue puis
dans les polices système (`font` d'un style = nom de fichier). Les styles peuvent être redéfinis dans `strings.json` (clé `styles`).

## Deux modes pour une image
* `tiles` (par défaut) : l'image est redessinée dans la ROM en mémoire (titres d'histoire, plaques de noms). Utilise la palette d'origine ; ne fonctionne que si le
  cadre est entièrement fait de tuiles réinscriptibles.
* `overlay` (`"mode": "overlay"` dans l'asset ou dans son style) : le jeu reconnaît les tuiles de l'image japonaise quand il les dessine, ne les affiche pas, et
  dessine à la place un bitmap RGBA préparé par l'outil. Sans limite de taille ni de palette : c'est le mode des légendes de briefing (texte coupé en lignes
  automatiquement, jusqu'à `max_lines`, police réduite si besoin). Fichier produit : `<langue>/overlay.g13o`.
Style d'un overlay : `font`, `size`, `min_size`, `fill`, `outline`, `area` ([x0, y0, x1, y1] dans l'image), `align`, `line_height`, `max_lines`.

## Troisième mode : overlay par capture de scène (sprites 8 bits, textes sans paquet lisible)
Certains textes (libellés de la carte d'objectif) sont des sprites dont le format n'est pas décodé. On les traite sans connaître ce format :
1. `translations/scenes.json` décrit comment atteindre la scène (entrées scriptées + numéro d'image) ;
2. `python tools/translation/capture_scene.py card_m1` fait tourner le jeu et enregistre la scène telle qu'il la dessine (`translations/captures/*.cap`, contient des
   pixels du jeu : ignoré par git, à régénérer depuis votre ROM) ;
3. dans `strings.json`, un asset libre `"cap:card_m1:ammo": {"capture": "card_m1", "select": {"rect": [x0,y0,x1,y1], "tp": [1], "clut_y": [...]}, "erase": [...], "text": "...", "style": "card_label"}`
   choisit les sprites de la zone, efface la zone `erase` de leur image d'origine et y écrit le texte. En jeu, les sprites sont reconnus par leurs pixels (donc aussi dans les autres missions).
Styles : `gradient` (dégradé vertical), `shadow` (ombre portée), `outline`, `base: "original"` + `erase` (garder l'image d'origine et n'en effacer qu'une zone).

## Quatrième mode : « paint » (peindre par-dessus)
Pour les bandeaux, cadres et bulles, le plus simple est de laisser le jeu dessiner l'original et de peindre un rectangle uni + le texte traduit par-dessus à la fin de l'image.
`{"capture": "tut_scope", "select": {"rect": [...], "tp": [0], "clut_y": [744]}, "paint": {"rect": [...], "fill": "auto" | "#rrggbb"}, "text": "...", "style": "tut_bar"}`
* `select` : sprites qui servent à reconnaître la scène (ils ne sont pas supprimés) ; `paint.rect` : zone repeinte ; `fill: "auto"` = couleur dominante de la capture.
* Utilisé pour le tutoriel de début de partie (scènes `tut_*` de `scenes.json`) : consignes dans les cadres verts, bandeau « réglage du fusil », ligne du bas (3 variantes), bulle de manga.
* Pour un texte qui change (ex. la ligne du bas), faire une capture par variante : chaque variante est reconnue par ses propres sprites.
`tools/translation/view_capture.py <scène> --rect ... --tp ... --clut-y ...` montre ce que dessinent les sprites d'une zone (aide à écrire les `select`/`paint`).
`tools/translation/scene_sources.py` retrouve de quels paquets de la ROM viennent les tuiles d'une scène.

## Limites actuelles
* L'overlay est dessiné à la résolution du jeu (512x480) puis agrandi comme le reste de l'image.
* Quand le jeu fait « clignoter » une légende à son apparition (fond vert), le texte français apparaît sur ce fond, comme le japonais d'origine.
* Règle de projet : les textes déjà en anglais dans le jeu (ACCIDENTAL, TIME OVER, CREDIT, TARGET, SNIPE POINT...) ne sont PAS traduits, c'est de l'anglais courant.
* Les onomatopées dessinées dans les cases de manga (ex. « チャ！ ») font partie du dessin : non traduites.
* Pas encore traduits (japonais) : « 信頼度 », la barre rouge du bas de l'écran de choix, les objectifs des autres missions, les autres textes japonais des missions et du menu de test.
