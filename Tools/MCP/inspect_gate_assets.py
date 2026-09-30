import unreal
import json

def run():
    results = {}
    
    # 1. Search for StaticMeshes matching gate, portcullis, door, bars, grid, grate, iron, stone
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = ar.get_assets_by_class("StaticMesh", True)
    
    keywords = ["gate", "door", "portcullis", "bars", "grate", "fence", "iron", "frame", "arch"]
    matched_meshes = []
    
    for a in assets:
        path = str(a.package_name)
        # Filter for relevant keywords
        if any(k in path.lower() for k in keywords):
            matched_meshes.append(path)
            
    results["matched_meshes"] = matched_meshes[:40] # cap
    
    # 2. Check existing actors on Map_Dungeon_01
    editor_actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors = editor_actor_subsys.get_all_level_actors()
    
    pressure_plates = []
    for act in all_actors:
        cls_name = act.get_class().get_name()
        if "pressure" in cls_name.lower() or "plate" in cls_name.lower():
            loc = act.get_actor_location()
            pressure_plates.append({
                "name": act.get_name(),
                "class": cls_name,
                "path": act.get_path_name(),
                "location": [loc.x, loc.y, loc.z]
            })
            
    results["pressure_plates"] = pressure_plates
    
    # 3. Check if ADungeonGateProp class is visible to reflection
    gate_class = unreal.load_class(None, "/Script/MyProject.DungeonGateProp")
    results["gate_class_loaded"] = gate_class is not None
    if gate_class:
        results["gate_class_name"] = gate_class.get_name()
        
    return results
