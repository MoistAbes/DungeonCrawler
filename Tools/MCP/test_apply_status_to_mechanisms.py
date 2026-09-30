import sys
import os
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from Tools.MCP.unreal_mcp import UnrealMcpClient
import json

def run():
    client = UnrealMcpClient()
    client.connect()

    print("=== TEST APLIKACJI STATUSU NA PLYCIE NACISKOWEJ I BRAMIE ===")

    # Weryfikacja polaczenia mechanizmow
    res_verify = client.call_tool("get_properties", "editor_toolset.toolsets.object.ObjectTools", {
        "instance": {"refPath": "/Game/Maps/Map_Dungeon_01.Map_Dungeon_01:PersistentLevel.BP_PressurePlate_C_1"},
        "properties": ["targetMechanisms", "bIsActive"]
    })
    print("Stan Plyty:", res_verify.get("returnValue"))

    return True

if __name__ == "__main__":
    run()
