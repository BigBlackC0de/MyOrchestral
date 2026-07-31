# ADR-003 — Têtes préchargées en RAM, queue en streaming

**Statut** : acté · **Date** : 2026-07-30

## Contexte

Une librairie orchestrale dépasse la RAM disponible. Trois approches : tout en
RAM (impossible au-delà de quelques Go), tout en streaming (le déclenchement
dépend alors de la latence disque), ou l'hybride.

## Décision

Les `preload_size` premières trames de chaque région restent résidentes ; la
suite arrive par un thread de streaming dans un ring buffer par voix.

## Raisons

Une note démarre **toujours** depuis la RAM. La latence de déclenchement est
donc bornée et indépendante du support de stockage — sur un disque externe
lent comme sur un SSD interne. Le budget mémoire devient prévisible :
`nombre de régions × preload_size`, indépendant de la longueur des samples.

Le préchargement par défaut (32 768 trames, ~740 ms à 44,1 kHz) donne au thread
de streaming une marge très large pour ouvrir le fichier et remplir le ring.

## Conséquences

- Trois profils exposés à l'utilisateur (Live / Équilibré / Rendu) qui règlent
  ensemble taille de préchargement, taille de ring et granularité de recharge.
- Le pool de slots de streaming est fixe : acquérir un slot doit être possible
  depuis le thread audio, donc sans allocation. Pool saturé = la voix joue sa
  tête préchargée puis s'éteint en fondu. Dégradation audible mais propre,
  jamais un blocage du thread audio.
- Un ring vidé produit un court fondu, pas un clic. Une dropout doit s'entendre
  comme une note qui s'éteint, pas comme un défaut.
