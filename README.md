# Backend Serveur WebSocket

## Structure du serveur

- `main_server.cpp` : Lance le serveur WebSocket
- Utilise `SimulationServer` pour gérer la simulation
- Envoie les données au frontend React via WebSocket

## Configuration de compilation

### Compiler le serveur
```bash
# Ouvrir le projet Qt avec le fichier .pro
# Compiler le projet
```

## Architecture WebSocket

Le serveur écoute sur le port `8080` et communique en JSON:

### Message d'initialisation de la simulation (clients au serveur)
```json
{
  "command": "start"
}
```

### Exemple de mise à jour des positions (serveur aux clients)
```json
{
  "type": "update",
  "timestamp": 1634567890,
  "data": [
    {
      "id": 0,
      "x": 120.5,
      "y": 450.2,
      "angle": 45.0,
      "vitesse": 50
    },
    ...
  ]
}
```