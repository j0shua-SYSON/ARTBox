"""Allow only OpenjdkJvm's existing source-level Bionic TLS binding."""
from icu_guest_link import check_code, check_dependencies as closed_dependencies


def check_dependencies(images, checked):
    reviewed = {name: {**item, 'imports': dict(item['imports'])} for name, item in images.items()}
    if 'libopenjdkjvm.so' in checked:
        imports = reviewed['libopenjdkjvm.so']['imports']
        if imports.pop('artbox_bionic_get_tls', None) != 'U':
            raise ValueError('OpenjdkJvm must retain its explicit Bionic TLS boundary')
    return closed_dependencies(reviewed, checked)
