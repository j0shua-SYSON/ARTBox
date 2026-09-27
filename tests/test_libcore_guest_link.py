import sys
sys.dont_write_bytecode = True
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from libcore_guest_link import check_dependencies


class LibcoreDependencies(unittest.TestCase):
    def setUp(self):
        self.images = {
            'libc.so': {'needed': [], 'exports': ['malloc'], 'imports': {}},
            'libopenjdkjvm.so': {'needed': ['libc.so'], 'exports': ['JVM_Open'],
                                'imports': {'malloc': 'U', 'artbox_bionic_get_tls': 'U'}},
            'client': {'needed': ['libopenjdkjvm.so'], 'exports': [], 'imports': {'JVM_Open': 'U'}}}

    def test_only_existing_jvm_tls_boundary_is_allowed(self):
        self.assertEqual(check_dependencies(self.images, ['libopenjdkjvm.so', 'client'])['client'],
                         ['client', 'libopenjdkjvm.so', 'libc.so'])

    def test_other_images_cannot_import_host_tls(self):
        self.images['client']['imports']['artbox_bionic_get_tls'] = 'U'
        with self.assertRaisesRegex(ValueError, 'artbox_bionic_get_tls'):
            check_dependencies(self.images, ['libopenjdkjvm.so', 'client'])

    def test_new_jvm_host_import_is_rejected(self):
        self.images['libopenjdkjvm.so']['imports']['host_fork'] = 'U'
        with self.assertRaisesRegex(ValueError, 'host_fork'):
            check_dependencies(self.images, ['libopenjdkjvm.so'])

    def test_jvm_tls_boundary_cannot_silently_disappear(self):
        del self.images['libopenjdkjvm.so']['imports']['artbox_bionic_get_tls']
        with self.assertRaisesRegex(ValueError, 'TLS'):
            check_dependencies(self.images, ['libopenjdkjvm.so'])

    def test_unreachable_export_cannot_satisfy_dependency(self):
        self.images['client']['needed'] = ['libc.so']
        with self.assertRaisesRegex(ValueError, 'JVM_Open'):
            check_dependencies(self.images, ['client'])

    def add_jni_images(self):
        self.images['libjavacore.so'] = {
            'needed': ['libc.so'], 'exports': ['JNI_OnLoad', 'JNI_OnUnload'], 'imports': {'malloc': 'U'}}
        self.images['libicu_jni.so'] = {
            'needed': ['libc.so'], 'exports': ['JNI_OnLoad', '_ZN12JniConstants10InitializeEP7_JNIEnv'], 'imports': {}}

    def test_javacore_cache_is_not_exposed_by_unrelated_preloaded_jni_image(self):
        self.add_jni_images()
        self.assertEqual(check_dependencies(self.images, ['libjavacore.so'])['libjavacore.so'],
                         ['libjavacore.so', 'libc.so'])

    def test_javacore_cache_cannot_be_exported_directly(self):
        self.add_jni_images()
        self.images['libjavacore.so']['exports'].append('_ZN12JniConstants10InitializeEP7_JNIEnv')
        with self.assertRaisesRegex(ValueError, 'JniConstants'):
            check_dependencies(self.images, ['libjavacore.so'])

    def test_javacore_cannot_acquire_another_jni_cache_through_needed(self):
        self.add_jni_images()
        self.images['libjavacore.so']['needed'].append('libicu_jni.so')
        with self.assertRaisesRegex(ValueError, 'JniConstants'):
            check_dependencies(self.images, ['libjavacore.so'])

    def test_transitive_jni_cache_leak_is_rejected(self):
        self.add_jni_images()
        self.images['libc.so']['needed'].append('libicu_jni.so')
        with self.assertRaisesRegex(ValueError, 'JniConstants'):
            check_dependencies(self.images, ['libjavacore.so'])


if __name__ == '__main__': unittest.main()
