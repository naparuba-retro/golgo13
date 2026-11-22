# Kiseki no Dandou (g13knd) : traduction française, état

Données dans `translations/g13knd/` (manifeste, scènes, `fr/strings*.json`). Construire :
`python tools/translation/build.py --lang fr --rom <dossier ROM g13knd> --translations translations/g13knd`.
Les images identiques à celles du premier jeu ont été reprises par `tools/translation/port_strings.py` ; le reste est écrit à la main (`tools/translation/mkpack.py`).

## Fait (vu à l'écran, rejeu d'une session du robot de test)
* Titres des 20 histoires + barre « Choisir : fusil / Valider : gâchette » + libellé « Confiance » (écran de choix, résultat).
* Légendes et plaques de noms de 10 paquets nouveaux (Sultanov, Taylor, Bell, Yoshida, Allison, Krasts, Marioli, Hassad, Williams…) ; les 9 premiers paquets sont repris du premier jeu.
* 20 objectifs de mission (plaques) ; libellés de la carte d'objectif (Munitions / Temps limite / coup / sec).
* Écran de résultat d'un tir raté (TIR RATÉ + libellés), tutoriel (bulle, 1re consigne, bandeau), « Commandes » (repris), carte « Big Comic » de l'attente.

## Pas fait / pas vu
* Intro de l'attente : traduits « Réussite des tirs : 99,98 % », la phrase « L'homme que toutes les organisations craignent… » et « Duke Togo » ; **non traité** : « またの名を… » (dessiné autrement que par sprites 1:1, non capturable avec l'outil actuel) et les caractères tapés 狙撃 成 功 au début de la ligne.
* Bulle de manga de l'écran GAME OVER : traduite (« Zut, zut !! Quel type !! »).
* Écrans de réussite : titre « TIR RÉUSSI » vu sur l'écran de résultat et sur les deux écrans SNIPE POINT (jauge, diamant) ; ce titre est la même image partout, un seul recouvrement suffit. Tutoriel complet (consigne de position, bulle, bulle de manga verticale) vu à l'écran (session de l'utilisateur, rejouée).
* Carte « munitions illimitées » (無制限) : non vue (les histoires des lignes suivantes sont verrouillées tant que les premières ne sont pas réussies). CONTINUE? est de l'anglais.
* Textes des missions elles-mêmes (en jeu) : non regardés ; manga : dessins non traduits.
* Noms propres romanisés d'après le katakana (Loukachenko, Sommer, Bracco, Rakhmonov, Hassad…) : à relire.
