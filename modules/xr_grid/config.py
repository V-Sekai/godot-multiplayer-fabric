def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        # Stroke transport
        "XRGridStrokeChannel",
        # Utilities
        "XRGridBoolTimer",
        "XRGridSwingTwistCodec",
        "XRGridEntityPacket",
        "XRGridVskVersion",
        # Fabric networking
        "XRGridFabricManager",
        "XRGridFabricTransformSync",
        "XRGridOrientationOrb",
        "XRGridRemotePlayer",
        "XRGridRemotePlayerManager",
        "XRGridZoneServer",
        "XRGridZoneSceneTree",
        # VR input
        "XRGridWorldGrab",
        "XRGridXROrigin",
        "XRGridHand",
        "XRGridXRPinch",
        "XRGridFlatscreenController",
        # Rendering
        "XRGridSimpleSketch",
        "XRGridSketchTool",
        "XRGridStrokes",
        "XRGridFollowTest",
        "XRGridBaseProceduralGrid3D",
        "XRGridProceduralGrid3D",
        "XRGridGPUTrail3D",
        # Avatar
        "XRGridCapsulePersona",
    ]


def get_doc_path():
    return "doc_classes"
