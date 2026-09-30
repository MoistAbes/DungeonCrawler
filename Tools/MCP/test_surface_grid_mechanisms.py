import sys
import os
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from Tools.MCP.unreal_mcp import UnrealMcpClient
import json

def run():
    client = UnrealMcpClient()
    client.connect()

    print("=== TEST WERYFIKACYJNY: MECHANIZMY JAKO POWIERZCHNIE KOMOREK ===")

    # 1. Pobranie referencji do Bramy i Plyty
    gate_res = client.call_tool("find_actors", "editor_toolset.toolsets.scene.SceneTools", {
        "name": "Gate", "tag": "", "collision_channels": []
    })
    gates = [g for g in gate_res.get("returnValue", []) if "gate" in g.get("refPath", "").lower()]
    if not gates:
        print("ERROR: Nie znaleziono bramy na scenie!")
        return False
    gate_ref = gates[0]

    plate_res = client.call_tool("find_actors", "editor_toolset.toolsets.scene.SceneTools", {
        "name": "Plate", "tag": "", "collision_channels": []
    })
    plates = [p for p in plate_res.get("returnValue", []) if "plate" in p.get("refPath", "").lower()]
    if not plates:
        print("ERROR: Nie znaleziono plyty na scenie!")
        return False
    plate_ref = plates[0]

    print(f"Brama: {gate_ref['refPath']}")
    print(f"Plyta: {plate_ref['refPath']}")

    # 2. Sprawdzenie komponentow Bramy - upewnienie sie ze StatusEffectComponent zostal usuniety
    gate_comps = client.call_tool("get_components", "editor_toolset.toolsets.actor.ActorTools", {"actor": gate_ref})
    gate_comp_list = [c.get("refPath", "") for c in gate_comps.get("returnValue", [])]
    print("\nKomponenty Bramy:")
    has_status_gate = False
    for gc in gate_comp_list:
        print(f"  - {gc.split(':')[-1]}")
        if "StatusEffect" in gc:
            has_status_gate = True

    # 3. Sprawdzenie komponentow Plyty - upewnienie sie ze StatusEffectComponent zostal usuniety
    plate_comps = client.call_tool("get_components", "editor_toolset.toolsets.actor.ActorTools", {"actor": plate_ref})
    plate_comp_list = [c.get("refPath", "") for c in plate_comps.get("returnValue", [])]
    print("\nKomponenty Plyty:")
    has_status_plate = False
    for pc in plate_comp_list:
        print(f"  - {pc.split(':')[-1]}")
        if "StatusEffect" in pc:
            has_status_plate = True

    print(f"\nStatusEffectComponent usuniety z Bramy: {not has_status_gate}")
    print(f"StatusEffectComponent usuniety z Plyty: {not has_status_plate}")

    # 4. Sprawdzenie wlasciwosci CDO Blueprintow
    gate_cdo = client.call_tool("get_default_object", "editor_toolset.toolsets.blueprint.BlueprintTools", {
        "blueprint": {"refPath": "/Game/Interactive/Mechanisms/BP_DungeonGate_Portcullis.BP_DungeonGate_Portcullis"}
    })
    print("\nBrama CDO:", gate_cdo.get("returnValue"))

    plate_cdo = client.call_tool("get_default_object", "editor_toolset.toolsets.blueprint.BlueprintTools", {
        "blueprint": {"refPath": "/Game/Interactive/Mechanisms/BP_PressurePlate.BP_PressurePlate"}
    })
    print("Plyta CDO:", plate_cdo.get("returnValue"))

    return True

if __name__ == "__main__":
    run()
