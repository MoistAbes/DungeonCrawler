import sys
import os
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from Tools.MCP.unreal_mcp import UnrealMcpClient
import json

def run():
    client = UnrealMcpClient()
    client.connect()
    
    # 1. Usuń istniejące instancje Gate_Arena_Entrance i PressurePlate_Arena_Gate jeśli już są
    for name in ["Gate_Arena_Entrance", "PressurePlate_Arena_Gate"]:
        res = client.call_tool("find_actors", "editor_toolset.toolsets.scene.SceneTools", {
            "name": name, "tag": "", "collision_channels": []
        })
        for act in res.get("returnValue", []):
            print(f"Removing existing {name}: {act}")
            client.call_tool("remove_from_scene", "editor_toolset.toolsets.scene.SceneTools", {"actor": act})

    # 2. Spawnowanie Bramy (Gate_Arena_Entrance)
    gate_xform = {
        "location": {"x": 5250.0, "y": 900.0, "z": 10.0},
        "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
        "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
    }
    gate_res = client.call_tool("add_to_scene_from_asset", "editor_toolset.toolsets.scene.SceneTools", {
        "asset_path": "/Game/Interactive/Mechanisms/BP_DungeonGate_Portcullis.BP_DungeonGate_Portcullis",
        "name": "Gate_Arena_Entrance",
        "xform": gate_xform,
        "snap_to_ground": False
    })
    gate_actor = gate_res.get("returnValue")
    print("Spawned Gate:", gate_actor)
    if gate_actor:
        client.call_tool("set_actor_folder", "editor_toolset.toolsets.scene.SceneTools", {
            "actor": gate_actor,
            "folder_path": "Dungeon_Generated/Gameplay"
        })

    # 3. Spawnowanie Płyty Naciskowej (PressurePlate_Arena_Gate)
    plate_xform = {
        "location": {"x": 4950.0, "y": 900.0, "z": 10.0},
        "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
        "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
    }
    plate_res = client.call_tool("add_to_scene_from_asset", "editor_toolset.toolsets.scene.SceneTools", {
        "asset_path": "/Game/Interactive/Mechanisms/BP_PressurePlate.BP_PressurePlate",
        "name": "PressurePlate_Arena_Gate",
        "xform": plate_xform,
        "snap_to_ground": False
    })
    plate_actor = plate_res.get("returnValue")
    print("Spawned PressurePlate:", plate_actor)
    if plate_actor:
        client.call_tool("set_actor_folder", "editor_toolset.toolsets.scene.SceneTools", {
            "actor": plate_actor,
            "folder_path": "Dungeon_Generated/Gameplay"
        })

    # 4. Połączenie Płyty z Bramą (TargetMechanisms)
    if plate_actor and gate_actor:
        # W Unreal format dla TArray<TObjectPtr<AActor>> to lista refPath
        target_vals = {
            "targetMechanisms": [gate_actor]
        }
        res_target = client.call_tool("set_properties", "editor_toolset.toolsets.object.ObjectTools", {
            "instance": plate_actor,
            "values": json.dumps(target_vals)
        })
        print("Connected PressurePlate to Gate:", res_target)

    # 5. Podłączenie Dźwigni BP_Switch_Lever_C_2 również do Bramy
    lever_res = client.call_tool("find_actors", "editor_toolset.toolsets.scene.SceneTools", {
        "name": "BP_Switch_Lever_C_2", "tag": "", "collision_channels": []
    })
    levers = lever_res.get("returnValue", [])
    if levers and gate_actor:
        lever_act = levers[0]
        lever_target_vals = {
            "targetMechanisms": [gate_actor]
        }
        res_lev_target = client.call_tool("set_properties", "editor_toolset.toolsets.object.ObjectTools", {
            "instance": lever_act,
            "values": json.dumps(lever_target_vals)
        })
        print("Connected Lever to Gate:", res_lev_target)

    # 6. Zapisanie poziomu
    save_res = client.save_level()
    print("Save Level result:", save_res)

if __name__ == "__main__":
    run()
