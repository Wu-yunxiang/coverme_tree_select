"""Unit tests for path_helper module."""

import os
import sys
import unittest

# Add src to path
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src"))
import path_helper


class TestPathHelper(unittest.TestCase):
    def test_get_root_dir(self):
        root = path_helper.get_root_dir()
        self.assertTrue(os.path.isdir(root))
        self.assertTrue(os.path.isfile(os.path.join(root, "CMakeLists.txt")))

    def test_get_output_dir(self):
        output = path_helper.get_output_dir()
        self.assertTrue(os.path.isdir(output))
        self.assertTrue(output.endswith("output"))

    def test_get_lib_dir(self):
        lib_dir = path_helper.get_lib_dir()
        self.assertTrue(lib_dir.endswith(os.path.join("build", "lib")))


if __name__ == "__main__":
    unittest.main()
