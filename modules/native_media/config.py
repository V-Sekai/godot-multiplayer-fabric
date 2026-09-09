def can_build(env, platform):
    # Built on every platform; the backend is selected at compile time and may
    # be a stub that returns ERR_UNAVAILABLE if the OS decoder is not yet
    # ported. The Vulkan video decode path is the primary; this module is the
    # cross-platform fallback.
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "AudioStreamNative",
        "ResourceImporterAudioNative",
        "ResourceImporterVideoNative",
        "VideoStreamNative",
        "VideoStreamPlaybackNative",
    ]


def get_doc_path():
    return "doc_classes"
