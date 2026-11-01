import json
import tempfile
import unittest
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).parents[1]))
from marble_android import verify_battery_preflight


class BatteryPreflight(unittest.TestCase):
    def test_missing_stale_wrong_device_and_hot_readings(self):
        normal=dict(confirmed=True,serial='YOUR_ANDROID_SERIAL',at_unix=1000,temperature_decic=320)
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'battery.json'
            p.write_text(json.dumps(normal));self.assertEqual(verify_battery_preflight(p,1010)['temperature_decic'],320)
            for values in [dict(confirmed=False),dict(serial='other'),dict(at_unix=1200),dict(at_unix=0),
                    dict(temperature_decic=380),dict(temperature_decic=True),dict(temperature_decic=-2730)]:
                p.write_text(json.dumps(dict(normal,**values)))
                with self.assertRaises(ValueError):verify_battery_preflight(p,1010)


if __name__=='__main__':unittest.main()
