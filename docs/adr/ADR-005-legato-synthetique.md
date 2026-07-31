# ADR-005 — Legato synthétique par défaut

**Statut** : acté · **Date** : 2026-07-30

## Contexte

Le legato échantillonné — une prise par intervalle et par vitesse — est ce qui
fait entendre un pupitre qui *joue une phrase* plutôt qu'une suite de notes. Il
est quasi absent des banques libres, qui sont le point de départ de ce projet.

## Décision

Le moteur détecte le recouvrement de notes et applique trois choses ensemble :
un portamento vers la nouvelle hauteur, une attaque adoucie, et un relâchement
court sur la note quittée.

Les régions `trigger=legato` restent prioritaires : une banque qui fournit de
vraies transitions les utilise, la simulation ne s'applique qu'en repli.

## Raisons

Les trois effets comptent, et le troisième le plus : sans retrait de la note
précédente, une ligne legato empile les notes et devient une bouillie
harmonique. Ce n'est pas ce que fait un pupitre monophonique.

Les intervalles au-delà d'une octave ne glissent pas : un instrumentiste ne
glisse pas sur deux octaves, et le faire s'entend immédiatement comme un défaut.

## Conséquences

- La quantité de portamento est un paramètre : ~0,85 convient aux cordes,
  ~0,45 aux cuivres et bois, 0 aux percussions (désactivé automatiquement selon
  la famille détectée).
- Ce n'est pas du legato échantillonné et ça ne l'imite pas parfaitement. Le
  changement de timbre pendant une transition réelle n'existe pas ici. Sur une
  banque professionnelle, `trigger=legato` prend le relais.
- La détection ne coûte rien : elle lit l'état MIDI déjà tenu à jour.
