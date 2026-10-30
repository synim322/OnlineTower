"""Original procedural environment meshes. Blender 4.3, no external dependencies.
Run: blender --background --python build_mountains.py
Meters in Blender; FBX unit metadata converts to centimeters in Unreal.
"""
import bpy, math, json, hashlib
from pathlib import Path
from mathutils import Vector, noise

OUT = Path(__file__).resolve().parent
OUT.mkdir(parents=True, exist_ok=True)
N = 68
SPECS = [('SM_RidgeA', 46., 13), ('SM_PeakB', 65., 27),
         ('SM_SaddleC', 39., 41), ('SM_MesaD', 32., 58), ('SM_CliffE', 48., 76)]
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1.0

def ns(x,y,f,seed):
    return noise.noise_vector(Vector((x*f+seed,y*f-seed,seed*.31)), noise_basis='PERLIN_ORIGINAL').z
def g(x,y,cx,cy,sx,sy):
    return math.exp(-((x-cx)/sx)**2-((y-cy)/sy)**2)
def height(kind,x,y,seed):
    # Asymmetrical geology-shaped masses, then domain-warped multiscale erosion.
    wx=x+.065*ns(x,y,3,seed); wy=y+.06*ns(x,y,3,seed+9)
    if kind=='SM_RidgeA':
        ridge=.16*math.sin(wx*4)+.11*wx
        h=(.75+.18*math.sin(wx*6+.4)+.12*math.cos(wx*11))*math.exp(-((wy-ridge)/.27)**2)*math.exp(-(wx/.83)**4)
        h+=.28*g(wx,wy,-.42,-.36,.38,.25)+.20*g(wx,wy,.55,.38,.3,.25)
    elif kind=='SM_PeakB':
        h=.96*g(wx,wy,-.17,.11,.30,.37)+.62*g(wx,wy,.31,-.18,.23,.33)+.39*g(wx,wy,-.43,-.32,.29,.24)
        h+=.24*g(wx,wy,.10,.51,.40,.18)+.13*g(wx,wy,.55,.33,.29,.29)
    elif kind=='SM_SaddleC':
        h=.89*g(wx,wy,-.46,.12,.25,.43)+.76*g(wx,wy,.45,-.14,.28,.4)+.3*g(wx,wy,0,0,.62,.3)
        h+=.18*g(wx,wy,.13,.50,.48,.18)
    elif kind=='SM_MesaD':
        # Flat crown, offset lobes and ledged sedimentary flanks, not a cone.
        d=((wx+.06)/.65)**4+((wy-.03)/.49)**4
        crown=max(0,min(1,(1.32-d)*2.4))
        skirt=.32*math.exp(-((wx/.88)**4+(wy/.76)**4))
        h=max(crown,skirt)
        if .12<h<.92: h=.72*h+.28*math.floor(h*9)/9
        return h
    else:
        # Broad cliff wall with a raised rear shelf and eroded toe buttresses.
        shelf=1/(1+math.exp(-18*(wx+.08+.07*math.sin(wy*8))))
        h=shelf*(.75+.17*math.sin(wy*4+.5))*math.exp(-(wy/.77)**6)*math.exp(-((wx-.2)/.72)**6)
        h+=.28*g(wx,wy,-.35,-.45,.24,.27)+.22*g(wx,wy,-.37,.32,.28,.2)
    fine=.09*ns(wx,wy,8,seed)+.038*ns(wx,wy,21,seed+12)+.014*ns(wx,wy,43,seed+3)
    channels=.06*(1-abs(ns(wx,wy,13,seed+8)))
    return max(0,h*(1+fine)-channels*h)

objects=[]; report=[]
for kind,target,seed in SPECS:
    values=[]
    for j in range(N+1):
        for i in range(N+1):
            x=2*i/N-1; y=2*j/N-1
            # Feather entire perimeter to the common closed base.
            edge=max(0,min(1,(1-max(abs(x),abs(y)))*9))
            values.append(height(kind,x,y,seed)*edge)
    peak=max(values); verts=[]
    for j in range(N+1):
        for i in range(N+1):
            idx=j*(N+1)+i
            verts.append(((i/N-.5)*100,(j/N-.5)*100,values[idx]/peak*target+.12))
    faces=[]
    for j in range(N):
        for i in range(N):
            a=j*(N+1)+i; b=a+1; c=a+N+2; d=a+N+1
            if (i+j)%2: faces.extend([(a,b,d),(b,c,d)])
            else: faces.extend([(a,b,c),(a,c,d)])
    ring=list(range(N+1))+[j*(N+1)+N for j in range(1,N+1)]+[N*(N+1)+i for i in range(N-1,-1,-1)]+[j*(N+1) for j in range(N-1,0,-1)]
    bottom=[]
    for k in ring:
        bottom.append(len(verts)); verts.append((verts[k][0],verts[k][1],0))
    center=len(verts); verts.append((0,0,0))
    for k,a in enumerate(ring):
        nxt=(k+1)%len(ring); b=ring[nxt]; c=bottom[nxt]; d=bottom[k]
        faces.extend([(a,d,c),(a,c,b),(center,c,d)])
    mesh=bpy.data.meshes.new(kind); mesh.from_pydata(verts,[],faces); mesh.update()
    obj=bpy.data.objects.new(kind,mesh); scene.collection.objects.link(obj)
    bpy.context.view_layer.objects.active=obj; obj.select_set(True)
    uv=mesh.uv_layers.new(name='UVMap')
    for poly in mesh.polygons:
        poly.use_smooth=True
        for li in poly.loop_indices:
            co=mesh.vertices[mesh.loops[li].vertex_index].co
            # Dominant-plane mapping gives steep walls texture area.
            normal=poly.normal
            if abs(normal.z)>.55: uv.data[li].uv=(co.x/100+.5,co.y/100+.5)
            elif abs(normal.x)>abs(normal.y): uv.data[li].uv=(co.y/100+.5,co.z/100)
            else: uv.data[li].uv=(co.x/100+.5,co.z/100)
    mat=bpy.data.materials.get('Sandstone' if kind=='SM_MesaD' else 'Rock')
    if mat is None:
        mat=bpy.data.materials.new('Sandstone' if kind=='SM_MesaD' else 'Rock')
        mat.diffuse_color=(.48,.29,.13,1) if kind=='SM_MesaD' else (.25,.31,.37,1)
    obj.data.materials.append(mat)
    bpy.ops.export_scene.fbx(filepath=str(OUT/(kind+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',use_mesh_modifiers=True,mesh_smooth_type='FACE',add_leaf_bones=False,bake_anim=False)
    # Validate manifold closure and nonzero areas in the exported source.
    counts={}
    for p in mesh.polygons:
        for ek in p.edge_keys: counts[tuple(sorted(ek))]=counts.get(tuple(sorted(ek)),0)+1
    assert all(v==2 for v in counts.values()), kind+' not manifold'
    assert min(p.area for p in mesh.polygons)>1e-8, kind+' degenerate triangle'
    assert len(mesh.polygons)<15000
    report.append(dict(name=kind,vertices=len(mesh.vertices),triangles=len(mesh.polygons),bounds_cm=[10000,10000,round((target+.12)*100,3)],pivot='base center z=0',material_slot=mat.name,uv='dominant-plane UV0; tiled overlap intentional; no lightmap UV',closed_manifold=True,degenerate_triangles=0,fbx_sha256=hashlib.sha256((OUT/(kind+'.fbx')).read_bytes()).hexdigest()))
    obj.select_set(False); objects.append(obj)

# Source scene preserves each production object at the origin in separate collections.
for obj in objects:
    coll=bpy.data.collections.new(obj.name); scene.collection.children.link(coll)
    scene.collection.objects.unlink(obj); coll.objects.link(obj)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'MountainsV2.blend'))
(OUT/'technical_report.json').write_text(json.dumps({'generator':'Blender 4.3.2 Python, original procedural geometry, no downloaded assets','units':'Blender meters, FBX converted to centimeters by UE import unit conversion','grid_segments':N,'assets':report,'limitations':['Not a canonical game-dev package; CLI unavailable.','UE import/render/collision validation pending coordinator.','No generated collision or LOD; background environment meshes, gameplay boundary remains separate.','Dominant-plane UV supports tiling with seams; prefer world-aligned rock material.']},indent=2),encoding='utf-8')

# Separate overview composition; source .blend above remains unmodified.
placements=[(-115,60,0),(0,60,0),(115,60,0),(-60,-65,0),(65,-65,0)]
for obj,pos in zip(objects,placements): obj.location=pos
camdata=bpy.data.cameras.new('PreviewCamera'); cam=bpy.data.objects.new('PreviewCamera',camdata); scene.collection.objects.link(cam)
cam.location=(210,-320,270); direction=Vector((0,0,15))-cam.location; cam.rotation_euler=direction.to_track_quat('-Z','Y').to_euler(); camdata.type='ORTHO'; camdata.ortho_scale=375; camdata.clip_end=2000; scene.camera=cam
scene.render.engine='BLENDER_WORKBENCH'; scene.display.shading.light='STUDIO'; scene.display.shading.studio_light='paint.sl'; scene.display.shading.color_type='MATERIAL'; scene.display.shading.show_shadows=True; scene.display.shading.show_cavity=True; scene.display.shading.cavity_type='BOTH'; scene.display.shading.curvature_ridge_factor=1.5; scene.display.shading.curvature_valley_factor=1.1; scene.display.shading.background_type='WORLD'; scene.world.color=(.12,.14,.17)
scene.render.resolution_x=1800; scene.render.resolution_y=1300; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'; scene.render.filepath=str(OUT/'MountainsV2-preview.png'); bpy.ops.render.render(write_still=True)
print('MOUNTAINS_V2_COMPLETE '+str(OUT))
