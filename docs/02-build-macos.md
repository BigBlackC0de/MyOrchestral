# Compiler et installer sur macOS

## Prérequis

| Outil | Version | Comment |
|---|---|---|
| macOS | 12 (Monterey) ou plus | cible de déploiement du projet |
| Xcode | 14+ | App Store, puis `sudo xcode-select --install` |
| CMake | 3.22+ | `brew install cmake` |
| Git | | fourni avec Xcode |
| Python 3 | 3.9+ | pour la banque placeholder ; `brew install python numpy` |

JUCE n'a pas besoin d'être installé : CMake le télécharge à la première
configuration. Si tu en as déjà une copie, voir « Accélérer la configuration »
plus bas.

## Compilation

```bash
git clone <ton-remote> MyOrchestral
cd MyOrchestral

cmake -B build -G Xcode
cmake --build build --config Release
```

C'est tout. La première configuration prend quelques minutes (téléchargement de
JUCE et construction de `juceaide`) ; les suivantes sont immédiates.

Les artefacts atterrissent dans :

```
build/plugin/MyOrchestral_artefacts/Release/
├── VST3/MyOrchestral.vst3
├── AU/MyOrchestral.component
└── Standalone/MyOrchestral.app
```

`COPY_PLUGIN_AFTER_BUILD` est activé : le VST3 et l'AU sont **automatiquement
copiés** dans `~/Library/Audio/Plug-Ins/`. Il n'y a rien à installer à la main.

### Universal Binary (Intel + Apple Silicon)

C'est le comportement par défaut : le projet force
`CMAKE_OSX_ARCHITECTURES=arm64;x86_64`. Vérification :

```bash
lipo -info build/plugin/MyOrchestral_artefacts/Release/VST3/MyOrchestral.vst3/Contents/MacOS/MyOrchestral
# -> Architectures in the fat file: x86_64 arm64
```

Pour ne compiler qu'une architecture pendant le développement (compilation
deux fois plus rapide) :

```bash
cmake -B build-arm -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64
```

### Build en ligne de commande (sans Xcode IDE)

Le générateur Ninja est nettement plus rapide en itératif :

```bash
brew install ninja
cmake -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja
```

Note : avec Ninja, `CMAKE_BUILD_TYPE` se fixe à la configuration ; avec Xcode,
c'est `--config` à la compilation.

### Accélérer la configuration

Le téléchargement de JUCE se refait pour chaque nouveau dossier de build. Avec
une copie locale :

```bash
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git ~/dev/JUCE
cmake -B build -G Xcode -DJUCE_SOURCE_DIR=~/dev/JUCE
```

Ou place-la dans `external/JUCE` : elle est trouvée automatiquement.

## Tests

Le moteur se teste sans JUCE, sans carte son et sans banque de samples : les
fixtures sont synthétisées à l'exécution.

```bash
cmake -B build-tests -DMOE_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Environ 15 secondes. À lancer avant chaque commit ; c'est aussi la boucle de
développement la plus rapide pour tout ce qui touche au moteur.

## Première utilisation

### 1. Générer une banque jouable

```bash
python3 tools/make_placeholder_bank.py --output banks/placeholder-strings
```

Compter ~1 minute avec NumPy, quelques minutes sans. `--quick` réduit à 5 notes
pour un premier essai.

### 2. Vérifier hors DAW

```bash
cmake --build build-ninja --target moe_render

./build-ninja/tools/moe_render \
    --bank banks/placeholder-strings/placeholder-strings.sfz \
    --out /tmp/test.wav --notes 48,55,60,64 --seconds 3
open /tmp/test.wav
```

Si ça sonne ici, le moteur va bien : tout problème rencontré ensuite dans une
DAW vient de l'hôte ou du chargement, pas du rendu.

### 3. Dans la DAW

**Logic Pro** — l'AU est validé au démarrage. S'il n'apparaît pas :

```bash
# Voir ce que Logic voit
auval -a | grep -i myorchestral

# Validation détaillée (remplacer par les codes du plugin)
auval -v aumu Morc Myor
```

Si `auval` échoue, Logic mettra le plugin en liste noire. Après correction :

```bash
rm -rf ~/Library/Caches/AudioUnitCache
killall -9 AudioComponentRegistrar
```

**Ableton Live / Reaper / Cubase** — VST3, détecté dans
`~/Library/Audio/Plug-Ins/VST3`. Dans Live : Préférences → Plug-Ins → Rescan.

**Standalone** — le plus simple pour tester au clavier sans DAW. Choisir
l'entrée MIDI dans le menu Audio/MIDI Settings.

### 4. Charger une vraie banque

Bouton `Load bank...` dans le panneau instrument, sélectionner un `.sfz`. Une
banque de plusieurs Go met quelques secondes : le chargement tourne sur un
thread séparé, l'interface reste réactive et l'audio n'est jamais interrompu.

## Validation avant distribution

```bash
brew install --cask pluginval
pluginval --strictness-level 8 --validate-in-process \
    build/plugin/MyOrchestral_artefacts/Release/VST3/MyOrchestral.vst3
```

`pluginval` teste ce que les tests unitaires ne peuvent pas : changements de
buffer size en cours de lecture, sample rates exotiques, appels de paramètres
depuis plusieurs threads, sauvegarde/restauration d'état. À lancer avant toute
diffusion.

## Signature et notarisation

Nécessaire seulement pour distribuer à d'autres machines. Pour un usage
personnel, macOS accepte un binaire non signé compilé localement.

```bash
codesign --force --deep --options runtime --timestamp \
    --sign "Developer ID Application: TON NOM (TEAMID)" \
    build/plugin/MyOrchestral_artefacts/Release/VST3/MyOrchestral.vst3

xcrun notarytool submit MyOrchestral.vst3.zip \
    --apple-id ton@email --team-id TEAMID --wait
xcrun stapler staple build/.../MyOrchestral.vst3
```

Demande un compte Apple Developer payant.

## AAX (Pro Tools)

Non activé, faute de SDK disponible publiquement. Une fois le SDK Avid obtenu :

```cmake
# dans plugin/CMakeLists.txt
FORMATS VST3 AU AAX Standalone
```

```bash
cmake -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DJUCE_AAX_SDK_PATH=/chemin/vers/aax-sdk
```

AAX exige en plus une signature PACE, qui suppose un compte développeur Avid.

## Problèmes fréquents

**« JUCE fetch failed »** — pas de réseau à la configuration. Cloner JUCE
manuellement et passer `-DJUCE_SOURCE_DIR=`.

**Le plugin n'apparaît pas dans la DAW** — vérifier qu'il est bien dans
`~/Library/Audio/Plug-Ins/`, puis relancer un scan. Logic exige `auval`.

**Craquements en jeu** — passer `Quality` sur `Live` dans la barre du haut, et
augmenter le buffer de la DAW. Surveiller le compteur `underruns` en bas de la
fenêtre : s'il grimpe, le disque ne suit pas.

**Le compteur `steals` grimpe** — la polyphonie est saturée. C'est audible
comme des notes coupées. Réduire le nombre de couches empilées, ou augmenter la
polyphonie maximale.

**Xcode 15 et les warnings de link** — sans effet sur le fonctionnement ;
`-Wl,-ld_classic` dans les flags de link les fait taire si besoin.
