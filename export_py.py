import argparse
import websocket
import struct
import numpy as np
import time
import os
from scipy.ndimage import label, generate_binary_structure

# --- Command-line arguments ---
parser = argparse.ArgumentParser(
    description="Record GY-MLX90640BAA thermal frames from an ESP8266 WebSocket server."
)
parser.add_argument(
    "--ip",
    default="10.28.26.7",
    help="IP address of the ESP8266 (default: 10.28.26.7)",
)
parser.add_argument(
    "--output",
    default="./dataset_thermique",
    help="Directory to save .npy frame files (default: ./dataset_thermique)",
)
args = parser.parse_args()

SAVE_DIR = args.output
os.makedirs(SAVE_DIR, exist_ok=True)

# Compteur pour la séquence des fichiers
frame_counter = 0

def on_message(ws, message):
    global frame_counter

    # On vérifie qu'on reçoit bien les 3072 octets (768 pixels * 4 octets)
    if len(message) == 3072:
        # Décodage du binaire brut (Little-Endian) vers un tuple de 768 floats
        data = struct.unpack('<768f', message)

        # Conversion en matrice 2D (24 lignes, 32 colonnes)
        matrix = np.array(data, dtype=np.float32).reshape((24, 32))

        # Extraction de la température minimum et maximum
        temp_min = np.min(matrix)
        temp_max = np.max(matrix)

        # Définition de la structure de connexion (permet de lier les pixels en diagonale)
        structure = generate_binary_structure(2, 2)

        # --- DÉTECTION DU NOMBRE DE PERSONNES (entre 25.0°C et 33.0°C) ---
        # On utilise l'opérateur bitwise '&' pour combiner les deux conditions NumPy
        masque_personne = (matrix >= 25.0) & (matrix <= 33.0)
        _, nb_personnes = label(masque_personne, structure)

        # --- DÉTECTION DES POINTS CHAUDS (> 33.0°C) ---
        seuil_point_chaud = 33.0
        masque_point_chaud = matrix > seuil_point_chaud
        _, nb_points_chauds = label(masque_point_chaud, structure)
        # ----------------------------------------

        # Sauvegarde avec l'ajout du nombre de personnes et de points chauds dans le nom de fichier
        filename = f"{SAVE_DIR}/frame_{temp_min:.1f}_{temp_max:.1f}_{nb_personnes}_{nb_points_chauds}_{frame_counter}.npy"
        np.save(filename, matrix)

        print(f"Sauvegarde : {filename} | Personnes (25-33°C) : {nb_personnes} | Points chauds (>33°C) : {nb_points_chauds}")

        frame_counter += 1
    else:
        print(f"Trame ignorée : taille incorrecte ({len(message)} octets)")

def on_error(ws, error):
    error_msg = str(error).lower()
    if "rsv is not implemented" in error_msg:
        print("⚠️ Perte de synchronisation réseau. Reconnexion en cours...")
    else:
        print(f"Erreur WebSocket : {error}")

def on_close(ws, close_status_code, close_msg):
    print("Connexion fermée. Reconnexion dans 5 secondes...")
    time.sleep(5)
    start_capture()

def start_capture():
    ws_url = f"ws://{args.ip}:81/"
    print(f"Connexion à {ws_url}...")

    ws = websocket.WebSocketApp(ws_url,
                              on_message=on_message,
                              on_error=on_error,
                              on_close=on_close)

    # Conservation des paramètres optimisés pour le flux binaire
    ws.run_forever(skip_utf8_validation=True, ping_interval=10, ping_timeout=5)

if __name__ == "__main__":
    print("--- Démarrage de la capture du dataset thermique ---")
    print(f"Les matrices seront sauvegardées dans : {os.path.abspath(SAVE_DIR)}")
    start_capture()
