# ADR-004 — Convolution à partitions uniformes de 256 trames

**Statut** : acté, à revoir · **Date** : 2026-07-30

## Contexte

La réverbe à convolution est indispensable au réalisme orchestral. Trois
implémentations possibles : directe (impraticable), partitions uniformes, ou
partitions non uniformes (Gardner).

## Décision

Partitions uniformes de 256 trames, FFT de 512 points. Latence : 256
échantillons (~5,3 ms à 48 kHz), déclarée à l'hôte.

## Raisons

C'est le meilleur rapport complexité/résultat pour une V1. Le schéma tient en
150 lignes testables, et 5 ms restent jouables en live.

Un schéma non uniforme (petites partitions au début, grandes ensuite) supprime
la latence et réduit le coût des queues longues — mais c'est nettement plus de
code, et pour une décision qui n'affecte pas l'architecture autour.

## Conséquences

- 5,3 ms de latence. L'hôte compense à la lecture ; en jeu live, c'est perceptible
  mais acceptable. Sans IR chargée, la latence déclarée est nulle.
- Le coût CPU croît linéairement avec la longueur d'IR. Une queue de 4 s à 48 kHz
  représente ~750 partitions. Le réglage de longueur de queue est le levier
  direct pour arbitrer.
- La FFT est complexe→complexe, donc ~2× plus de travail que nécessaire pour un
  signal réel. Optimisation évidente, non faite : le gain serait invisible tant
  que le schéma de partitionnement domine.
- L'entrée de réverbe est un mono sommé ; les deux canaux d'IR sont conservés,
  donc la queue reste stéréo. Inaudible pour une simulation de salle, moitié
  moins de FFT.

## À revoir quand

Le partitionnement non uniforme devient prioritaire si la latence gêne en jeu,
ou si l'empilement de plusieurs instances devient coûteux.
