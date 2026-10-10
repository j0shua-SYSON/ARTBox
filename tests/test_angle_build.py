"""Reject unreviewed graphics backends and falsely successful rendering evidence."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import copy
import json
import os
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from build_angle import CATALOG, PROFILE, generated_headers, validate_probe, validate_profile, validate_window_probe
from environment import environment


class ANGLEBoundary(unittest.TestCase):
    def setUp(self):
        self.catalog = json.loads(CATALOG.read_bytes())
        self.profile = json.loads(PROFILE.read_bytes())

    def test_pinned_scope(self):
        validate_profile(self.catalog, self.profile)

    def test_header_named_upstream_group_can_contain_implementation(self):
        # Upstream deliberately puts this .cpp in libangle_headers. GN compiles
        # it; omitting it leaves every affected GLES validation entry unresolved.
        name = 'src/libANGLE/entry_points_utils.cpp'
        self.assertIn(name, self.profile['original_groups']['libangle_headers'])
        self.assertIn(name, self.profile['common'])
        broken = copy.deepcopy(self.profile); broken['common'].remove(name)
        with self.assertRaises(RuntimeError): validate_profile(self.catalog, broken)

    def test_unreviewed_or_duplicate_unit(self):
        for name in ('src/unknown.cpp', self.profile['common'][0], 'src/libANGLE/renderer/vulkan/DisplayVk.cpp'):
            profile = copy.deepcopy(self.profile)
            profile['common'].append(name)
            with self.assertRaises(RuntimeError): validate_profile(self.catalog, profile)

    def test_forbidden_backend_and_private_ownership(self):
        for define in ('ANGLE_ENABLE_VULKAN', 'ANGLE_ENABLE_WGPU', 'ANGLE_ENABLE_METAL_OWNERSHIP_IDENTITY'):
            profile = copy.deepcopy(self.profile); profile['defines'].append(define)
            with self.assertRaises(RuntimeError): validate_profile(self.catalog, profile)

    def test_cache_identity_changes_with_recipe_and_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            first = generated_headers(path, self.catalog, self.profile)
            self.profile['defines'].append('ANGLE_TEST_RECIPE_ID=1')
            second = generated_headers(path, self.catalog, self.profile)
            self.assertNotEqual(first['ANGLEShaderProgramVersion.h'], second['ANGLEShaderProgramVersion.h'])
            self.catalog['angle']['files'][0]['sha256'] = 'f' * 64
            third = generated_headers(path, self.catalog, self.profile)
            self.assertNotEqual(second['ANGLEShaderProgramVersion.h'], third['ANGLEShaderProgramVersion.h'])

    def test_no_gpu_does_not_claim_pixels(self):
        record = dict(schema=1, translated_shaders=2, invalid_shader_rejected=True,
                      metal_available=False, verified_pixels=0)
        validate_probe(record)
        record['verified_pixels'] = 2048
        with self.assertRaises(RuntimeError): validate_probe(record)

    def test_gpu_requires_pixels_backend_and_cleanup(self):
        record = dict(schema=1, translated_shaders=2, invalid_shader_rejected=True,
                      metal_available=True, backend='metal', verified_pixels=2048,
                      surfaces_destroyed=1, contexts_destroyed=1)
        validate_probe(record)
        for key, value in [('translated_shaders', 1), ('invalid_shader_rejected', False),
                           ('backend', 'null'), ('verified_pixels', 1024), ('surfaces_destroyed', 0),
                           ('contexts_destroyed', 0), ('metal_available', 1)]:
            broken = dict(record); broken[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError): validate_probe(broken)

    def test_window_requires_render_lifetime_and_thread_evidence(self):
        record = dict(schema=1, backend='metal', metal_available=True, verified_pixels=7296,
                      verified_texture_bytes=4, frames_presented=7, surfaces_created=3,
                      surfaces_destroyed=3, thread_rejections=6, suspend_resume_passed=True,
                      shared_display_passed=True)
        validate_window_probe(record)
        for key in record:
            broken = dict(record)
            broken.pop(key)
            with self.subTest(missing=key), self.assertRaises(RuntimeError): validate_window_probe(broken)
        for key, value in [('backend', 'null'), ('metal_available', False), ('verified_pixels', 2048),
                           ('verified_texture_bytes', 0), ('surfaces_created', 1), ('surfaces_destroyed', 2),
                           ('frames_presented', 6), ('thread_rejections', 5), ('suspend_resume_passed', False),
                           ('shared_display_passed', False), ('schema', True)]:
            broken = dict(record); broken[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError): validate_window_probe(broken)


if __name__ == '__main__':
    os.environ.update(environment())
    unittest.main()
