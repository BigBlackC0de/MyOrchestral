# ADR-001 — Le moteur ne dépend pas de JUCE

**Statut** : acté · **Date** : 2026-07-30

## Contexte

JUCE fournit tout ce qu'il faut pour écrire un sampler : `AudioBuffer`,
`Synthesiser`, lecteurs de fichiers, FFT, threads. Partir de `juce::Synthesiser`
aurait fait gagner plusieurs jours.

## Décision

Le cœur (`engine/`) est du C++20 pur. JUCE n'apparaît que dans `plugin/`, comme
adaptateur.

## Raisons

1. **Testabilité.** Le moteur se compile et se teste en quelques secondes sur
   n'importe quelle machine, sans SDK plugin, sans carte son, sans DAW. C'est ce
   qui permet à la CI de tourner sur Linux alors que la cible est macOS — et
   c'est cette contrainte qui a fait remonter deux bugs réels (lissage bloqué en
   flottant, taille minimale du ring buffer) avant qu'ils n'atteignent l'audio.
2. **Durée de vie.** JUCE 8 ne sera pas la dernière version. Un portage CLAP, un
   moteur de rendu hors ligne ou une migration de framework touche `plugin/`,
   pas les 80 % de code qui comptent.
3. **Discipline temps réel.** La frontière force à expliciter ce qui a le droit
   d'allouer. `juce::Synthesiser` alloue dans `noteOn` ; le moteur ici ne le peut
   pas, structurellement.

## Conséquences

- Un lecteur WAV, une FFT et une convolution ont dû être écrits. ~600 lignes,
  toutes couvertes par des tests.
- La FFT est ~2× plus lente qu'une implémentation réelle-vers-complexe optimisée.
  Mesurable, pas gênant : voir ADR-004.
- Les conversions `juce::AudioBuffer` ↔ `float* const*` sont explicites. C'est
  du bruit visuel, pas un coût d'exécution.
