import sys
import os
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from Tools.MCP.unreal_mcp import UnrealMcpClient
import json

def run():
    client = UnrealMcpClient()
    client.connect()
    
    print("=== WERYFIKACJA SYSTEMU BRAMY I PLYTY NACISKOWEJ NA SCENIE ===")
    
    # 1. Sprawdzenie Gate_Arena_Entrance
    gate_res = client.call_tool("find_actors", "editor_toolset.toolsets.scene.SceneTools", {
        "name": "Gate", "tag": "", "collision_channels": []
    })
    gates = [g for g in gate_res.get("returnValue", []) if "gate" in g.get("refPath", "").lower()]
    if not gates:
        print("ERROR: Nie znaleziono bramy na scenie!")
        return False
        
    gate_ref = gates[0]
    print(f"1. Znaleziono Brame: {gate_ref['refPath']}")
    
    gate_tf = client.call_tool("get_actor_transform", "editor_toolset.toolsets.actor.ActorTools", {"actor": gate_ref})
    print(f"   Transform: {gate_tf['returnValue']}")
    
    gate_props = client.call_tool("get_properties", "editor_toolset.toolsets.object.ObjectTools", {
        "instance": gate_ref,
        "properties": [
            "materialType", "gateState", "openOffset", "openSpeed", 
            "closeSpeed", "blockBehavior", "crushDamage", "bStartOpen"
        ]
    })
    gate_data = json.loads(gate_props["returnValue"])
    print(f"   Stan poczatkowy bramy: {gate_data.get('gateState')}")
    print(f"   Typ materialu (Swieta Trojca): {gate_data.get('materialType')}")
    print(f"   Predkosci: Open={gate_data.get('openSpeed')} cm/s, Close={gate_data.get('closeSpeed')} cm/s")
    print(f"   Offset Otwarcia: {gate_data.get('openOffset')}")
    print(f"   Anti-Crush zachowanie: {gate_data.get('blockBehavior')}, Obrazenia: {gate_data.get('crushDamage')}")
    
    # 2. Sprawdzenie PressurePlate_Arena_Gate
    plate_res = client.call_tool("find_actors", "editor_toolset.toolsets.scene.SceneTools", {
        "name": "Plate", "tag": "", "collision_channels": []
    })
    plates = [p for p in plate_res.get("returnValue", []) if "plate" in p.get("refPath", "").lower()]
    if not plates:
        print("ERROR: Nie znaleziono plyty naciskowej na scenie!")
        return False
        
    plate_ref = plates[0]
    print(f"\n2. Znaleziono Plyte Naciskowa: {plate_ref['refPath']}")
    
    plate_tf = client.call_tool("get_actor_transform", "editor_toolset.toolsets.actor.ActorTools", {"actor": plate_ref})
    print(f"   Transform: {plate_tf['returnValue']}")
    
    plate_props = client.call_tool("get_properties", "editor_toolset.toolsets.object.ObjectTools", {
        "instance": plate_ref,
        "properties": [
            "bIsActive", "requiredMass", "defaultCharacterMass", "bAllowSwitchBack", "targetMechanisms"
        ]
    })
    plate_data = json.loads(plate_props["returnValue"])
    print(f"   Stan logiczny poczatkowy: {plate_data.get('bIsActive')}")
    print(f"   Wymagana masa: {plate_data.get('requiredMass')} kg (Domyslna gracza: {plate_data.get('defaultCharacterMass')} kg)")
    print(f"   Powrot po zwolnieniu (bAllowSwitchBack): {plate_data.get('bAllowSwitchBack')}")
    
    targets = plate_data.get("targetMechanisms", [])
    print(f"   Liczba podpietych mechanizmow: {len(targets)}")
    for t in targets:
        print(f"     -> Cel: {t.get('refPath')}")
        
    is_connected = any(t.get("refPath") == gate_ref.get("refPath") for t in targets)
    if is_connected:
        print("\n>>> SUKCES: Plyta naciskowa jest poprawnie polaczona z Brama lochu!")
    else:
        print("\n>>> OSTRZEZENIE: Brak bezposredniego dopasowania refPath w TargetMechanisms.")
        
    return True

if __name__ == "__main__":
    run()
