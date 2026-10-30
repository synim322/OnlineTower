"""Import original EnvironmentV2 bitmap maps; create ISM-compatible triplanar materials."""
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve().parent / 'Art/Textures/EnvironmentV2'
TEXTURES = '/Game/Core/Textures/EnvironmentV2'
MATERIALS = '/Game/Core/Materials'
LIB = unreal.MaterialEditingLibrary
ASSETS = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
SETS = [('Rock', 500., 0.), ('Sandstone', 500., 0.), ('Sand', 250., 0.),
        ('Terrazzo', 250., 0.), ('Wood', 150., 0.), ('Metal', 200., .65),
        ('Shrub', 100., 0.)]
OLD_NAMES = ['M_OfficeWall', 'M_OfficeFloor', 'M_OfficeMetal', 'M_OfficeWood',
             'M_OfficeGlass', 'M_OfficeLight', 'M_MountainRock', 'M_ArenaStone',
             'M_DungeonMetal', 'M_DungeonGlow', 'M_DesertSand', 'M_DesertRock', 'M_Shrub']

COMMON = '''
float3 p = WorldPos / max(TileSize, 1.0);
float3 n = normalize(WorldNorm);
float3 w = pow(abs(n), 4.0);
w /= max(w.x + w.y + w.z, 0.0001);
float3 a = Texture2DSample(Tex, TexSampler, p.yz).rgb;
float3 b = Texture2DSample(Tex, TexSampler, p.xz).rgb;
float3 c = Texture2DSample(Tex, TexSampler, p.xy).rgb;
'''
COLOR_CODE = COMMON + 'return a*w.x + b*w.y + c*w.z;'
# Normal maps are imported as linear RGB (not BC5) to preserve explicit XYZ
# for custom triplanar reprojection. DirectX green is inverted into UV slope.
NORMAL_CODE = COMMON + '''
a = a * 2.0 - 1.0; b = b * 2.0 - 1.0; c = c * 2.0 - 1.0;
float3 ax = float3(sign(n.x)*a.z, a.x, -a.y);
float3 by = float3(b.x, sign(n.y)*b.z, -b.y);
float3 cz = float3(c.x, -c.y, sign(n.z)*c.z);
return normalize(ax*w.x + by*w.y + cz*w.z);
'''


def save(asset):
    if not ASSETS.save_loaded_asset(asset):
        raise RuntimeError('Failed saving ' + asset.get_path_name())


def usage(material):
    LIB.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    if not material.get_editor_property('used_with_instanced_static_meshes'):
        raise RuntimeError('ISM usage flag not set: ' + material.get_name())
    LIB.recompile_material(material)
    save(material)


def repair_originals():
    backup_dir = MATERIALS + '/Backups/EnvironmentV2'
    for name in OLD_NAMES:
        path = MATERIALS + '/' + name
        material = ASSETS.load_asset(path)
        if not material or material.get_editor_property('used_with_instanced_static_meshes'):
            continue
        ASSETS.make_directory(backup_dir)
        backup = backup_dir + '/' + name
        if not ASSETS.does_asset_exist(backup):
            copied = ASSETS.duplicate_asset(path, backup)
            if not copied:
                raise RuntimeError('Backup failed: ' + path)
            save(copied)
        usage(material)
        unreal.log('TOWER_MATERIAL_ISM_REPAIRED ' + path)


def import_texture(name, kind):
    source = ROOT / name / (kind + '.png')
    if not source.is_file():
        raise RuntimeError('Missing texture source: ' + str(source))
    asset_name = 'T_' + name + '_' + kind
    path = TEXTURES + '/' + asset_name
    if ASSETS.does_asset_exist(path):
        return ASSETS.load_asset(path)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(source))
    task.set_editor_property('destination_path', TEXTURES)
    task.set_editor_property('destination_name', asset_name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', False)
    task.set_editor_property('save', False)
    TOOLS.import_asset_tasks([task])
    texture = ASSETS.load_asset(path)
    if texture is None:
        raise RuntimeError('Texture import failed: ' + path)
    texture.set_editor_property('srgb', kind == 'BaseColor')
    if kind != 'BaseColor':
        # Raw linear RGB is deliberate: Custom HLSL explicitly decodes XYZ.
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    save(texture)
    return texture


def node(material, cls, x, y):
    result = LIB.create_material_expression(material, cls, x, y)
    if result is None:
        raise RuntimeError('Expression creation failed: ' + str(cls))
    return result


def connect(a, b, pin):
    if not LIB.connect_material_expressions(a, '', b, pin):
        raise RuntimeError('Connection failed: ' + pin)


def output(a, prop):
    if not LIB.connect_material_property(a, '', prop):
        raise RuntimeError('Property connection failed: ' + str(prop))


def create_material(name, size, metallic, maps):
    material_name = 'M_Env' + name + 'V2'
    path = MATERIALS + '/' + material_name
    if ASSETS.does_asset_exist(path):
        usage(ASSETS.load_asset(path))
        unreal.log('TOWER_V2_MATERIAL_KEPT ' + path)
        return
    mat = TOOLS.create_asset(material_name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError('Material creation failed: ' + path)
    mat.set_editor_property('tangent_space_normal', False)
    position = node(mat, unreal.MaterialExpressionWorldPosition, -1000, -150)
    vertex_normal = node(mat, unreal.MaterialExpressionVertexNormalWS, -1000, 0)
    tile = node(mat, unreal.MaterialExpressionScalarParameter, -1000, 150)
    tile.set_editor_property('parameter_name', 'TileSize')
    tile.set_editor_property('default_value', size)
    channels = {}
    for index, kind in enumerate(('BaseColor', 'Normal', 'Roughness')):
        y = 350 * index
        texture = node(mat, unreal.MaterialExpressionTextureObjectParameter, -800, y + 300)
        texture.set_editor_property('parameter_name', kind + 'Map')
        texture.set_editor_property('texture', maps[kind])
        sample = node(mat, unreal.MaterialExpressionCustom, -400, y)
        sample.set_editor_property('description', 'World triplanar ' + kind)
        sample.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        sample.set_editor_property('code', NORMAL_CODE if kind == 'Normal' else COLOR_CODE)
        inputs = []
        for pin in ('WorldPos', 'WorldNorm', 'Tex', 'TileSize'):
            item = unreal.CustomInput()
            item.set_editor_property('input_name', pin)
            inputs.append(item)
        sample.set_editor_property('inputs', inputs)
        for source, pin in ((position, 'WorldPos'), (vertex_normal, 'WorldNorm'),
                            (texture, 'Tex'), (tile, 'TileSize')):
            connect(source, sample, pin)
        channels[kind] = sample
    tint = node(mat, unreal.MaterialExpressionVectorParameter, -100, -200)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(1, 1, 1, 1))
    multiply = node(mat, unreal.MaterialExpressionMultiply, 100, 0)
    connect(channels['BaseColor'], multiply, 'A')
    connect(tint, multiply, 'B')
    output(multiply, unreal.MaterialProperty.MP_BASE_COLOR)
    output(channels['Normal'], unreal.MaterialProperty.MP_NORMAL)
    output(channels['Roughness'], unreal.MaterialProperty.MP_ROUGHNESS)
    metal = node(mat, unreal.MaterialExpressionScalarParameter, 100, 800)
    metal.set_editor_property('parameter_name', 'Metallic')
    metal.set_editor_property('default_value', metallic)
    output(metal, unreal.MaterialProperty.MP_METALLIC)
    usage(mat)
    unreal.log('TOWER_V2_MATERIAL_CREATED ' + path)


def main():
    repair_originals()
    ASSETS.make_directory(TEXTURES)
    for name, size, metallic in SETS:
        maps = {kind: import_texture(name, kind) for kind in ('BaseColor', 'Normal', 'Roughness')}
        create_material(name, size, metallic, maps)
    unreal.log('TOWER_ENVIRONMENT_TEXTURES_V2_READY sets=7 maps=21')


if __name__ == '__main__':
    main()
