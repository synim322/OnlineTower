import unreal

DEST = "/Game/Core/Materials"
unreal.EditorAssetLibrary.make_directory(DEST)
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary

def create(name, color, emissive=False, stone=False):
    path = DEST + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log("Keeping existing material: " + path)
        return
    material = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("two_sided", True)
    tint = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -500, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(*color, 1.0))
    if emissive:
        strength = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -500, 150)
        strength.set_editor_property("r", 3.0)
        multiply = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 0)
        lib.connect_material_expressions(tint, "", multiply, "A")
        lib.connect_material_expressions(strength, "", multiply, "B")
        lib.connect_material_property(multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    elif stone:
        position = lib.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -900, 180)
        noise = lib.create_material_expression(material, unreal.MaterialExpressionNoise, -650, 200)
        noise.set_editor_property("scale", 0.025)
        noise.set_editor_property("output_min", 0.45)
        noise.set_editor_property("output_max", 1.0)
        lib.connect_material_expressions(position, "", noise, "Position")
        multiply = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 0)
        lib.connect_material_expressions(tint, "", multiply, "A")
        lib.connect_material_expressions(noise, "", multiply, "B")
        lib.connect_material_property(multiply, "", unreal.MaterialProperty.MP_BASE_COLOR)
    else:
        lib.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -250, 200)
    roughness.set_editor_property("r", 0.85 if stone else 0.5)
    lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log("TOWER_MATERIAL_CREATED " + path)

create("M_HubStone", (0.16, 0.135, 0.105), stone=True)
create("M_HubTrim", (0.27, 0.205, 0.105), stone=True)
create("M_Character", (0.18, 0.28, 0.38))
create("M_Portal", (0.025, 0.32, 0.8), emissive=True)
unreal.log("TOWER_MATERIALS_READY")
