# Réponses impulsionnelles

Non versionnées. Formats acceptés : WAV mono ou stéréo, n'importe quel taux
d'échantillonnage (rééchantillonné au chargement) et n'importe quelle résolution.

## Où en trouver, librement

- **OpenAIR** (openairlib.net) — vraies salles, majoritairement CC-BY. Contient
  des salles de concert et des espaces très longs, utiles pour l'épique.
- **EchoThief** — gratuites, espaces variés et parfois spectaculaires.

Vérifier la licence au téléchargement si le plugin est destiné à être distribué :
CC-BY impose une attribution.

## Choisir

Pour de l'orchestral, chercher une salle de concert de 1,8 à 2,6 s de RT60. Au
delà, la définition se perd dans les passages rapides ; en dessous, l'orchestre
sonne petit.

Le moteur normalise l'énergie de l'IR au chargement, donc changer d'IR ne change
pas le niveau du send.

Le réglage de longueur de queue est le levier CPU principal : le coût est
proportionnel à la durée de l'IR.
