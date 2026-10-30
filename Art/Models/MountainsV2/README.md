# Mountains V2 — original procedural environment models

Created locally with Blender 4.3.2 on 2026-10-30. No provider generation, downloads, licensed third-party source assets or paid services were used. These are original project source assets, not a verified canonical `game-dev` package (CLI unavailable).

## Deliverables

- `MountainsV2.blend`: five production meshes, each in a named collection at local origin.
- `SM_*.fbx`: one mesh per file, ready for the explicit Unreal import step.
- `MountainsV2-preview.png`: Blender Workbench review render, not an Unreal screenshot. Rear row: RidgeA, PeakB, SaddleC. Front row: MesaD, CliffE.
- `build_mountains.py`: deterministic source generator. Seeds and geological mass functions live in the source. Re-running regenerates these owned outputs.
- `verify_fbx.py`: independent re-import validation.
- `technical_report.json`: triangles, vertices, bounds, manifold check, hashes and limitations.
- `fbx_roundtrip.json`: successful Blender FBX re-import evidence.

## Geometry contract

Each mesh is a closed heightfield volume with 5,034 vertices and 10,064 triangles. Surface shading is smooth. UV0 uses dominant-plane projections; intentional overlap is suitable for tiled materials, not unique baked maps or lightmaps. World-aligned surface materials are recommended to avoid visible chart transitions. Outer low skirts should overlap surrounding ground or adjacent mountains.

All pivots are centered at the footprint base, Z=0. Footprints are 10,000 × 10,000 cm. Blender uses meters, FBX carries unit conversion; roundtrip dimensions were verified. Unreal import script additionally checks bounds to catch incorrect conversion.

| Mesh | Height cm | Form | Material slot 0 |
|---|---:|---|---|
| SM_RidgeA | 4612 | Meandering ridge primarily along X, subsidiary spurs | Rock |
| SM_PeakB | 6512 | Unequal cluster of sharp peaks and offset shoulders | Rock |
| SM_SaddleC | 3912 | Two unequal summits separated by a low saddle | Rock |
| SM_MesaD | 3212 | Flat crown and stepped, eroded sandstone sides | Sandstone |
| SM_CliffE | 4812 | Raised shelf, cliff break along Y, toe buttresses | Rock |

For a desired size `(width, depth, height)` use scale `(width/10000, depth/10000, height/source_height)`. To place MesaD beneath a separate arena floor, use `actor_z = floor_z - 3212*scale_z`, with a small extra downward offset to avoid z-fighting. The flat crown is smaller than the footprint; inspect coverage for the arena diameter. Preserve separate gameplay floor and boundary collision.

## Build and inspect

```powershell
& 'C:\Program Files\Blender Foundation\Blender 4.3\blender.exe' --background --python 'Art\Models\MountainsV2\build_mountains.py'
& 'C:\Program Files\Blender Foundation\Blender 4.3\blender.exe' --background --python 'Art\Models\MountainsV2\verify_fbx.py'
```

Both commands completed successfully in the production session. All exported files were re-imported, producing one mesh each, correct dimensions, 10,064 triangles, one UV channel and one material slot. Source validation confirmed each edge has two incident faces and all triangle areas are nonzero. Preview inspection confirmed five distinct silhouettes.

## Unreal handoff

Coordinator runs `OnlineTower 5.8/Scripts/import_mountains_v2.py` inside Unreal after V2 materials are available. Destination `/Game/Environment/MountainsV2/`. Rock variants receive `/Game/Core/Materials/M_EnvRockV2`; mesa receives `/Game/Core/Materials/M_EnvSandstoneV2`.

The script retains existing same-name assets, imports normals, disables automatic collision/lightmap UV generation, validates dimensions and reports assignments. These background meshes do not include collision, LODs or a claimed performance budget. Unreal import, final textured appearance, placement, gameplay access and performance require separate verification by the coordinator. This production report does not claim those stages passed.
