# Format de banque — SFZ

## Pourquoi SFZ et pas un format maison

Un format propriétaire aurait été plus rapide à écrire et plus compact à
charger. Il aurait aussi voulu dire : aucune banque existante ne fonctionne,
aucun éditeur tiers, et un convertisseur à maintenir pour toujours.

SFZ est du texte, ouvert, documenté, et c'est le format dans lequel les banques
libres sont réellement distribuées. On peut ouvrir un mapping dans un éditeur de
texte et comprendre ce qui se passe — ce qui compte beaucoup quand une note
sonne mal et qu'on cherche pourquoi.

Le coût : le parsing d'une banque de 10 000 régions prend une seconde ou deux.
C'est payé une fois au chargement, hors thread audio. Le jour où ça devient
gênant, un cache binaire du résultat du parsing règle le problème sans changer
le format source. Voir `docs/adr/ADR-002`.

## Opcodes reconnus

### Sélection

| Opcode | Effet |
|---|---|
| `sample` | fichier, relatif au `.sfz` ou à `default_path` |
| `lokey` / `hikey` / `key` | étendue de notes (nom ou numéro : `c4` = 60) |
| `lovel` / `hivel` | étendue de vélocité |
| `pitch_keycenter` | note de référence ; à défaut, le chunk `smpl` du WAV |
| `seq_length` / `seq_position` | round-robin |
| `sw_lokey` / `sw_hikey` / `sw_last` / `sw_default` | keyswitches |
| `sw_label` | nom de l'articulation — **fortement recommandé** |
| `locc<n>` / `hicc<n>` | conditionnement par contrôleur |
| `trigger` | `attack` (défaut), `release`, `first`, `legato` |
| `group` / `off_by` / `off_mode` | groupes exclusifs |

### Amplitude et timbre

| Opcode | Effet |
|---|---|
| `volume` | gain en dB |
| `pan` | -100 à +100 |
| `width` | largeur stéréo |
| `amp_veltrack` | sensibilité à la vélocité, en % |
| `xfin_lovel` / `xfin_hivel` / `xfout_lovel` / `xfout_hivel` | crossfade en vélocité |
| `xfin_locc<n>` / `xfin_hicc<n>` / `xfout_locc<n>` / `xfout_hicc<n>` | **crossfade en CC — le mécanisme de la dynamique orchestrale** |
| `amp_random` | variation aléatoire de niveau |

### Hauteur

`transpose`, `tune`, `pitch_keytrack`, `pitch_random`.

### Lecture et boucles

`offset`, `end`, `loop_mode` (`no_loop`, `one_shot`, `loop_continuous`,
`loop_sustain`), `loop_start`, `loop_end`.

### Enveloppe

`ampeg_delay`, `ampeg_attack`, `ampeg_hold`, `ampeg_decay`, `ampeg_sustain`
(en %), `ampeg_release`, `delay`.

### Structure

En-têtes `<control>`, `<global>`, `<master>`, `<group>`, `<region>`, avec
héritage dans cet ordre. `<curve>` et `<effect>` sont reconnus et ignorés.
Directives `#define $nom valeur` et `#include "fichier.sfz"`. Commentaires `//`
et `/* */`.

Extension propre au projet : `moe_name` dans `<control>` fixe le nom affiché.

### Non implémenté

Filtres (`fil_type`, `cutoff`, `resonance`), LFO, effets, `<curve>`. Les
banques qui s'en servent chargent quand même — ces opcodes sont ignorés, pas
rejetés. Les filtres sont le premier candidat pour une V2.

## Ce que le moteur attend d'une bonne banque

### Déclarer `sw_label`

Sans lui, l'articulation est devinée depuis le nom du fichier
(`vln_spicc_c4.wav` → spiccato). Ça marche souvent, mais ce n'est qu'une
heuristique. Avec `sw_label`, c'est exact, et l'interface affiche le bon nom.

```
<group> sw_last=25 sw_label=spiccato
```

### Câbler le CC1

C'est le point qui change le plus le résultat. Sans crossfade CC, la dynamique
se réduit à un changement de volume — le moteur simule bien une ouverture de
timbre au filtre, mais ça ne remplace pas de vraies couches enregistrées.

```
// couche piano : disparaît quand CC1 monte
<group> xfout_locc1=40 xfout_hicc1=100
<region> sample=vln_p_c4.wav ...

// couche forte : apparaît quand CC1 monte
<group> xfin_locc1=30 xfin_hicc1=95
<region> sample=vln_f_c4.wav ...
```

Les plages se recouvrent volontairement : c'est ce recouvrement qui donne le
fondu. Le moteur applique une loi de puissance constante, donc la somme reste
cohérente en niveau.

### Boucler les notes tenues

Une note tenue non bouclée s'arrête quand le fichier finit. `loop_sustain` est
le bon mode pour de l'orchestral : la boucle tourne tant que la touche est
tenue, puis la queue enregistrée est jouée au relâchement.

Les points de boucle peuvent venir du chunk `smpl` du WAV (écrit par la plupart
des éditeurs) ou de `loop_start`/`loop_end`.

### Fournir du round-robin

Deux variantes par note suffisent à supprimer l'essentiel de l'effet
mitraillette. Le compteur du moteur est **par note**, donc deux répétitions de
la même note ne réutilisent jamais le même échantillon.

## Organisation des fichiers

Rien n'est imposé, mais ceci se relit bien :

```
banks/
└── ma-banque-cordes/
    ├── violins-1.sfz          un fichier par pupitre
    ├── violins-2.sfz
    ├── violas.sfz
    ├── shared/
    │   └── keyswitches.sfz    #include commun
    └── samples/
        ├── sustain/
        ├── staccato/
        └── ...
```

Le chargeur déduplique les fichiers : un même WAV référencé par vingt régions
n'est ouvert et préchargé qu'une fois.

## Intégrer ses propres enregistrements

1. **Enregistrer** — notes tenues d'au moins 4 s, plusieurs nuances, plusieurs
   prises par note. Le silence avant l'attaque doit être coupé net : le moteur
   déclenche à l'échantillon près, tout silence en tête devient de la latence.
2. **Découper** un fichier par note/nuance/prise. Une convention de nommage
   régulière (`vln_sus_c4_p_rr1.wav`) rend la génération du SFZ triviale.
3. **Boucler** les notes tenues, dans un éditeur qui écrit le chunk `smpl`
   (Audacity, RX, Wavosaur…). Chercher un point de boucle sur un passage stable,
   après le vibrato d'attaque.
4. **Mapper** — écrire le SFZ, ou le générer par script. Une note tous les 3 à 4
   demi-tons est un bon compromis ; en dessous de 2, le gain n'est plus audible,
   au-delà de 5 la transposition s'entend.
5. **Vérifier** :

```bash
moe_render --bank ma-banque.sfz --list          # ce que le moteur a compris
moe_render --bank ma-banque.sfz --out /tmp/t.wav --notes 60 --seconds 3
```

`--list` affiche le nombre de régions, l'étendue, les articulations détectées et
les keyswitches. C'est le moyen le plus rapide de repérer un mapping qui ne dit
pas ce qu'on croyait.

## Diagnostics

Le parseur ne rejette jamais un fichier pour une ligne malformée : il émet un
avertissement et continue. Les avertissements remontent dans l'interface après
un chargement, et sur la sortie standard avec `moe_render`.

Un fichier introuvable, un `<region>` sans `sample`, une étendue de notes vide :
chacun coûte une région, pas la banque entière.
