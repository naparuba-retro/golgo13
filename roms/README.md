# roms/ : tes ROM (non fournies)

Dépose ici **tes propres ROM**, une par jeu, en dossier ou en zip (jamais publiées, ce dossier est ignoré par git sauf ce fichier) :

| Jeu | Nom attendu |
|---|---|
| Golgo 13 (1999, System 12) | `golgo13.zip` ou dossier `golgo13/` (set MAME `golgo13`) |
| Golgo 13 : Kiseki no Dandou (2000, System 12) | `g13knd.zip` ou dossier `g13knd/` (set MAME `g13knd`) |
| Golgo 13 : Juusei no Requiem (2001, System 10) | `g13jnr.zip` ou dossier `g13jnr/` (set MAME `g13jnr`) |

Le jeu vérifie les CRC32 des fichiers, puis les copie dans ton dossier de données. Tu peux aussi les importer à la main :
`golgo13 --import chemin\vers\golgo13.zip`. Sans ROM, le sélecteur affiche « ROMS MANQUANTES » sur la carte du jeu.
