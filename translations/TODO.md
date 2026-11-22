# Traduction française : ce qui est fait et ce qui reste

Mis à jour après la session de jeu de référence (`translations/captures/ref_s2.g13r`, local, non publié) : le français est la seule langue, avec « Original ».

## Fait (dans `translations/fr/strings*.json`)
| Élément | Où | Vérifié dans le jeu |
|---|---|---|
| Titres des 20 histoires (écran de choix) | `strings.json` | oui |
| Légendes + plaques de noms du briefing, **mission 1 et missions 2 à 19** (19 paquets, ~230 légendes) | `strings.json`, `strings.<paquet>.json` | mission 1 oui ; une autre mission (« Miracle sur la corde ») oui ; les autres : même mécanisme, **non vues à l'écran** |
| Objectifs de mission (carte d'objectif) des 19 missions | `strings.json`, `strings.objectives.json` | mission 1 oui ; « Sauvez le condamné » (mission « Miracle sur la corde ») oui ; les autres : aperçu image seulement |
| Libellés de la carte d'objectif (munitions, temps limite, coup, sec, cible) | `strings.json` (capture `card_m1`) | oui, y compris sur une autre mission (reconnus par leurs pixels) |
| Tutoriel du début (2 consignes, bandeau, 3 variantes de la ligne du bas, bulle de manga) | `strings.json` (captures `tut_*`) | oui |
| Écran de résultat d'un tir raté **et réussi** (« TIR RATÉ » / « TIR RÉUSSI » + 5 libellés), écran « SNIPE POINT » réussi | `strings.result.json` (captures `result_fail`, `result_ok`, `snipe_ok`) | oui (rejeu de la session de référence) |
| Écran GAME OVER (« Gains cumulés », « Précision moyenne », mention « cette histoire est fictive ») | `strings.result.json` (capture `game_over`) | oui (rejeu de la session « inactive » `ref_idle.g13r`) |
| Carte d'objectif « munitions illimitées » (無制限 → ILLIMITÉ) | `strings.objectives.json` (capture `card_unl`) | oui |
| Écran « Commandes » (8 panneaux d'explication du fusil), carte « Big Comic », barre rouge et « Confiance » de l'écran de choix | `strings.ui.json` | « Commandes » : **oui**, vu en français dans le cycle d'attente (démarrage sans pièce, ~20 s) ; carte « Big Comic », barre rouge, « Confiance » : « Confiance » et barre vus à l'écran de choix, le reste non vu |

## Couverture vérifiée à l'écran (rejeux)
* Les 20 histoires + le « Niveau spécial » ont été jouées en rejeu (session de référence de l'utilisateur pour les lignes 1 et 2, robot de test `--bot` pour les suivantes, en échec) : titres, cartes d'objectif, écrans « TIR RATÉ », « CONTINUE? », GAME OVER, boucle d'attente. Les écrans de réussite viennent de la session de l'utilisateur (une dizaine de missions).
* Légendes de briefing : vues en français sur une dizaine de missions (rejeux) ; les autres légendes suivent le même mécanisme.
* Le robot ne sait pas gagner (cibles propres à chaque mission, zoom + tir au bon moment) : les réussites des lignes 3 et 4 n'ont pas été vues ; les écrans de réussite sont communs.

## Non traité
1. **Classement / démo** : observés (démarrage sans pièce) : SCORE RANKING, ACCURACY RANKING, démos de missions, NOTICE sont **en anglais dans le jeu** (rien à traduire) ; NAME ENTRY et CONTINUE? aussi. Avec des crédits, le jeu reste sur l'écran titre (pas de cycle d'attente).
2. **Textes dessinés dans les cases de manga** : dans les missions jouées (1 à 5 environ), les cases ne contiennent que « !! » ; les onomatopées dessinées (ex. « ズガーン ») font partie du dessin, laissées (règle de projet). Les missions 6 à 19 n'ont pas été jouées : cases non vues.
3. **Menu de test et de service** : regardés, entièrement en anglais (MENU, COIN OPTIONS, GUN INITIALIZE...) : rien à traduire.
4. **Plaque illisible** : `033f2660/13` (paquet de la mission du hacker) : l'image de la ROM est brouillée, pas de texte lisible.
5. **Légende vide de texte** : `033dc690/03` (« …!! ») : laissée telle quelle.
6. **Tampon « 削除 »** dans l'image de la légende `0335cdb0/04` : dans les données de la ROM ; le texte français le remplace.
7. **Mentions de droits** « © さいとう・プロ／小学館 » et l'URL : laissées (noms propres).
8. **Textes des missions en 3D** : n'existent pas en texte ; le texte anglais du jeu (TARGET, SNIPE POINT, TIME OVER, CREDIT...) est conservé volontairement.

## Défauts connus de rendu
* Pendant l'animation d'ouverture / fermeture des bulles de l'écran « Commandes » (la boîte verte qui grossit), le texte français est brouillé quelques images (≈10) ; stable ensuite.
* Pendant l'arrivée de la carte d'objectif, le texte français suit les morceaux du cadre qui glissent (comme l'original) : quelques images en « accordéon ».
* L'ombre du titre d'histoire japonais laisse parfois un halo noir derrière le titre français.

## À relire (traductions incertaines)
* Noms propres romanisés d'après le katakana : Bardot (バルドー), Viroresi (ヴィロレーシ), Rutchinski (ルチンスキー), Siad (シアド), Taruffi, Doyle (ドイル), Schmitt, Schechter, Nichols, Koyama, Meres, Bigram, Gonzalez, Randy, Hills, Collins, Amon…
* Phrases coupées sur deux légendes (la suite dans la légende suivante) : j'ai écrit chaque légende pour qu'elle se lise seule, ce qui peut s'éloigner un peu de l'original (ex. `031f0870/08-09`, `0325ca20/08-09`, `0335cdb0/08-11`, `034bc170/08-10`).
* Plaques raccourcies pour tenir sur 200 px (« Pdt Play System Berry », « Chef MI6 Hume », « Soc. dév. Nichols »…).
* « Réglage du fusil de sniper », « Mettez-vous en position à côté du fusil » (le japonais « 銃の横で構える » est ambigu).

## Limites techniques connues
* Le texte superposé est dessiné à la résolution du jeu (512×480) : un peu flou en plein écran.
* Pendant les fondus, un rectangle peint peut apparaître un instant à pleine luminosité (corrigé pour la bulle du tutoriel seulement).
* Le menu de langue utilise la petite police intégrée (ASCII, sans accents).
