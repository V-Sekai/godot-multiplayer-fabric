def can_build(env, platform):
    # The CineForm codec includes <emmintrin.h> unconditionally and uses __m128 in 16 files,
    # with no NEON path and no scalar fallback.
    return env["arch"] in ["x86_64", "x86_32"]


def configure(env):
    pass


def get_doc_classes():
    return ["MovieWriterCineForm"]


def get_doc_path():
    return "doc_classes"
