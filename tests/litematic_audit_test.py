"""Packed-bit boundary and compatibility validation, independent of fixture assets."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('audit', Path(__file__).resolve().parents[1] / 'tools/audit-litematic.py')
audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)


class LitematicAuditTests(unittest.TestCase):
    def test_signed_words_and_cross_word_indices(self):
        values = [i % 31 for i in range(127)]
        # Independent bit-string encoding puts several indices across 64-bit boundaries.
        bit_string = ''.join(f'{v:05b}'[::-1] for v in values).ljust(640, '0')
        words = []
        for i in range(0, len(bit_string), 64):
            word = int(bit_string[i:i+64][::-1], 2)
            words.append(word if word < 1 << 63 else word - (1 << 64))
        self.assertEqual(list(audit.indices(words, len(values), 31)), values)

    def test_bad_arrays_and_palette_indices_are_rejected(self):
        for words, count, palette in [([], 1, 2), ([3], 1, 3), ([0], 1, 0), ([0, 0], 1, 2)]:
            with self.assertRaises(ValueError): list(audit.indices(words, count, palette))

    def test_unknown_block_or_state_is_not_silently_converted(self):
        registry = {'minecraft:lever': {'properties': {'powered': ['true', 'false']}}}
        self.assertIsNone(audit.state_error({'Name': 'minecraft:lever', 'Properties': {'powered': 'true'}}, registry))
        self.assertIn('unknown block', audit.state_error({'Name': 'minecraft:potent_sulfur'}, registry))
        self.assertIn('unknown property', audit.state_error({'Name': 'minecraft:lever', 'Properties': {'powered': 'maybe'}}, registry))


if __name__ == '__main__': unittest.main(verbosity=2)
