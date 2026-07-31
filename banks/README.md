# Banques de samples

Ce dossier n'est pas versionné (voir `.gitignore`) : les banques sont
volumineuses et ont leurs propres licences.

## Pour démarrer sans rien télécharger

```bash
python3 ../tools/make_placeholder_bank.py --output placeholder-strings
```

Génère une banque de cordes synthétisée, structurellement complète
(articulations, couches dynamiques, round-robin, keyswitches, boucles). Elle ne
sonne pas comme un orchestre — son rôle est de rendre toute la chaîne audible
avant qu'une vraie librairie soit choisie.

## Pour une vraie banque

Lire `docs/00-audit-samples.md`. Recommandation de départ : **VSCO 2 Community
Edition** (CC0, orchestre complet). Déposer le dossier ici, puis charger le
`.sfz` depuis le plugin.

Arborescence attendue — rien n'est imposé, les chemins des samples sont
résolus relativement au fichier `.sfz` :

```
banks/
├── placeholder-strings/
│   ├── placeholder-strings.sfz
│   └── *.wav
└── vsco2-ce/
    ├── violins.sfz
    └── samples/
```

## Vérifier une banque

```bash
moe_render --bank banks/ma-banque.sfz --list
```

Affiche ce que le moteur a compris : régions, étendue, articulations détectées,
keyswitches, et tous les avertissements de parsing.
