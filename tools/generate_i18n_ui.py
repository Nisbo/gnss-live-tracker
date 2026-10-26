"""Generate the built-in static UI dictionary without downloads."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root / 'main/web/locales/ui.json'
output = root / 'main/web/i18n-ui.js'
catalogue = json.loads(source.read_text())
if set(catalogue) != {'de', 'en'} or set(catalogue['de']) != set(catalogue['en']):
    raise ValueError('German and English keys must match')
if any(not isinstance(value, str) or not value for entries in catalogue.values() for value in entries.values()):
    raise ValueError('Translations must be nonempty strings')
output.write_text('/* Generated from locales/ui.json by tools/generate_i18n_ui.py. */\n'
                  + 'TrackerI18n.register("ui",'
                  + json.dumps(catalogue, ensure_ascii=False, separators=(',', ':'))
                  + ');\n')
