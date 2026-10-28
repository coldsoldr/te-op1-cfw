"""Where things are: the repository, the tools, the shipped image, the
manifests, the user's preset sets and the app's own settings file."""

import json
import os
import sys

APP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(APP_DIR)
TOOLS = os.path.join(REPO, 'tools')
REPACKER = os.path.join(REPO, 'op1repacker-master')
MANIFESTS = os.path.join(APP_DIR, 'manifests')
IMAGE_DIR = os.path.join(APP_DIR, 'image')
UI_DIR = os.path.join(APP_DIR, 'ui')
PRESET_SETS = os.path.join(APP_DIR, 'presets')
DATA_DIR = os.path.join(APP_DIR, 'data')          # settings, undo log, backups index (git-ignored)
SETTINGS_FILE = os.path.join(DATA_DIR, 'settings.json')
FIRMWARE_DIR = os.path.join(REPO, 'op1 firmware')  # the user's own firmware files (never committed)
STOCK_DIR = os.path.join(FIRMWARE_DIR, 'op1_246')

for p in (TOOLS, REPACKER):
    if p not in sys.path:
        sys.path.insert(0, p)

DEFAULT_SETTINGS = {
    'stock_package': os.path.join(FIRMWARE_DIR, 'op1_246.op1'),
    'output_dir': FIRMWARE_DIR,
    'device_root': '',
    'backup_dir': os.path.join(DATA_DIR, 'backups'),
    'window': 'browser',
}


def load_settings():
    settings = dict(DEFAULT_SETTINGS)
    try:
        with open(SETTINGS_FILE, encoding='utf-8') as f:
            settings.update(json.load(f))
    except (OSError, ValueError):
        pass
    return settings


def save_settings(settings):
    os.makedirs(DATA_DIR, exist_ok=True)
    with open(SETTINGS_FILE, 'w', encoding='utf-8') as f:
        json.dump(settings, f, indent=2)
        f.write('\n')
