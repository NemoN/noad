import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('check_marks', ROOT / 'tools/check_marks.py')
marks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(marks)


class FrameReferenceTests(unittest.TestCase):
    def test_hd_frame_component_is_not_centiseconds(self):
        self.assertEqual(marks.frames('0:00:00.00\n0:01:00.49\n', 50), [0, 3049])

    def test_comments_do_not_hide_shifted_endpoints(self):
        expected = '0:00:00.03 Logo start\n0:34:02.13 Logo start\n'
        actual = ('0:00:00.03 Logo start\n'
                  '0:33:27.18 moved from[0:34:02.13(51063) Logo start] by checkBlackFrameOnMark\n')
        result = marks.compare(expected, actual, 25)
        self.assertFalse(result['matches'])
        self.assertEqual(result['delta_frames'], [0, -870])

    def test_missing_boundary_is_a_failure_even_if_other_marks_match(self):
        expected = '0:00:00.00\n0:01:00.00\n0:02:00.00\n0:03:00.00\n'
        self.assertFalse(marks.compare(expected, '0:00:00.00\n0:01:00.00\n', 25)['matches'])

    def test_invalid_mark_files_are_rejected(self):
        for text in ['0:00:00.25\n0:01:00.00', '0:00:00.00',
                     '0:01:00.00\n0:00:00.00', '0:60:00.00\n1:01:00.00',
                     'garbage', '0:00:00.00\n0:00:00.00']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                marks.frames(text, 25)


if __name__ == '__main__':
    unittest.main()
