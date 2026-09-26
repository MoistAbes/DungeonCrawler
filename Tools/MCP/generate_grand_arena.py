import json

def spawn_asset(asset_path, name, x, y, z, pitch=0.0, yaw=0.0, roll=0.0, sx=1.0, sy=1.0, sz=1.0, folder=""):
    xform = {
        "location": {"x": float(x), "y": float(y), "z": float(z)},
        "rotation": {"pitch": float(pitch), "yaw": float(yaw), "roll": float(roll)},
        "scale": {"x": float(sx), "y": float(sy), "z": float(sz)}
    }
    payload = {
        "asset_path": asset_path,
        "name": name,
        "xform": xform,
        "parent": None,
        "snap_to_ground": False
    }
    res = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps(payload))
    actor = res["returnValue"] if "returnValue" in res else None
    if folder and actor:
        execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({"actor": actor, "folder_path": folder}))
    return actor

def spawn_class(class_path, name, x, y, z, pitch=0.0, yaw=0.0, roll=0.0, sx=1.0, sy=1.0, sz=1.0, folder=""):
    xform = {
        "location": {"x": float(x), "y": float(y), "z": float(z)},
        "rotation": {"pitch": float(pitch), "yaw": float(yaw), "roll": float(roll)},
        "scale": {"x": float(sx), "y": float(sy), "z": float(sz)}
    }
    payload = {
        "actor_type": {"refPath": class_path},
        "name": name,
        "xform": xform,
        "parent": None,
        "snap_to_ground": False
    }
    res = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_class", json.dumps(payload))
    actor = res["returnValue"] if "returnValue" in res else None
    if folder and actor:
        execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({"actor": actor, "folder_path": folder}))
    return actor

def cleanup_arena_generation(parent_folder="Dungeon_Generated/GrandArena"):
    """Usuwa poprzednio wygenerowane obiekty Areny w przypadku re-generacji."""
    subfolders = ["Floors", "Ceilings", "Walls", "Balcony", "Pillars", "Ramp", "Targets", "Lighting"]
    for sub in subfolders:
        path = f"{parent_folder}/{sub}"
        try:
            res = execute_tool("editor_toolset.toolsets.scene.SceneTools.get_actors_in_folder", json.dumps({"folder_path": path, "recursive": True}))
            actors = res["returnValue"] if "returnValue" in res else []
            for a in actors:
                execute_tool("editor_toolset.toolsets.scene.SceneTools.remove_from_scene", json.dumps({"actor": a}))
        except Exception:
            pass

def run():
    counts = {
        "floors": 0, "ceilings": 0, "walls": 0,
        "balcony_tiles": 0, "pillars": 0, "ramps": 0,
        "targets": 0, "lights": 0
    }

    base_folder = "Dungeon_Generated/GrandArena"

    # 1. Czyszczenie poprzedniej instancji areny
    cleanup_arena_generation(base_folder)

    # 2. Otwarcie przejścia w ścianie wschodniej starej sali (X = 5250, Y = 900)
    all_res = execute_tool("editor_toolset.toolsets.scene.SceneTools.find_actors", json.dumps({
        "name": "Wall_R2_E_3",
        "tag": "",
        "collision_channels": []
    }))
    door_wall = all_res["returnValue"] if "returnValue" in all_res else []
    for dw in door_wall:
        execute_tool("editor_toolset.toolsets.scene.SceneTools.remove_from_scene", json.dumps({"actor": dw}))

    # Przesunięcie dźwigni Altar, jeśli tam stoi
    lever_res = execute_tool("editor_toolset.toolsets.scene.SceneTools.find_actors", json.dumps({
        "name": "Switch_Lever_Altar",
        "tag": "",
        "collision_channels": []
    }))
    levers = lever_res["returnValue"] if "returnValue" in lever_res else []
    for lev in levers:
        execute_tool("editor_toolset.toolsets.actor.ActorTools.set_actor_transform", json.dumps({
            "actor": lev,
            "xform": {
                "location": {"x": 5220.0, "y": 700.0, "z": 100.0},
                "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
                "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
            }
        }))

    # Ścieżki assetów
    wall_solid = "/Game/Dungeon/Structures/BP_DungeonWall_Solid"
    floor_cracked = "/Game/Dungeon/Structures/BP_DungeonFloor_Cracked"
    wall_wood = "/Game/Dungeon/Structures/BP_DungeonWall_Wood"
    wall_glass = "/Game/Dungeon/Structures/BP_DungeonWall_Glass"

    # Siatka: X od 5400 do 7800 (9 kafelków: 5400, 5700, 6000, 6300, 6600, 6900, 7200, 7500, 7800)
    #        Y od -300 do 2100 (9 kafelków: -300, 0, 300, 600, 900, 1200, 1500, 1800, 2100)
    x_coords = [5400.0 + i * 300.0 for i in range(9)]
    y_coords = [-300.0 + j * 300.0 for j in range(9)]

    # 3. Parter (Ground Floor) oraz Sufit (Ceiling)
    for x in x_coords:
        for y in y_coords:
            # Sprawdzenie, czy kafelek to strefa zrzutu na spękaną podłogę
            is_cracked_dropzone = (x == 6900.0 and (y == 900.0 or y == 1200.0))
            if is_cracked_dropzone:
                spawn_asset(floor_cracked, f"Arena_CrackedFloor_{int(x)}_{int(y)}", x, y, 10.0, roll=90.0, sx=3.0, sy=0.2, sz=3.0, folder=f"{base_folder}/Floors")
            else:
                spawn_asset(wall_solid, f"Arena_Floor_{int(x)}_{int(y)}", x, y, 10.0, roll=90.0, sx=3.0, sy=0.2, sz=3.0, folder=f"{base_folder}/Floors")
            counts["floors"] += 1

            # Sufit (Z = 930 cm)
            spawn_asset(wall_solid, f"Arena_Ceiling_{int(x)}_{int(y)}", x, y, 930.0, roll=90.0, sx=3.0, sy=0.2, sz=3.0, folder=f"{base_folder}/Ceilings")
            counts["ceilings"] += 1

    # 4. Ściany Obwodowe (Z = 470 cm, wysokość 900 cm, grubość 20 cm)
    # Północna ściana: Y = 2250, Yaw = 0.0
    for x in x_coords:
        spawn_asset(wall_solid, f"Arena_Wall_N_{int(x)}", x, 2250.0, 470.0, yaw=0.0, sx=3.0, sy=0.2, sz=9.0, folder=f"{base_folder}/Walls")
        counts["walls"] += 1

    # Południowa ściana: Y = -450, Yaw = 0.0
    for x in x_coords:
        spawn_asset(wall_solid, f"Arena_Wall_S_{int(x)}", x, -450.0, 470.0, yaw=0.0, sx=3.0, sy=0.2, sz=9.0, folder=f"{base_folder}/Walls")
        counts["walls"] += 1

    # Wschodnia ściana (tylna za balkonem): X = 7950, Yaw = 90.0
    for y in y_coords:
        spawn_asset(wall_solid, f"Arena_Wall_E_{int(y)}", 7950.0, y, 470.0, yaw=90.0, sx=3.0, sy=0.2, sz=9.0, folder=f"{base_folder}/Walls")
        counts["walls"] += 1

    # Zachodnia ściana (łącząca z poprzednią salą): X = 5250, Yaw = 90.0
    spawn_asset(wall_solid, "Arena_Wall_W_Minus300", 5250.0, -300.0, 470.0, yaw=90.0, sx=3.0, sy=0.2, sz=9.0, folder=f"{base_folder}/Walls")
    spawn_asset(wall_solid, "Arena_Wall_W_2100", 5250.0, 2100.0, 470.0, yaw=90.0, sx=3.0, sy=0.2, sz=9.0, folder=f"{base_folder}/Walls")
    counts["walls"] += 2

    # 5. Prosty Balkon (Mezzanine / Piętro) wzdłuż wschodniej ściany (Z = 310 cm, wierzch Z = 320 cm)
    # Głębokość 2 kafelki: X = 7500 i X = 7800, szerokość: wszystkie 9 kafelków Y
    for x in [7500.0, 7800.0]:
        for y in y_coords:
            spawn_asset(wall_solid, f"Arena_Balcony_{int(x)}_{int(y)}", x, y, 310.0, roll=90.0, sx=3.0, sy=0.2, sz=3.0, folder=f"{base_folder}/Balcony")
            counts["balcony_tiles"] += 1

    # 6. Filary Podporowe pod Krawędzią Balkonu (X = 7350, Y = 0, 600, 1200, 1800, Z = 170 cm)
    for y in [0.0, 600.0, 1200.0, 1800.0]:
        spawn_asset(wall_solid, f"Arena_Pillar_{int(y)}", 7350.0, y, 170.0, sx=0.8, sy=0.8, sz=3.0, folder=f"{base_folder}/Pillars")
        counts["pillars"] += 1

    # 7. Rampa Wjazdowa na Piętro (Kąt 26.565 stopni, szerokość 300 cm)
    # Zaczyna się na parterze X = 6750, Z = 20, wjeżdża na balkon X = 7350, Z = 320
    # Środek: X = 7050, Y = -300, Z = 170
    spawn_asset(wall_solid, "Arena_Ramp_Main", 7050.0, -300.0, 170.0, pitch=26.565, yaw=0.0, roll=0.0, sx=6.7082, sy=3.0, sz=0.2, folder=f"{base_folder}/Ramp")
    counts["ramps"] += 1

    # 8. Cele Testowe i Przeszkody Kinetyczne
    # A. Szklany poligon do sprintu i przebijania (BP_DungeonWall_Glass)
    for y in [600.0, 900.0, 1200.0]:
        spawn_asset(wall_glass, f"Arena_Target_GlassWall_{int(y)}", 6300.0, y, 170.0, yaw=90.0, sx=3.0, sy=0.2, sz=3.0, folder=f"{base_folder}/Targets")
        counts["targets"] += 1

    # B. Drewniana barykada do taranowania (BP_DungeonWall_Wood)
    for y in [600.0, 900.0]:
        spawn_asset(wall_wood, f"Arena_Target_WoodWall_{int(y)}", 5800.0, y, 170.0, yaw=90.0, sx=3.0, sy=0.2, sz=3.0, folder=f"{base_folder}/Targets")
        counts["targets"] += 1

    # C. Rekwizyty fizyczne na balkonie do zrzucania (Z = 340 cm, X = 7500)
    spawn_asset("/Game/Interactive/Props/BP_Stone", "Arena_Boulder_Balcony_1", 7450.0, 900.0, 360.0, sx=2.0, sy=2.0, sz=2.0, folder=f"{base_folder}/Targets")
    spawn_asset("/Game/Interactive/Props/BP_Stone", "Arena_Boulder_Balcony_2", 7450.0, 1200.0, 360.0, sx=1.5, sy=1.5, sz=1.5, folder=f"{base_folder}/Targets")
    spawn_asset("/Game/Interactive/Props/BP_ExplosiveBarrel", "Arena_ExplosiveBarrel_Balcony", 7450.0, 600.0, 360.0, folder=f"{base_folder}/Targets")
    spawn_asset("/Game/Interactive/Props/BP_Physics_Barrel", "Arena_PhysicsBarrel_Balcony_1", 7450.0, 1500.0, 360.0, folder=f"{base_folder}/Targets")
    spawn_asset("/Game/Interactive/Props/BP_KnockbackBarrel", "Arena_KnockbackBarrel_Balcony", 7450.0, 300.0, 360.0, folder=f"{base_folder}/Targets")
    spawn_asset("/Game/Interactive/Bombs/BP_Bomb_Knockback", "Arena_Bomb_Knockback_Balcony", 7400.0, 900.0, 340.0, folder=f"{base_folder}/Targets")
    counts["targets"] += 6

    # D. Rekwizyty fizyczne na parterze
    spawn_asset("/Game/Interactive/Props/BP_Physics_Barrel", "Arena_PhysicsBarrel_Floor_1", 6000.0, 750.0, 60.0, folder=f"{base_folder}/Targets")
    spawn_asset("/Game/Interactive/Props/BP_Physics_Barrel", "Arena_PhysicsBarrel_Floor_2", 6000.0, 1050.0, 60.0, folder=f"{base_folder}/Targets")
    counts["targets"] += 2

    # 9. Oświetlenie Areny (PointLights)
    spawn_class("/Script/Engine.PointLight", "Arena_Light_Center_West", 6000.0, 900.0, 550.0, folder=f"{base_folder}/Lighting")
    spawn_class("/Script/Engine.PointLight", "Arena_Light_Center_East", 6900.0, 900.0, 550.0, folder=f"{base_folder}/Lighting")
    spawn_class("/Script/Engine.PointLight", "Arena_Light_Balcony_North", 7600.0, 1500.0, 500.0, folder=f"{base_folder}/Lighting")
    spawn_class("/Script/Engine.PointLight", "Arena_Light_Balcony_South", 7600.0, 300.0, 500.0, folder=f"{base_folder}/Lighting")
    counts["lights"] += 4

    # 10. Zapis mapy na dysk
    execute_tool("editor_toolset.toolsets.asset.AssetTools.save_assets", json.dumps({
        "asset_paths": ["/Game/Maps/Map_Dungeon_01"]
    }))

    return {"status": "SUCCESS", "counts": counts}
