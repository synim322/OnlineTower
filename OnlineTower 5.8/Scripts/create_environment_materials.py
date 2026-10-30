"""Create the environment palette in Unreal Editor (no external textures).

Run after saving the project, outside PIE. Existing assets are never overwritten.
Colours are linear RGB; world-space procedural variation uses centimetres.
"""
import unreal

DEST = "/Game/Core/Materials"
LIB = unreal.MaterialEditingLibrary
ASSETS = unreal.EditorAssetLibrary


def linear(hex_color):
    """Convert the concept artist's sRGB swatches to material linear RGB."""
    values = [int(hex_color[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
                 for v in values)


# name, linear RGB, roughness, metallic, noise scale, noise floor, emission
PALETTE = [
    ("M_OfficeWall", linear("E4E2D9"), 0.65, 0.0, 0.0, 1.0, 0.0),
    ("M_OfficeFloor", linear("BFC4C4"), 0.50, 0.0, 0.32, 0.85, 0.0),
    ("M_OfficeMetal", linear("28333C"), 0.55, 0.72, 0.0, 1.0, 0.0),
    ("M_OfficeWood", linear("9B6B45"), 0.65, 0.0, 0.035, 0.68, 0.0),
    ("M_OfficeGlass", (0.07, 0.19, 0.23), 0.12, 0.35, 0.0, 1.0, 0.0),
    ("M_OfficeLight", (1.0, 0.84, 0.61), 0.45, 0.0, 0.0, 1.0, 3.0),
    ("M_MountainRock", linear("65717D"), 0.90, 0.0, 0.007, 0.48, 0.0),
    ("M_ArenaStone", (0.38, 0.40, 0.37), 0.82, 0.0, 0.04, 0.73, 0.0),
    ("M_DungeonMetal", linear("344E61"), 0.55, 0.62, 0.018, 0.78, 0.0),
    ("M_DungeonGlow", linear("42D8D0"), 0.35, 0.15, 0.0, 1.0, 3.5),
    ("M_DesertSand", linear("CAA365"), 0.95, 0.0, 0.012, 0.82, 0.0),
    ("M_DesertRock", linear("745849"), 0.92, 0.0, 0.009, 0.58, 0.0),
    ("M_Shrub", linear("777A46"), 0.90, 0.0, 0.055, 0.52, 0.0),
]


def node(material, expression_type, x, y):
    result = LIB.create_material_expression(material, expression_type, x, y)
    if result is None:
        raise RuntimeError("Could not create expression " + str(expression_type))
    return result


def scalar(material, name, value, x, y):
    result = node(material, unreal.MaterialExpressionScalarParameter, x, y)
    result.set_editor_property("parameter_name", name)
    result.set_editor_property("default_value", value)
    return result


def connect(source, destination, input_name):
    if not LIB.connect_material_expressions(source, "", destination, input_name):
        raise RuntimeError("Material expression connection failed: " + input_name)


def output(source, prop):
    if not LIB.connect_material_property(source, "", prop):
        raise RuntimeError("Material property connection failed: " + str(prop))


def create(spec):
    name, color, roughness, metallic, scale, noise_floor, emission = spec
    path = DEST + "/" + name
    if ASSETS.does_asset_exist(path):
        existing = ASSETS.load_asset(path)
        if not isinstance(existing, unreal.MaterialInterface):
            raise RuntimeError("Expected a material at existing path: " + path)
        unreal.log("TOWER_ENV_MATERIAL_KEPT " + path)
        return False

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Material creation failed: " + path)
    # Opaque surfaces keep the procedural scene simple and predictable.
    material.set_editor_property("two_sided", False)
    tint = node(material, unreal.MaterialExpressionVectorParameter, -650, -150)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(*color, 1.0))
    surface_color = tint

    if scale > 0:
        noise = node(material, unreal.MaterialExpressionNoise, -850, 100)
        noise.set_editor_property("scale", scale)
        noise.set_editor_property("levels", 2)
        noise.set_editor_property("quality", 1)
        noise.set_editor_property("output_min", noise_floor)
        noise.set_editor_property("output_max", 1.0)
        # UE 5.8 Noise::Compile supplies world position when this input is empty.
        # Its visible pin name varies with WorldPositionOriginType; do not connect
        # the historical "Position" label, which is no longer a valid pin name.
        surface_color = node(material, unreal.MaterialExpressionMultiply, -350, -100)
        connect(tint, surface_color, "A")
        connect(noise, surface_color, "B")

    output(surface_color, unreal.MaterialProperty.MP_BASE_COLOR)
    output(scalar(material, "Roughness", roughness, -350, 180),
           unreal.MaterialProperty.MP_ROUGHNESS)
    output(scalar(material, "Metallic", metallic, -350, 300),
           unreal.MaterialProperty.MP_METALLIC)
    if emission > 0:
        strength = scalar(material, "Emission", emission, -650, -350)
        glow = node(material, unreal.MaterialExpressionMultiply, -350, -300)
        connect(tint, glow, "A")
        connect(strength, glow, "B")
        output(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    LIB.recompile_material(material)
    if not ASSETS.save_loaded_asset(material):
        raise RuntimeError("Material save failed: " + path)
    unreal.log("TOWER_ENV_MATERIAL_CREATED " + path)
    return True


def main():
    ASSETS.make_directory(DEST)
    created = sum(int(create(spec)) for spec in PALETTE)
    unreal.log("TOWER_ENV_MATERIALS_READY created=%d kept=%d" %
               (created, len(PALETTE) - created))


if __name__ == "__main__":
    main()
