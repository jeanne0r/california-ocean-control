# California Ocean 2023 · interface camping

Une interface web locale et un firmware **ESP32 en lecture seule** pour observer le bus CAN de l'espace camping d'un Volkswagen California 6.1 Ocean 2023. Le projet utilise la **WeAct CAN485 DevBoard V1.1**, son Wi-Fi et une microSD.

> **État du projet : exploration.** La carte et l'interface fonctionnent sur table. La réception sur le véhicule, le point de raccordement et les trames propres à cet Ocean 2023 restent à valider. Aucun bouton de l'interface n'envoie de commande au véhicule.

[Voir l'interface de démonstration](https://california-ocean-camping-2023.romain-jeanneret.chatgpt.site) · [Télécharger le firmware](dist/flash/california-ocean-weact-v1.1-can-auto.bin) · [Télécharger l'interface microSD](dist/flash/microsd-interface.zip)

## Ce qui fonctionne

| Fonction | État |
| --- | --- |
| Page web servie par l'ESP32 depuis la microSD | Prête, vérifiée sur la carte |
| Point d'accès Wi-Fi avec mot de passe aléatoire à chaque démarrage | Prêt, vérifié sur la carte |
| Réception CAN avec recherche automatique à 125, 250 et 500 kbit/s | Firmware compilé, à vérifier sur le véhicule |
| Affichage des trames récentes et compteur de pertes du pilote | Prêt, à vérifier avec un bus actif |
| Enregistrement et téléchargement de fichiers CSV sur microSD | Prêt, à vérifier avec un bus actif |
| Éclairage, chauffage, réfrigérateur, eau, batteries et toit | Interface de démonstration uniquement |

Le pilote CAN est configuré en `TWAI_MODE_LISTEN_ONLY`. La file d'émission est désactivée et le firmware ne contient aucun appel à `twai_transmit`. Les captures sont limitées à **10 Mo par fichier**.

## Démarrage rapide

### 1. Flasher la WeAct

Retirer la microSD pendant le flash, conformément à la [notice du fabricant](https://github.com/WeActStudio/WeActStudio.CAN485DevBoardV1_ESP32). Brancher la carte en USB-C et flasher le [binaire complet](dist/flash/california-ocean-weact-v1.1-can-auto.bin) avec un outil compatible ESP32 à l'adresse **`0x0`**. Il comprend le bootloader, la table de partitions et l'application.

La [première version, sans écoute CAN](dist/flash/california-ocean-weact-v1.1-complet.bin), reste disponible pour revenir à l'état initial.

### 2. Préparer la microSD

Formater une microSD en **FAT32**, puis extraire [microsd-interface.zip](dist/flash/microsd-interface.zip) à sa racine. Le fichier doit se trouver à l'emplacement `/index.html`. Insérer la carte et redémarrer l'ESP32.

### 3. Ouvrir l'interface

Ouvrir la console série à **115200 bauds** après le démarrage. Elle affiche le nom du réseau Wi-Fi, son mot de passe et l'adresse de l'interface (habituellement `http://192.168.4.1/`). Le mot de passe change à chaque redémarrage et n'est pas stocké dans ce dépôt.

Sans véhicule raccordé, la recherche du débit tourne en boucle : c'est le comportement attendu. Une trame CAN valide verrouille le débit. Le menu **Réception CAN** permet alors de lancer une capture, de l'arrêter et de télécharger le CSV.

### Compiler depuis les sources

Le projet utilise [PlatformIO](https://platformio.org/) et le profil `esp32dev` :

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

`firmware.bin` produit dans `.pio/build/weact_can485/` est l'application seule, destinée à l'adresse `0x10000`. Les binaires proposés plus haut sont complets et se flashent à `0x0`.

## Raccordement au véhicule

Le point de prise envisagé **sous le siège** n'est pas encore identifié comme portant le CAN camping sur cet Ocean 2023. Le [schéma Volkswagen d'un Ocean 2022](https://www.tempsdepause.ch/solfar/wp-content/uploads/2023/03/Schemas-de-parcours-du-courant-6.1-oCalifornia-Ocean-2022.pdf) documente la liaison entre l'écran camping **E153** et le calculateur **J608**, mais ne valide pas un point de prise sous le siège pour notre véhicule. Le [programme d'autoformation VW 708](https://esperformance.net/ssp/vwn/SSP_708_EN.pdf) situe J608 sous le porte-fusibles du tableau de bord sur le California 6.1.

Avant de connecter la WeAct, identifier le faisceau sur le schéma correspondant au véhicule, confirmer CAN-H et CAN-L et placer le commutateur `120Ω` **du bas** sur `OFF` pour une dérivation sur le bus existant. Voir les [notes matérielles et les sources](docs/hardware.md). La recherche automatique du débit ne prouve pas, à elle seule, qu'on a trouvé le bon bus.

## Structure

```text
src/main.cpp          Serveur Wi-Fi, écoute TWAI et captures CSV
platformio.ini        Configuration PlatformIO
dist/index.html       Interface web autonome
dist/flash/           Binaires de flash et ZIP pour la microSD
docs/hardware.md      Brochage, sources et points à confirmer
```

## Suite du projet

1. Identifier sur le véhicule un point de prise de l'ABH-CAN camping et confirmer son brochage.
2. Capturer des séquences en manipulant les commandes camping d'origine, une fonction à la fois.
3. Déduire les états et vérifier les décodages sur plusieurs captures.
4. Étudier séparément les commandes, acquittements et conditions de sécurité. Le toit restera verrouillé tant que ces éléments ne sont pas démontrés.

Ce dépôt ne contient aucun identifiant CAN ou ordre de commande inventé pour le California 2023.
