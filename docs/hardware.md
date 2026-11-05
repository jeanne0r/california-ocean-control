# Matériel et raccordement · notes de recherche

Ces notes séparent les éléments documentés de ce qui reste à vérifier sur le véhicule. Elles ne constituent pas un brochage validé pour l'Ocean 2023 concerné.

## WeAct CAN485 DevBoard V1.1

La [documentation officielle WeAct](https://github.com/WeActStudio/WeActStudio.CAN485DevBoardV1_ESP32) donne :

| Usage | Broches ESP32 |
| --- | --- |
| CAN | RX GPIO26, TX GPIO27 |
| microSD | CS GPIO13, SCK GPIO14, MOSI GPIO15, MISO GPIO2 |
| RS485 | DE GPIO17, RO GPIO21, DI GPIO22 |

Le [schéma WeAct V1.1](https://github.com/WeActStudio/WeActStudio.CAN485DevBoardV1_ESP32/blob/master/Hardware/WeAct-CAN485DevBoardV1_ESP32_V1.1%20SchDoc.pdf) donne pour le connecteur CAN **J2** : broche 1 = CAN-H, broche 2 = CAN-L, broche 3 = masse isolée. Vérifier la numérotation physique gravée sur la carte avant de placer les fils. Le bornier CAN est le bornier à trois positions **du bas** ; celui du haut est le RS485. Le commutateur `120Ω` du bas règle la terminaison CAN ; celui du haut et `PUPD` concernent le RS485. Le bornier à deux positions `VIN/GND` sert à l'alimentation externe. Le petit adaptateur à connecteur plat expose des GPIO supplémentaires et n'est pas nécessaire pour ce projet.

Pour une dérivation sur un bus du véhicule déjà terminé aux deux extrémités, laisser la terminaison CAN de la WeAct sur `OFF`. Pour les essais sur table, l'USB-C suffit à alimenter la carte.

## Réseau camping du California 6.1

Le [programme d'autoformation Volkswagen 708](https://esperformance.net/ssp/vwn/SSP_708_EN.pdf) décrit deux éléments distincts : **E153**, l'écran camping dans la console de toit, et **J608**, calculateur pour véhicules spéciaux situé sous le porte-fusibles SC. Leur liaison est l'**ABH-CAN**, distinct du CAN confort du véhicule. J608 supervise notamment réfrigérateur, éclairage, hydraulique du toit, pompe à eau et niveaux d'eau.

Un [schéma de parcours du courant Volkswagen pour un Ocean 2022](https://www.tempsdepause.ch/solfar/wp-content/uploads/2023/03/Schemas-de-parcours-du-courant-6.1-oCalifornia-Ocean-2022.pdf), page 8 du circuit 75, indique :

| Élément | CAN-H | CAN-L |
| --- | --- | --- |
| E153, connecteur noir T6l | broche 1 | broche 2 |
| J608, connecteur noir T20d | broche 11 | broche 12 |

Ce schéma montre également un point de couplage **TSKVR sous le siège droit**, mais ne repère pas la paire CAN E153↔J608 à cet endroit. Il ne permet donc pas d'affirmer qu'une paire visible sous le siège donne accès au bus camping. Le millésime et l'équipement exacts peuvent modifier le faisceau : obtenir le schéma correspondant au véhicule ou confirmer le câblage par une mesure adaptée avant tout raccordement.

Le [manuel Volkswagen du calculateur de fonctions spécifiques pour Crafter](https://www.volkswagen-nutzfahrzeuge.ch/idhub/content/dam/onehub_nfz/importers/ch/beratung-und-kauf/umbauloesungen_technische_informationen/technische-informationen_2018/Crafter_Funktionssteuergeraet-KW28-2017-EN.pdf) cite 125, 250 ou 500 kbit/s pour l'ABH-CAN selon l'application. Il ne fixe **pas** le débit de ce California ; le firmware essaie ces trois valeurs et attend une trame valide.

## Limites connues

- Le [DBC public du Grand California](https://github.com/mk4001/VWGC-CanBus) concerne un autre véhicule. Ses identifiants ne sont pas repris ici.
- Aucune table publique vérifiée n'a été trouvée pour les commandes CAN du California 6.1 Ocean 2023, leurs acquittements et leurs conditions de sécurité.
- L'écoute TWAI est configurée en [mode `LISTEN_ONLY`](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32/api-reference/peripherals/twai.html), mais le comportement électrique du montage et la qualité des captures doivent encore être contrôlés sur le véhicule.
