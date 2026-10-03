"""Allow only OpenjdkJvm's existing source-level Bionic TLS binding."""
from icu_guest_link import check_code, check_dependencies as closed_dependencies


def check_dependencies(images, checked):
    reviewed = {name: {**item, 'imports': dict(item['imports'])} for name, item in images.items()}
    if 'libopenjdkjvm.so' in checked:
        imports = reviewed['libopenjdkjvm.so']['imports']
        if imports.pop('artbox_bionic_get_tls', None) != 'U':
            raise ValueError('OpenjdkJvm must retain its explicit Bionic TLS boundary')
    scopes = closed_dependencies(reviewed, checked)
    # javacore's version map hides its JNI class cache. An unnecessary
    # DT_NEEDED edge to another JNI library would still expose that library's
    # identically named cache through handle-local dlsym.
    for name in scopes.get('libjavacore.so', []):
        if any(symbol.startswith('_ZN12JniConstants') for symbol in images[name]['exports']):
            raise ValueError('JniConstants is visible in javacore lookup scope through ' + name)
    return scopes
