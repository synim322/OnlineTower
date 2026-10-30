"""Run inside Unreal Editor Python. Imports only the MountainsV2 owned assets.
No level is changed. Existing same-name assets are retained, not overwritten.
The resulting mesh bounds are checked to catch any FBX unit conversion error.
"""
import unreal
import json
from pathlib import Path

SOURCE = Path(unreal.Paths.project_dir()).resolve().parent / 'Art' / 'Models' / 'MountainsV2'
DEST = '/Game/Environment/MountainsV2'
MATERIAL_ROOT = '/Game/Core/Materials'
report = json.loads((SOURCE / 'technical_report.json').read_text(encoding='utf-8'))
unreal.EditorAssetLibrary.make_directory(DEST)
tools = unreal.AssetToolsHelpers.get_asset_tools()
results = []
for spec in report['assets']:
    name = spec['name']
    path = DEST + '/' + name
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', str(SOURCE / (name + '.fbx')))
        task.set_editor_property('destination_path', DEST)
        task.set_editor_property('destination_name', name)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', False)
        task.set_editor_property('save', True)
        options = unreal.FbxImportUI()
        options.set_editor_property('import_mesh', True)
        options.set_editor_property('import_as_skeletal', False)
        options.set_editor_property('import_materials', False)
        options.set_editor_property('import_textures', False)
        options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
        data = options.static_mesh_import_data
        data.set_editor_property('combine_meshes', True)
        data.set_editor_property('auto_generate_collision', False)
        data.set_editor_property('generate_lightmap_u_vs', False)
        data.set_editor_property('convert_scene', True)
        data.set_editor_property('convert_scene_unit', True)
        data.set_editor_property('import_uniform_scale', 1.0)
        data.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        task.set_editor_property('options', options)
        tools.import_asset_tasks([task])
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError('StaticMesh import failed: ' + path)
    bounds = mesh.get_bounds()
    actual = [bounds.box_extent.x * 2, bounds.box_extent.y * 2, bounds.box_extent.z * 2]
    expected = spec['bounds_cm']
    if any(abs(a-e) > max(5, e * .01) for a,e in zip(actual, expected)):
        raise RuntimeError('FBX dimensions mismatch %s: actual=%s expected=%s' % (path,actual,expected))
    material_name = 'M_EnvSandstoneV2' if name == 'SM_MesaD' else 'M_EnvRockV2'
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL_ROOT + '/' + material_name)
    if material:
        mesh.set_material(0, material)
    else:
        unreal.log_warning('Material not yet available: ' + material_name + '; mesh imported, assignment pending')
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    results.append({'asset':path,'bounds_cm':actual,'material_assigned':bool(material)})
unreal.log('MOUNTAINS_V2_IMPORT ' + json.dumps(results))
