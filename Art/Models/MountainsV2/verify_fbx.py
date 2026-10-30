"""Independent FBX roundtrip inspection in Blender; writes evidence beside files."""
import bpy,json
from pathlib import Path
out=Path(__file__).resolve().parent
evidence=[]
for f in sorted(out.glob('SM_*.fbx')):
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=str(f))
    objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
    assert len(objects)==1
    o=objects[0]; o.data.calc_loop_triangles()
    dims=[round(float(x)*100,3) for x in o.dimensions]
    assert abs(dims[0]-10000)<1 and abs(dims[1]-10000)<1
    assert len(o.data.uv_layers)==1
    assert len(o.data.loop_triangles)==10064
    evidence.append({'file':f.name,'roundtrip_bounds_cm':dims,'triangles':len(o.data.loop_triangles),'uv_channels':len(o.data.uv_layers),'material_slots':len(o.material_slots)})
(out/'fbx_roundtrip.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
print('FBX_ROUNDTRIP_PASS')
