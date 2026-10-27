#!/usr/bin/env python3
import contextlib,hashlib,importlib.util,io,json,pathlib,sys,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import catalogue,index_catalogue,compile_bank
class Catalogue(unittest.TestCase):
    def test_integrity_and_credits(self):
        entries=catalogue.entries();self.assertGreaterEqual(len(entries),200);self.assertEqual(len(entries),len({e['id'] for e in entries}))
        for e in entries:
            self.assertTrue(e['author']);self.assertTrue(e['description']);self.assertEqual(len(e['controls']),3)
            self.assertEqual(hashlib.sha256((ROOT/e['original_path']).read_bytes()).hexdigest(),e['original_sha256'])
            self.assertEqual(hashlib.sha256((ROOT/e['path']).read_bytes()).hexdigest(),e['sha256'])
    def test_every_page_and_alias(self):
        data=catalogue.load();covered=[]
        for name,b in data['banks'].items():
            self.assertGreater(len(b['programs']),0);self.assertLessEqual(len(b['programs']),6)
            for identifier in b['programs']:self.assertTrue(catalogue.resolve(identifier).is_file())
            if name.startswith('page-'):covered.extend(b['programs'])
        expected={e['id'] for e in data['programs'] if e['assembly']['status']=='assembled' and not e.get('duplicate_of')}
        self.assertEqual(set(covered),expected);self.assertEqual(len(covered),len(expected))
    def test_cosine_and_decimal_normalization(self):
        source='CHO RDAL,COS0\nCHO RDAL,9\nSOF -.5,.25\nWRAX DACL,0.\n'
        normalized,changes=index_catalogue.normalize(source)
        self.assertEqual(normalized,'CHO RDAL,SIN0,COS\nCHO RDAL,SIN1,COS\nSOF -0.5,0.25\nWRAX DACL,0.0\n')
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'test.spn';p.write_text(normalized);words=compile_bank.assemble(p)
        self.assertEqual(words[:2],[0xc1000014,0xc1200014])
    def test_unsupported_not_silently_clamped(self):
        with self.assertRaises(ValueError):catalogue.resolve('otherworld-01-prism-scatter')
        self.assertIn('2.0',index_catalogue.normalize('SOF 2.0,0\n')[0])
        with self.assertRaises(ValueError):catalogue.select(['directory-dattorro'],'classic-reverbs')
    def test_manifest_credits(self):
        with tempfile.TemporaryDirectory() as d:
            rows=compile_bank.generate([catalogue.resolve('directory-publisher-rom-rev1')],pathlib.Path(d))
            self.assertIn('Keith Barr',rows[0]['author']);self.assertEqual(rows[0]['controls'][0],'reverb time')
            self.assertEqual(rows[0]['id'],'directory-publisher-rom-rev1')
if __name__=='__main__':unittest.main()
