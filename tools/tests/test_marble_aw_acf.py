import importlib.util,struct,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('aw_acf',ROOT/'tools/marble-aw-acf.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture():
 data=bytearray(244)
 struct.pack_into('<I24s16s4s8I',data,0,0xa15f908,b'M16T',b'xiaomi',b'\0\0\0\4',656,96,1,80,0x1000000,0,0,0)
 payload=bytearray(68)
 struct.pack_into('<6I8s3I',payload,0,0,0x1000000,0,1,8,0,b'AW88263S',2,2,0)
 struct.pack_into('<IHH',payload,60,1,4,0x1234);struct.pack_into('<I',payload,0,sum(payload[4:]))
 struct.pack_into('<I16s4H4I32sI4I',data,80,2,b'aw88263S',0,65535,65535,0,4,68,176,m.crc8(payload),b'Music',0x2032,0,0,0,0)
 data[176:]=payload;return data
class Acf(unittest.TestCase):
 def test_valid_nested_register_profile(self):
  r=m.inspect(fixture());self.assertTrue(r['valid']);self.assertFalse(r['audio_acceptance']);self.assertEqual(r['entries'][0]['decoded']['registers'],[dict(address=4,value=0x1234)])
 def test_all_truncations_and_trailing_bytes(self):
  data=fixture()
  for n in range(len(data)):
   with self.assertRaises(ValueError,msg=str(n)):m.inspect(data[:n])
  with self.assertRaises(ValueError):m.inspect(data+b'\0')
 def test_top_level_bounds_identity(self):
  for at,value in ((0,0),(52,0xffffffff),(56,0xffffffff),(60,0xfffffff0),(64,1),(80,1),(108,99),(112,0xffffffff),(116,0xffffffff),(120,256),(156,0x2113)):
   data=fixture();struct.pack_into('<I',data,at,value)
   with self.assertRaises(ValueError,msg=str(at)):m.inspect(data)
 def test_payload_crc_and_repaired_crc_still_rejects_inner_corruption(self):
  for at in (0,4,8,12,16,32,36,60,64):
   data=fixture();data[176+at]^=1
   with self.assertRaises(ValueError):m.inspect(data)
   struct.pack_into('<I',data,120,m.crc8(data[176:]))
   with self.assertRaises(ValueError,msg=str(at)):m.inspect(data)
 def test_duplicate_overlap_not_accepted(self):
  data=fixture();payload=data[176:];row=bytearray(data[80:176]);struct.pack_into('<I',row,36,272)
  struct.pack_into('<I',data,52,192);struct.pack_into('<I',data,56,2);struct.pack_into('<I',data,116,272)
  data=data[:176]+row+payload
  with self.assertRaises(ValueError):m.inspect(data)
 def test_real_rom_and_android_match(self):
  path=ROOT/'out/audio-route-v103-plan/rom-aw882xx-acf.bin'
  if not path.is_file():self.skipTest('local ROM material unavailable')
  r=m.inspect(path.read_bytes());self.assertEqual(len(r['entries']),8)
  self.assertEqual({x['index'] for x in r['entries']},{0,1})
  self.assertEqual({x['chip_id'] for x in r['entries']},{0x2032})
  self.assertEqual(path.read_bytes(),path.with_name('android-aw882xx-acf.bin').read_bytes())
if __name__=='__main__':unittest.main()
