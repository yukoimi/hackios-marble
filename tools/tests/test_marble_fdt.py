import importlib.util,struct,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('bounded_fdt',ROOT/'tools/marble-fdt.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture(duplicate=False):
 prop=struct.pack('>4I',3,4,0,0x1020304)
 body=struct.pack('>2I',1,0)+prop+(prop if duplicate else b'')+struct.pack('>2I',2,9)
 header=struct.pack('>10I',0xd00dfeed,56+len(body)+5,56,56+len(body),40,17,16,0,5,len(body))
 return bytearray(header+bytes(16)+body+b'name\0')
class Fdt(unittest.TestCase):
 def test_exact_property_data(self):self.assertEqual(m.parse(fixture()),{'/':{'name':b'\1\2\3\4'}})
 def test_all_truncations_and_trailing_data(self):
  data=fixture()
  for n in range(len(data)):
   with self.assertRaises(ValueError,msg=str(n)):m.parse(data[:n])
  with self.assertRaises(ValueError):m.parse(data+b'\0')
 def test_bounds_overlap_versions_tokens_and_lengths(self):
  for at,value in ((0,0),(4,0xffffffff),(8,55),(12,0xfffffff0),(16,56),(20,16),(24,18),(32,0xffffffff),(36,0xffffffff),(56,2),(64,99),(68,0xffffffff),(72,5),(80,1),(84,0)):
   data=fixture();struct.pack_into('>I',data,at,value)
   with self.assertRaises(ValueError,msg=str(at)):m.parse(data)
 def test_duplicate_property_terminator_and_limits(self):
  with self.assertRaises(ValueError):m.parse(fixture(True))
  data=fixture();data[-1]=65
  with self.assertRaises(ValueError):m.parse(data)
  with self.assertRaises(ValueError):m.parse(fixture(),maximum=64)
 def test_raw_header_change_does_not_invent_property_difference(self):
  data=fixture();other=bytearray(data);struct.pack_into('>I',other,28,1)
  self.assertNotEqual(data,other);self.assertEqual(m.parse(data),m.parse(other))
 def test_pinned_rom(self):
  path=ROOT/'out/i2c-controller-v105-audit/uefi-config.dtb'
  if not path.is_file():self.skipTest('ROM reference unavailable')
  tree=m.parse(path.read_bytes());self.assertEqual(len(tree),282)
  self.assertEqual(sorted(k for k in tree if k.startswith('/soc/i2c/i2c_device_config_')),['/soc/i2c/i2c_device_config_2','/soc/i2c/i2c_device_config_6'])
 def test_explicit_android_limits_do_not_relax_firmware_defaults(self):
  body=struct.pack('>2I',1,0)
  for i in range(4096):
   name=f'n{i}'.encode()+b'\0';name+=bytes(-len(name)%4);body+=struct.pack('>I',1)+name+struct.pack('>I',2)
  body+=struct.pack('>2I',2,9);data=struct.pack('>10I',0xd00dfeed,56+len(body)+5,56,56+len(body),40,17,16,0,5,len(body))+bytes(16)+body+b'name\0'
  with self.assertRaises(ValueError):m.parse(data,maximum=len(data))
  self.assertEqual(len(m.parse(data,maximum=len(data),maximum_nodes=4097)),4097)
  with self.assertRaises(ValueError):m.parse(data,maximum=len(data),maximum_nodes=4096)
  for kw in (dict(maximum_nodes=0),dict(maximum_nodes=16385),dict(maximum_properties=0),dict(maximum_properties=131073)):
   with self.assertRaises(ValueError):m.parse(fixture(),**kw)
  self.assertEqual(m.parse(fixture(),maximum_properties=1),{'/':{'name':b'\1\2\3\4'}})
if __name__=='__main__':unittest.main()
