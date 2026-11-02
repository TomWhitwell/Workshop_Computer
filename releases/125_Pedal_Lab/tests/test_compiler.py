#!/usr/bin/env python3
"""Check parser failures, official example encodings, and bank compilation."""
import importlib.util, pathlib, struct, tempfile, unittest, contextlib, io
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bank',ROOT/'tools/compile_bank.py')
bank=importlib.util.module_from_spec(spec);spec.loader.exec_module(bank)
class Compiler(unittest.TestCase):
    def source(self,text):
        p=pathlib.Path(self.directory.name)/'test.spn';p.write_text(text);return p
    def setUp(self):self.directory=tempfile.TemporaryDirectory()
    def tearDown(self):self.directory.cleanup()
    def test_known_encoding(self):
        # Independent worked example in asfv1's published README, with SpinASM reals.
        w=bank.assemble(self.source('mem delay 19660\nequ vol reg0\nskp RUN,main\nldax POT0\nwrax vol,0.0\nmain: ldax ADCL\nmulx vol\nwra delay,0.0\nrda delay^,0.5\nrda delay#,0.5\nwrax DACL,0.0\n'))
        self.assertEqual(w[:9],[0x80400011,0x205,0x406,0x285,0x40a,0x2,0x2004ccc0,0x20099980,0x2c6])
    def test_invalid_fails(self):
        for text in ('wat ADCL,1\n','mem too_long 32768\n','skp RUN,missing\n','ldax ADCL\n'*129,'raw $ffffffff\n'):
            with contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises((SystemExit,ValueError)):bank.assemble(self.source(text))
    def test_skip_label_aot(self):
        p=self.source('skp RUN,done\nsof 0,0.5\ndone: wrax DACL,0\n')
        out=pathlib.Path(self.directory.name)/'out';bank.generate([p],out)
        cpp=(out/'bank.hpp').read_text();self.assertIn('goto L2',cpp);self.assertIn('L2:',cpp)
    def test_no_eval(self):
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises((SystemExit,ValueError)):bank.assemble(self.source("sof __import__('os'),0\n"))
    def test_all_bundled_sources(self):
        for path in (ROOT/'programs').glob('*.spn'):
            self.assertEqual(len(bank.assemble(path)),128)
    def test_no_silent_clamping(self):
        for text in ('sof 3.0,0\n','mem all 32768\n'):
            with contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises((SystemExit,ValueError)):bank.assemble(self.source(text))
if __name__=='__main__':unittest.main()
