def can_build(env, platform):
    import os

    return (
        env.get("module_sqlite_enabled", False)
        and env.get("module_http3_enabled", False)
        and os.path.isfile("scene/main/fabric_zone_peer_callbacks.h")
    )


def configure(env):
    pass


def get_doc_classes():
    return [
        "FabricMultiplayerPeer",
        "FabricSnapshot",
        "FabricZone",
    ]


def get_doc_path():
    return "doc_classes"
