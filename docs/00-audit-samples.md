# Audit de faisabilité — la question des samples

> **À lire avant toute autre chose.** Ce document répond au point de vigilance
> soulevé dans le cahier des charges et conditionne toutes les décisions
> techniques qui suivent.

## 1. Le constat, sans détour

Le réalisme d'un orchestre virtuel vient **d'abord de la matière sonore**, pas du
moteur. Un moteur de sample-playback excellent joue ce qu'on lui donne : si on lui
donne une note de violon tenue enregistrée une seule fois à une seule nuance, il
restituera fidèlement… une note de violon tenue enregistrée une seule fois à une
seule nuance. Aucune quantité de code ne fabrique les informations absentes de
l'enregistrement.

Concrètement, ce qui sépare une librairie « gratuite correcte » d'une librairie
professionnelle type Spitfire/EastWest/Orchestral Tools :

| Dimension | Librairie libre typique | Librairie pro (Zimmer-grade) |
|---|---|---|
| Couches de vélocité par note | 1 à 3 | 4 à 8, avec crossfade continu piloté au CC1 |
| Round-robin (variantes d'une même note) | 0 à 2 | 4 à 8, souvent × plusieurs micros |
| Transitions legato **échantillonnées** | quasi jamais | oui, par intervalle et par vitesse |
| Articulations par pupitre | 3 à 6 | 15 à 40 |
| Positions de micros | 1 (stéréo mixée) | 3 à 8 (close / tree / ambient / outriggers) |
| Taille | 0,5 – 3 Go | 30 – 200 Go+ par librairie |
| Effectif enregistré | souvent 2–4 musiciens multipliés | 60 cordes réellement en salle |

Le poste « transitions legato échantillonnées » est le plus déterminant pour les
cordes : c'est lui qui fait entendre un pupitre qui *joue une phrase* plutôt
qu'une suite de notes. Il est presque toujours absent du libre.

**Traduction en attente réaliste :** avec des samples libres et le moteur décrit
ici, on obtient une maquette convaincante — largement de quoi composer, faire
écouter, valider des idées, et sonner correctement dans un mix chargé. On
n'obtient pas un rendu de bande-annonce indiscernable d'un vrai orchestre. Cet
écart n'est pas rattrapable par le code.

Nuance importante pour ton genre cible : la musique épique/trailer est
**beaucoup plus tolérante** que, disons, un quatuor à cordes exposé. Elle
s'appuie sur des ostinatos, des nappes larges, des couches de percussions, de la
saturation, de la réverbe longue et des sub-hits. Une bonne partie de son
identité vient du **traitement**, pas seulement de la source. C'est un terrain où
des samples libres bien exploités montent nettement plus haut que la moyenne.

## 2. Ce qui est légalement et techniquement exploitable

### 2.1 Utilisable directement dans un moteur custom (format ouvert)

Ce moteur lit du **SFZ** — format texte ouvert et documenté. Toute librairie
distribuée en SFZ est donc branchable. Options concrètes :

| Librairie | Licence | Taille | Ce que ça couvre | Verdict |
|---|---|---|---|---|
| **VSCO 2 Community Edition** (Versilian Studios) | **CC0** (domaine public) | ~1,5 Go | Orchestre de chambre complet : cordes (solo + ensembles), bois, cuivres, percussions | **Meilleur point de départ.** CC0 = redistribuable sans condition, y compris dans un installeur |
| **Virtual Playing Orchestra** (Simon Dalzell) | gratuite, usage commercial autorisé | ~1,5 Go | Orchestre complet, mappings SFZ soignés, quelques articulations par pupitre | Excellente qualité de mapping ; agrège plusieurs sources libres |
| **Sonatina Symphonic Orchestra** | CC Sampling Plus 1.0 | ~500 Mo | Orchestre complet, ancienne mais cohérente | Utile en complément ; licence à relire avant redistribution |
| **VSCO 2 Pro** | payante (prix modeste) | ~12 Go | Version étendue de VSCO 2 | Bon rapport qualité/prix si le CE convainc |
| **Pianobook** (format DecentSampler `.dspreset`, XML ouvert) | variable, souvent libre | variable | Milliers d'instruments communautaires, dont beaucoup de textures orchestrales et de percussions | Format XML trivial à parser — support prévu en V2 |

### 2.2 Matière première pour construire ses propres banques

- **University of Iowa Electronic Music Studios** — enregistrements chromatiques
  note à note de la plupart des instruments d'orchestre, qualité studio, plusieurs
  nuances. Ce ne sont pas des banques prêtes à l'emploi : il faut découper, mapper,
  boucler. C'est la meilleure matière brute libre disponible.
- **Philharmonia Orchestra sample library** — ~13 000 notes isolées, très propres.
  Licence orientée usage personnel/éducatif : à vérifier avant tout usage
  commercial ou redistribution.
- **Tes propres enregistrements** — un seul instrumentiste bien enregistré,
  multiplié et désaccordé par le moteur, bat une section libre médiocre.

Le pipeline `tools/` de ce projet est prévu pour ça : découpage automatique,
détection de pitch, génération du SFZ.

### 2.3 Réverbération à convolution — réponses impulsionnelles

- **OpenAIR** (openairlib.net) — IR de vraies salles, majoritairement CC-BY.
  Contient des salles de concert et des espaces très longs utiles pour l'épique.
- **EchoThief** — IR gratuites, espaces variés et parfois spectaculaires.

Ces IR sont légères (quelques Mo) et redistribuables sous réserve d'attribution.

### 2.4 Ce qui est **hors de portée** — à acter définitivement

- **Kontakt** (EastWest, Spitfire, CineSamples, Audio Imperia, 8Dio…) : les
  échantillons sont dans des conteneurs `.nkx`/`.ncw` chiffrés et propriétaires.
  Les lire depuis un moteur tiers demanderait de casser la protection : c'est
  illégal (contournement de mesure technique de protection) et hors sujet ici.
- **Spitfire LABS / BBC SO Discover**, **Orchestral Tools SINE**, **Native
  Instruments Play series** : gratuits mais enfermés dans leur propre lecteur.
  Ils ne sont pas « des samples gratuits », ce sont des plugins fermés.
- Autrement dit : **acheter une librairie commerciale ne la rend pas
  utilisable dans ce plugin.** Une librairie commerciale s'utilise dans son
  lecteur, en parallèle du nôtre — jamais dedans, sauf si elle est explicitement
  distribuée en SFZ ou en WAV nus.

## 3. Les trois chemins possibles

### Chemin A — 100 % libre, redistribuable
Base **VSCO 2 CE (CC0)**, complétée par Virtual Playing Orchestra.
- Coût : 0 €. Aucun risque juridique (CC0).
- Rendu : maquette solide, très correcte en contexte épique dense, faible en
  cordes exposées.
- Le plugin peut être distribué avec ses samples inclus.

### Chemin B — libre + banques perso
Chemin A, plus tes propres enregistrements (ou Iowa/Philharmonia retravaillés)
sur les pupitres qui comptent le plus pour toi — typiquement les cordes hautes et
les cors.
- Coût : temps, éventuellement quelques heures de studio/musicien.
- C'est le meilleur rapport effort/réalisme à moyen terme.

### Chemin C — moteur custom + librairies SFZ payantes
Le moteur reste le même ; on y branche des banques SFZ commerciales.
- L'offre SFZ commerciale reste limitée comparée à l'écosystème Kontakt.
- Réaliste surtout pour les percussions, où le SFZ payant est mieux fourni.

**Recommandation :** démarrer sur **A** pour valider toute la chaîne, puis
glisser vers **B** sur deux ou trois pupitres critiques. Le moteur est conçu pour
que ce basculement ne coûte rien : il suffit de pointer une autre banque.

## 4. Décision à valider de ton côté

Une seule question bloque la suite, et elle est stratégique plus que technique :

> **Est-ce que ce plugin est destiné à être distribué à d'autres (vendu ou
> partagé), ou est-ce un outil personnel ?**

- **Outil personnel** → tu peux utiliser librement toute banque SFZ que tu
  possèdes, y compris à licence restrictive. Contrainte quasi nulle.
- **Distribution** → il faut que tout ce qui est empaqueté soit CC0 ou
  explicitement redistribuable. En pratique : VSCO 2 CE, et attribution soignée
  pour les IR CC-BY.

En attendant ta réponse, le développement avance sans être bloqué : le MVP
tourne sur une **banque placeholder générée par code** (voir §5), et le moteur ne
présuppose aucune banque particulière.

## 5. Ce qui permet de démarrer aujourd'hui, sans rien télécharger

`tools/make_placeholder_bank.py` synthétise une banque de cordes jouable —
modèle physique simplifié d'archet (excitation bruitée filtrée + résonances
harmoniques + corps), avec plusieurs couches de vélocité, du round-robin et
plusieurs articulations. Elle produit des WAV et le SFZ correspondant.

Ce n'est pas beau. Ce n'est pas le but. Le but est que **toute la chaîne soit
validable immédiatement** — MIDI → keyswitch → sélection de région → streaming
disque → enveloppe → panoramique de scène → réverbe → sortie — et que le jour où
tu déposes VSCO 2 dans le dossier `banks/`, il n'y ait rien d'autre à faire que
de le sélectionner.

## 6. Ce que le moteur peut compenser (et ce qu'il ne peut pas)

**Compensable par le code, et implémenté ici :**
- absence de transitions legato échantillonnées → legato synthétique
  (détection de recouvrement, portamento, re-attaque atténuée, crossfade) ;
- pauvreté du round-robin → micro-variations de pitch, de timing et de timbre
  qui cassent l'effet mitraillette ;
- section trop petite → empilement désaccordé/décalé (« divisi » synthétique) ;
- absence de salle → réverbe à convolution + modèle de placement sur scène
  (distance, premières réflexions, largeur stéréo par pupitre).

**Non compensable :**
- le timbre d'un vrai *fortissimo* de cuivres (le spectre change, ce n'est pas
  un question de volume) — sans couche ff enregistrée, ça n'existera pas ;
- le bruit d'archet, les respirations, le grain d'ensemble ;
- les articulations jamais enregistrées.

C'est précisément pour ça que le moteur expose un **crossfade dynamique piloté au
CC1** : sur une banque riche, c'est le paramètre qui porte l'essentiel de
l'expressivité.
