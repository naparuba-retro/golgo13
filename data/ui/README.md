# data/ui/ : habillage du menu (fabriqué localement, non versionné)

Le menu fonctionne sans ces fichiers (l'ancien menu texte prend le relais, et les cartes de jeux sont dessinées à la trame). Pour avoir le menu « BD » :

```
python tools/ui/bake_font.py          # cuit font_title.bin et font_body.bin à partir des polices installées (Impact + Comic Sans sous Windows)
python tools/ui/make_art.py "chemin/vers/tes/scans/de/manga"   # facultatif : images des cartes (data/ui/cards/*.rgba) et du gag de démarrage
```

Les polices cuites et les images dérivées de manga restent sur ta machine (ignorées par git) : elles ne sont jamais redistribuées.
