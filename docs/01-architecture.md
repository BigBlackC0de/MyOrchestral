# Architecture

## Principe directeur

Le cœur métier (`engine/`) est du **C++20 pur, sans aucune dépendance à JUCE**.
JUCE n'intervient que dans `plugin/`, comme adaptateur : format de plugin, I/O
audio, MIDI, UI.

Trois raisons, dans l'ordre d'importance :

1. **Testabilité.** Le moteur se compile et se teste sur n'importe quelle machine
   en quelques secondes, sans SDK plugin, sans carte son, sans DAW. Les tests
   tournent en CI sur Linux alors que la cible est macOS.
2. **Durée de vie.** JUCE 8 ne sera pas la dernière version. Le jour d'une
   migration — ou d'un portage vers CLAP, ou d'un moteur de rendu offline — c'est
   `plugin/` qui bouge, pas les 80 % de code qui comptent.
3. **Discipline temps réel.** La frontière force à expliciter ce qui a le droit
   d'allouer et ce qui n'en a pas le droit.

## Arborescence

```
MyOrchestral/
├── CMakeLists.txt          Racine : options, orchestration des sous-projets
├── cmake/                  Modules CMake (résolution de JUCE, warnings)
├── docs/                   Cette documentation + ADR
├── engine/                 Cœur — C++20 pur, zéro JUCE
│   ├── include/moe/        En-têtes publics
│   │   ├── sfz/            Parsing SFZ (lexer, opcodes, expansion des <group>)
│   │   ├── bank/           Modèle d'instrument, index de régions, chargeur
│   │   ├── audio/          Décodage de fichiers (WAV ; FLAC en V2)
│   │   ├── stream/         Pool mémoire, préchargement d'attaques, streaming
│   │   ├── voice/          Enveloppes, voix, allocateur de voix
│   │   ├── perf/           État MIDI, keyswitches, détection de legato
│   │   ├── dsp/            Interpolation, panoramique, lissage, convolution
│   │   ├── space/          Placement sur scène (distance, réflexions, largeur)
│   │   └── humanize/       Micro-variations pitch/timing/timbre
│   └── src/
├── plugin/                 Adaptateur JUCE
│   └── source/
│       ├── PluginProcessor.*   Pont DAW ↔ moteur
│       ├── PluginEditor.*      Fenêtre principale
│       ├── state/              Paramètres automatisables, sérialisation
│       └── ui/                 Composants, thème
├── tests/                  doctest — tests unitaires du moteur
├── tools/                  Scripts Python (banque placeholder, validation SFZ)
└── banks/                  Banques de samples (non versionné)
```

## Chaîne de traitement d'une note

```
  MIDI de la DAW
        │
        ▼
  ┌───────────────┐   noteOn(canal, note, vélocité)
  │  MidiState    │   suit : notes tenues, CC1/CC11, pédale, aftertouch
  └───────┬───────┘
          ▼
  ┌────────────────────┐  la note est-elle un keyswitch ?
  │  KeyswitchRouter   │  si oui : change l'articulation courante, pas de son
  └───────┬────────────┘
          ▼
  ┌────────────────────┐  la note précédente est-elle encore tenue ?
  │  LegatoDetector    │  → mode LEGATO (portamento + attaque atténuée)
  └───────┬────────────┘  → sinon mode NORMAL
          ▼
  ┌────────────────────┐  filtre : lokey/hikey, lovel/hivel, sw_last,
  │  RegionIndex       │  seq_position (round-robin), articulation
  │  (sélection)       │  → une ou plusieurs régions (couches à crossfader)
  └───────┬────────────┘
          ▼
  ┌────────────────────┐  vole une voix si le pool est plein (politique :
  │  VoiceManager      │  plus ancienne relâchée > plus ancienne > plus faible)
  └───────┬────────────┘
          ▼
  ┌────────────────────┐  attaque déjà en RAM (préchargée) ; la suite arrive
  │  StreamingSource   │  par un thread de fond dans un ring buffer
  └───────┬────────────┘
          ▼
  ┌────────────────────┐  Hermite 4 points — pitch, portamento, humanisation
  │  Interpolation     │
  └───────┬────────────┘
          ▼
  ┌────────────────────┐  ADSR + crossfade dynamique piloté CC1
  │  Envelope / xfade  │
  └───────┬────────────┘
          ▼
  ┌────────────────────┐  position du pupitre : gain de distance, pré-délai,
  │  StagePosition     │  premières réflexions, largeur stéréo
  └───────┬────────────┘
          ▼
  ┌────────────────────┐  bus par pupitre → send réverbe → sortie stéréo
  │  Mixer + Convolver │
  └────────────────────┘
```

## Contrat temps réel

Le thread audio ne fait **jamais** : `new`/`delete`, verrou bloquant, I/O
fichier, allocation implicite (`std::vector::push_back`, `std::string`,
`std::function` capturante).

Conséquences architecturales :

- **Voix pré-allouées.** `VoiceManager` construit son pool à `prepare()`. Une
  note qui arrive alors que le pool est plein vole une voix ; elle n'en alloue
  jamais une nouvelle.
- **Trois threads.**
  - *Audio* — rendu. Priorité temps réel, jamais bloqué.
  - *Streaming* — remplit les ring buffers depuis le disque. Réveillé par
    condition variable, jamais depuis le thread audio.
  - *Chargement* — parse le SFZ, ouvre les fichiers, précharge les attaques.
    Lent, hors ligne, publie son résultat par échange de pointeur atomique.
- **Publication atomique.** Changer de banque construit un `LoadedBank` complet
  hors ligne, puis échange un pointeur. Le thread audio ne voit jamais un état
  intermédiaire. L'ancienne banque est libérée par le thread de chargement une
  fois que plus aucune voix ne la référence.

## Sélection de région — l'endroit qui compte

`RegionIndex` pré-calcule, au chargement, une table indexée par
`[articulation][note MIDI]` pointant vers les régions candidates. À l'exécution
la sélection est un parcours de quelques éléments contigus, pas un scan des
milliers de régions de la banque. C'est ce qui rend le coût d'un `noteOn`
prévisible même sur une banque de 30 Go.

Filtrage dans l'ordre : articulation → étendue de notes → étendue de vélocité →
round-robin (`seq_position` avec compteur par groupe) → conditions `sw_*`.

Les régions survivantes ne sont pas exclusives : plusieurs couches dynamiques
peuvent sonner simultanément avec des gains complémentaires (crossfade CC1),
ce qui est le mécanisme central de l'expressivité orchestrale.

## Streaming disque

Chaque région déclare un `preload_size` (défaut : 64 ko par canal, soit ~370 ms
à 44,1 kHz). Ce début de fichier vit en RAM en permanence : une note démarre
donc **toujours** sans toucher le disque, ce qui borne la latence de déclenchement
indépendamment du support de stockage.

Au-delà, un ring buffer par voix est alimenté par le thread de streaming. Le seuil
de remplissage et la taille du ring sont exposés dans le réglage
« qualité vs latence » :

| Mode | Ring | Préchargement | Usage |
|---|---|---|---|
| Live | 32 ko | 32 ko | jeu clavier, latence minimale, plus de disque |
| Équilibré | 128 ko | 64 ko | défaut |
| Rendu | 512 ko | 128 ko | mixage/export, robustesse maximale |

Si un ring se vide malgré tout (disque saturé), la voix applique un fondu de
sortie court plutôt que de produire un clic — une dropout doit s'entendre comme
une note qui s'éteint, pas comme un défaut.

## Multi-timbral

Une instance = un `OrchestraEngine` contenant N `Section`. Chaque `Section` a :
son canal MIDI, sa banque, son articulation courante, sa position sur scène, son
gain/pan/mute/solo, son send réverbe.

Deux modes d'usage, sans différence de code :
- **mono-instrument** — une instance par pupitre, canal MIDI omni ; c'est le mode
  recommandé en DAW (meilleur parallélisme, automation plus lisible) ;
- **orchestre complet** — une instance, 16 sections sur 16 canaux MIDI.

## État et automation

Les paramètres automatisables (gains, pans, sends, largeur, position) vivent dans
un `AudioProcessorValueTreeState`. Ce qui n'est pas automatisable — chemin de
banque, mapping des keyswitches, IR chargée — vit dans un `ValueTree` annexe
sérialisé avec l'état.

Règle : le moteur ne lit jamais un paramètre JUCE directement. `PluginProcessor`
copie les valeurs dans une structure `EngineParams` simple au début de chaque
bloc. Le moteur reste ainsi testable sans JUCE, et le coût de lecture des
paramètres est payé une fois par bloc plutôt qu'une fois par échantillon.
