# ADR-002 — SFZ comme format de banque

**Statut** : acté · **Date** : 2026-07-30

## Contexte

Il fallait un format pour décrire les mappings. Trois options : un format
binaire maison, SFZ, ou DecentSampler (XML).

## Décision

SFZ, avec un parseur tolérant. Un format binaire compilé pourra venir plus tard
comme **cache**, jamais comme format source.

## Raisons

- C'est le format dans lequel les banques libres existent réellement
  (VSCO 2, Virtual Playing Orchestra, Sonatina). Un format maison aurait voulu
  dire : aucune banque existante ne fonctionne au démarrage.
- C'est du texte. Quand une note sonne mal, on ouvre le mapping et on voit
  pourquoi. Avec un binaire, il faut écrire un inspecteur.
- Des éditeurs tiers existent déjà.

## Conséquences

- Parser 10 000 régions coûte une à deux secondes, hors thread audio. Acceptable ;
  un cache binaire du résultat réglera le problème le jour où il se pose.
- Le sous-ensemble implémenté couvre la sélection, l'amplitude, la hauteur, les
  boucles et les enveloppes. Filtres et LFO sont ignorés — une banque qui s'en
  sert charge quand même, en sonnant un peu différemment.
- Le parseur ne rejette jamais un fichier pour une ligne fautive : il avertit et
  continue. Une banque tierce imparfaite reste jouable, ce qui est le cas normal.

## Écarté

**DecentSampler** — format ouvert lui aussi, gros catalogue gratuit (Pianobook).
Bon candidat pour une V2, mais le catalogue *orchestral* est plus faible qu'en
SFZ, et ajouter un second parseur avant que le premier soit éprouvé était
prématuré.
