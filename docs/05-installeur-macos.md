# Installeur macOS (.dmg) et reconnaissance par Logic Pro

## Fabriquer le DMG

```bash
./packaging/make_installer.sh
```

Ça produit `MyOrchestral-0.1.0.dmg` à la racine. Le script :

1. compile en Release, **universal binary** (arm64 + x86_64), cible macOS 12 ;
2. **vérifie** avec `lipo` que les deux architectures sont bien présentes, et
   échoue sinon — un plugin qui refuse silencieusement de se charger sur l'autre
   architecture est pénible à diagnostiquer à distance ;
3. construit un package par format ;
4. les assemble en un `.pkg` avec choix des formats à l'installation ;
5. enveloppe le tout dans un `.dmg`.

Compter 5 à 10 minutes la première fois (compilation des deux architectures).

## Pourquoi un .pkg dans un .dmg, et pas un glisser-déposer

Un plugin doit atterrir dans **deux dossiers système différents** :

| Format | Destination | Qui le lit |
|---|---|---|
| Audio Unit | `/Library/Audio/Plug-Ins/Components` | Logic Pro, GarageBand, MainStage |
| VST3 | `/Library/Audio/Plug-Ins/VST3` | Live, Cubase, Reaper, Studio One, Bitwig |

Un DMG en glisser-déposer ne sait pas faire ça : il pointe vers un seul dossier.
C'est pour cette raison que tous les plugins commerciaux livrent un installeur,
et pas une icône à traîner dans Applications.

L'installation dans `/Library` demande le mot de passe administrateur. C'est le
comportement normal et attendu.

## Reconnaissance par Logic Pro

Logic ne scanne les nouveaux Audio Units **qu'au démarrage**. Après
l'installation :

1. quitter Logic complètement (⌘Q, pas juste fermer la fenêtre) ;
2. le relancer — une barre « Validation des unités audio » apparaît quelques
   secondes ;
3. le plugin est alors dans **Instrument → AU Instruments → MyOrchestral →
   Stéréo**.

### S'il n'apparaît pas

Logic ne dit jamais pourquoi il a rejeté un plugin. `auval` le dit :

```bash
auval -v aumu Morc Myor
```

Trois cas :

**« PASS »** mais absent de Logic → cache corrompu :

```bash
rm -rf ~/Library/Caches/AudioUnitCache
killall -9 AudioComponentRegistrar
```

puis relancer Logic.

**« FAIL »** → le détail de l'échec est dans la sortie. Logic met alors le
plugin en liste noire, et il y reste même après correction tant que le cache
n'est pas vidé (commande ci-dessus).

**Rien du tout / composant introuvable** → le `.component` n'est pas au bon
endroit. Vérifier :

```bash
ls -la /Library/Audio/Plug-Ins/Components/MyOrchestral.component
```

## Signature et Gatekeeper — le point important

Le script fonctionne **sans** compte développeur Apple, et le DMG produit
s'installe parfaitement **sur la machine qui l'a compilé**.

En revanche, un DMG non signé et non notarisé qui **voyage** vers une autre
machine (téléchargement, AirDrop, clé USB) est mis en quarantaine par
Gatekeeper. L'utilisateur voit « impossible d'ouvrir, développeur non
identifié ».

Contournement ponctuel, côté destinataire :

```bash
xattr -dr com.apple.quarantine ~/Downloads/MyOrchestral-0.1.0.dmg
```

Solution propre, si tu veux vraiment distribuer — il faut un **compte Apple
Developer payant** (99 €/an) :

```bash
# Une fois : enregistrer les identifiants de notarisation
xcrun notarytool store-credentials my-profile \
    --apple-id ton@email.com --team-id TONTEAMID

# À chaque build
SIGN_ID="Developer ID Application: Ton Nom (TONTEAMID)" \
INSTALLER_ID="Developer ID Installer: Ton Nom (TONTEAMID)" \
NOTARY_PROFILE=my-profile \
./packaging/make_installer.sh
```

Le script signe alors les bundles, signe le `.pkg`, soumet à Apple, attend le
verdict et agrafe le ticket. Le DMG s'installe ensuite sans avertissement sur
n'importe quel Mac.

**Si le plugin reste pour toi seul, tout ça est inutile.** Compile, lance le
script, installe : c'est fini.

## Après l'installation

Le plugin ne contient **aucun sample** — il en lit. Il faut donc une banque SFZ :

```bash
python3 tools/make_placeholder_bank.py --output ~/Music/MyOrchestral/placeholder
```

Puis, dans le plugin, `Load bank...` et choisir le `.sfz`. Voir
[00-audit-samples.md](00-audit-samples.md) pour passer à une vraie librairie.

## Désinstaller

```bash
sudo rm -rf /Library/Audio/Plug-Ins/Components/MyOrchestral.component
sudo rm -rf /Library/Audio/Plug-Ins/VST3/MyOrchestral.vst3
rm -rf ~/Library/Caches/AudioUnitCache
```
