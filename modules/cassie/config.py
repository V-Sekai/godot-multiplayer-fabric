def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "CassieBeautifier",
        "CassieBeautifierParams",
        "CassieConstraint",
        "CassieConstraintSolver",
        "CassieCurvenet",
        "CassieCurvenetExtractor",
        "CassieCurvenetKnot",
        "CassieEdgeCollider",
        "CassieFinalStroke",
        "CassieInputStroke",
        "CassieIntersectionConstraint",
        "CassieMirrorPlaneConstraint",
        "CassiePath3D",
        "CassieProfileMover",
        "CassieSketchContext",
        "CassieSketchGraph",
        "CassieSketchGraphEdge",
        "CassieSketchGraphNode",
        "CassieSketcher",
        "CassieSolverParams",
        "CassieStrokePacket",
        "CassieSurface",
        "CassieSurfaceConstraint",
        "CassieSurfaceManager",
        "CassieSurfacePatch",
        "CassieTriangulator",
        "IntrinsicTriangulation",
        "PolygonTriangulation",
        "PolygonTriangulationGodot",
    ]


def get_doc_path():
    return "doc_classes"
