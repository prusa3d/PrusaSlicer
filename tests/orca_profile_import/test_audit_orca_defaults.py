import unittest

from audit_orca_defaults import unmapped_source_fields


class SourceCoverageTests(unittest.TestCase):
    def test_explicit_only_zero_and_default_precedence(self):
        sources = {'process': {
            'prime_volume': {'value': '0', 'profile': 'saved process'},
            'source_only_switch': {'value': '0', 'profile': 'saved process'},
            'wall_loops': {'value': '3'},
            'brim_type': {'value': ['nil'], 'profile': 'parent process'},
        }}
        defaults = {'process': {'prime_volume': '45', 'brim_type': 'auto_brim', 'omitted': '7'}}
        rows = unmapped_source_fields(sources, defaults, {'perimeters'}, {'wall_loops': 'perimeters'})['process']
        self.assertNotIn('wall_loops', rows)
        self.assertEqual(rows['prime_volume']['value'], '0')
        self.assertEqual(rows['prime_volume']['engine_default'], '45')
        self.assertEqual(rows['prime_volume']['origin'], 'explicit_source')
        self.assertEqual(rows['source_only_switch']['value'], '0')
        self.assertFalse(rows['source_only_switch']['has_engine_default'])
        self.assertEqual(rows['brim_type']['value'], 'auto_brim')
        self.assertEqual(rows['brim_type']['nil_source']['profile'], 'parent process')
        self.assertEqual(rows['omitted']['origin'], 'orca_engine_default')


if __name__ == '__main__':
    unittest.main()
