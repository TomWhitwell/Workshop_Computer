#!/usr/bin/env python3
import sys,pathlib,tempfile,unittest,json,wave,base64,io
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import serve
class Web(unittest.TestCase):
 def test_check_and_errors(self):
  _,stats=serve.check_source('ldax ADCL\nwrax DACL,0\n');self.assertEqual(stats['instructions'],2)
  with self.assertRaises(ValueError):serve.check_source('not_an_instruction\n')
  with self.assertRaises(ValueError):serve.prepare([],ROOT/'build/web/test')
 def test_guides_and_modified_source(self):
  import catalogue
  e=next(e for e in catalogue.entries() if e['id']=='directory-publisher-rom-rev1')
  p={'id':e['id'],'name':'../Edited reverb','source':(ROOT/e['path']).read_text()+';edit\n'}
  with tempfile.TemporaryDirectory(dir=ROOT/'build') as d:
   paths,guides=serve.prepare([p],pathlib.Path(d));self.assertTrue(paths[0].is_relative_to(d));self.assertTrue(guides[0]['modified']);self.assertIn('Keith Barr',guides[0]['author'])
 def test_real_native_preview(self):
  result=serve.preview({'source':'ldax ADCL\nwrax DACL,0\nldax ADCR\nwrax DACR,0\n','duration':1,'stimulus':'sine'})
  self.assertEqual(result['clipped_samples'],0)
  self.assertGreater(result['peak_dbfs'],-25)
  with wave.open(io.BytesIO(base64.b64decode(result['wav'])),'rb') as f:self.assertEqual(f.getframerate(),48000);self.assertEqual(f.getnframes(),48000)
if __name__=='__main__':unittest.main()
