# Feuille de route

## Où on en est

Le MVP est atteint : la chaîne complète fonctionne, de la note MIDI à la sortie
audio, avec keyswitches, articulations, streaming, spatialisation et réverbe.
Le moteur est couvert par 54 tests. Le plugin compile en VST3, AU et standalone.

Ce qui manque n'est plus de l'architecture — c'est du contenu, du réglage à
l'oreille, et des fonctions de confort.

## Décisions qui t'appartiennent

### 1. Distribution ou usage personnel ? — *bloquant à terme, pas maintenant*

Si le plugin reste personnel, n'importe quelle banque SFZ convient. S'il est
destiné à circuler, tout ce qui est empaqueté doit être CC0 ou explicitement
redistribuable. Voir [00-audit-samples.md](00-audit-samples.md) §4.

Rien n'est bloqué en attendant : le développement continue sur la banque
placeholder.

### 2. Quelle banque en premier ?

Recommandation : **VSCO 2 Community Edition** (CC0, ~1,5 Go, orchestre complet).
C'est la seule option qui soit à la fois complète, correcte et juridiquement
sans risque.

### 3. Une instance par pupitre, ou une instance multi-timbrale ?

Les deux fonctionnent. Une instance par pupitre est plus lisible en DAW et
parallélise mieux ; le patch multi-timbral consomme moins de mémoire. C'est un
choix de workflow, pas technique.

## Prochaines étapes, par ordre d'utilité réelle

### 1. Brancher une vraie banque et régler à l'oreille — *le plus important*

Tout le reste est spéculatif tant que ça n'est pas fait. Points à vérifier
spécifiquement, parce qu'aucun test ne peut les couvrir :

- **Le legato.** Les valeurs de portamento et d'atténuation d'attaque ont été
  choisies sur des bases musicales, pas mesurées. Elles vont demander du réglage.
- **Le placement.** Les distances par défaut (cordes 7 m, cuivres 15 m,
  percussions 19 m) correspondent à un plan de scène classique, mais l'équilibre
  dépend de la banque et de l'IR.
- **La transposition.** L'interpolation Hermite est propre, mais une banque
  échantillonnée tous les 5 demi-tons s'entendra quand même. Écouter les
  extrémités de chaque zone.
- **Le seuil d'humanisation.** 4 cents et 8 ms sur les cordes : à valider. Au
  delà, ça sonne « mal joué » plutôt qu'« ensemble ».

### 2. Presets

Un patch « orchestre complet » qui charge seize pupitres, les place et fixe les
canaux MIDI en un clic. C'est ce qui transforme l'outil en instrument utilisable
au quotidien.

Le format est déjà là : `getStateInformation` sérialise tout. Il manque un
navigateur et un dossier de presets d'usine.

### 3. Étendre aux autres familles

Le moteur ne fait aucune différence entre les familles : charger une banque de
cuivres dans un pupitre suffit. Ce qui reste à faire est spécifique :

- **Cuivres** — le `cuivré`/`stopped` demande une couche enregistrée dédiée, pas
  un filtre. Les swells *ff* aussi.
- **Bois** — moins de couches dynamiques nécessaires, mais le legato doit être
  plus discret (portamento vers 0,3).
- **Percussions** — legato désactivé automatiquement, `one_shot` déjà géré,
  humanisation de timing volontairement très faible. Les impacts et risers
  cinématiques n'ont besoin de rien de plus que ce qui existe.

### 4. Confort de jeu

- **Verrouillage d'articulation** par pupitre, pour ignorer les keyswitches d'une
  piste MIDI importée.
- **Transposition et limites de tessiture** par pupitre.
- **Panneau de diagnostic** plus détaillé : quelles régions ont été choisies pour
  la dernière note. Extrêmement utile pour déboguer un mapping tiers.

### 5. Format DecentSampler

Le catalogue Pianobook est vaste et gratuit, en XML trivial à parser. Le modèle
interne (`bank::Region`) est déjà indépendant du SFZ : il ne manque qu'un second
lecteur qui remplisse les mêmes structures.

### 6. Optimisations, quand elles se justifieront

Aucune n'est urgente ; toutes sont identifiées :

- **Convolution non uniforme** — supprime les 5,3 ms de latence et réduit le coût
  des queues longues. À faire si la latence gêne en jeu. Voir ADR-004.
- **FFT réelle** — ~2× sur la convolution, sans effet sur l'architecture.
- **Cache binaire de banque** — le parsing SFZ d'une grosse librairie prend une à
  deux secondes ; un cache du résultat le ramènerait à quelques dizaines de ms.
- **SIMD sur la boucle de voix** — l'interpolation et le gain se vectorisent bien.
  À mesurer d'abord : le streaming domine probablement.

### 7. Non prévu en V1, assumé

- Séquenceur intégré — la DAW le fait mieux.
- Synthèse additive/FM — hors sujet, la priorité est le réalisme échantillonné.
- iOS.
- Marketplace de presets.
- Sorties multiples (un bus par pupitre) — utile en mixage, mais change
  l'usage du mixeur et de la vue scène. À rediscuter après les presets.

## Points à tester à l'oreille, systématiquement

Les tests automatiques vérifient que le moteur fait ce qu'on lui a dit. Ils ne
peuvent pas vérifier que ce qu'on lui a dit sonne bien. À écouter après toute
modification du moteur :

| Quoi | Comment | Ce qu'on cherche |
|---|---|---|
| Legato | une ligne conjointe lente aux cordes | pas de bouillie, pas de re-attaque marquée, glissando pas caricatural |
| Round-robin | même note répétée vite | aucun effet mitraillette |
| Crossfade CC1 | note tenue, mod wheel de 0 à 127 | pas de marche, pas de re-déclenchement, ouverture de timbre crédible |
| Vol de voix | accord très dense, polyphonie basse | pas de clic, pas de note coupée net |
| Streaming | note très longue, profil Live | pas de trou, compteur d'underruns à zéro |
| Placement | même banque à 5 m puis à 25 m | la distance s'entend comme de la distance, pas comme du volume |
| Réverbe | IR de grande salle, send à 100 % | queue lisse, pas de résonance métallique |
