"""Builds /Game/Maps/Lvl_CombatArena: a cover-rich arena with lanes for flanking.

Run headless:
  UnrealEditor.exe TacticalSquadAI.uproject -ExecutePythonScript=Scripts/build_combat_arena.py -quit_after_build
"""
import unreal

MAP_PATH = "/Game/Maps/Lvl_CombatArena"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
MAT_FLOOR = unreal.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray")
MAT_WALL = unreal.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark")
MAT_COVER = unreal.load_asset("/Game/LevelPrototyping/Materials/MI_DefaultColorway.MI_DefaultColorway")

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def block(label, x, y, sx, sy, h, material, folder, z0=0.0, yaw=0.0):
    """Axis-aligned box resting on the floor, size in cm."""
    actor = actors.spawn_actor_from_object(CUBE, unreal.Vector(x, y, z0 + h * 0.5), unreal.Rotator(0.0, 0.0, yaw))
    actor.set_actor_scale3d(unreal.Vector(sx / 100.0, sy / 100.0, h / 100.0))
    actor.static_mesh_component.set_material(0, material)
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    return actor


def spawn(cls, label, location, rotation=unreal.Rotator(), folder="Gameplay"):
    actor = actors.spawn_actor_from_class(cls, location, rotation)
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    return actor


if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
    # Rebuild in place so the script can be re-run after layout tweaks.
    if not levels.load_level(MAP_PATH):
        raise RuntimeError("Could not load " + MAP_PATH)
    for existing in actors.get_all_level_actors():
        if not isinstance(existing, (unreal.WorldSettings, unreal.Brush)) or isinstance(existing, unreal.Volume):
            actors.destroy_actor(existing)
elif not levels.new_level(MAP_PATH):
    raise RuntimeError("Could not create " + MAP_PATH)

# ---- Floor and perimeter -------------------------------------------------------------------------
block("Floor", 0, 0, 6400, 4400, 20, MAT_FLOOR, "Arena", z0=-20)
for label, x, y, sx, sy in [
    ("Wall_N", 0, 2200, 6400, 40), ("Wall_S", 0, -2200, 6400, 40),
    ("Wall_E", 3200, 0, 40, 4400), ("Wall_W", -3200, 0, 40, 4400),
]:
    block(label, x, y, sx, sy, 450, MAT_WALL, "Arena/Perimeter")

# ---- Cover (x, y, size x, size y, height) --------------------------------------------------------
TALL, LOW, CRATE = 300, 115, 125
cover = [
    # Player side
    ("S_Low_W", -900, -1200, 420, 40, LOW), ("S_Low_E", 900, -1200, 420, 40, LOW),
    ("S_Crate_C", 0, -1350, 130, 130, CRATE),
    ("S_Crate_W", -2450, -1100, 160, 160, CRATE), ("S_Crate_E", 2450, -1100, 160, 160, CRATE),
    # Middle: a U-shaped bunker open to the south, flanked by two lanes
    ("M_Bunker_W", -420, 0, 40, 520, TALL), ("M_Bunker_E", 420, 0, 40, 520, TALL),
    ("M_Bunker_N", 0, 260, 880, 40, TALL),
    ("M_Low_W", -1150, 150, 320, 40, LOW), ("M_Low_E", 1150, 150, 320, 40, LOW),
    ("M_Crate_W", -750, -550, 130, 130, CRATE), ("M_Crate_E", 750, -550, 130, 130, CRATE),
    ("M_LaneWall_W", -1900, -250, 40, 700, TALL), ("M_LaneWall_E", 1900, -250, 40, 700, TALL),
    ("M_Pillar_W", -2500, 450, 110, 110, TALL), ("M_Pillar_E", 2500, 450, 110, 110, TALL),
    # Enemy side
    ("N_Wall_W", -950, 950, 520, 40, TALL), ("N_Wall_E", 950, 950, 520, 40, TALL),
    ("N_Low_C", 0, 1250, 420, 40, LOW),
    ("N_Low_W", -1800, 1100, 320, 40, LOW), ("N_Low_E", 1800, 1100, 320, 40, LOW),
    ("N_Crate_W", -320, 720, 130, 130, CRATE), ("N_Crate_E", 320, 720, 130, 130, CRATE),
    ("N_Pillar_W", -2600, 1500, 110, 110, TALL), ("N_Pillar_E", 2600, 1500, 110, 110, TALL),
]
for label, x, y, sx, sy, h in cover:
    block(label, x, y, sx, sy, h, MAT_WALL if h == TALL else MAT_COVER, "Arena/Cover")

# ---- Lighting -------------------------------------------------------------------------------------
sun = spawn(unreal.DirectionalLight, "Sun", unreal.Vector(0, 0, 1000), unreal.Rotator(0.0, -48.0, 35.0), "Lighting")
sun.light_component.set_editor_property("atmosphere_sun_light", True)
sun.light_component.set_intensity(8.0)
spawn(unreal.SkyAtmosphere, "SkyAtmosphere", unreal.Vector(0, 0, 0), folder="Lighting")
sky = spawn(unreal.SkyLight, "SkyLight", unreal.Vector(0, 0, 500), folder="Lighting")
sky.light_component.set_editor_property("real_time_capture", True)
sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
spawn(unreal.ExponentialHeightFog, "HeightFog", unreal.Vector(0, 0, -100), folder="Lighting")
spawn(unreal.VolumetricCloud, "Clouds", unreal.Vector(0, 0, 0), folder="Lighting")

# ---- Navigation -----------------------------------------------------------------------------------
nav = spawn(unreal.NavMeshBoundsVolume, "NavBounds", unreal.Vector(0, 0, 150), folder="Navigation")
nav.set_actor_scale3d(unreal.Vector(33.0, 23.0, 4.0))  # 200 cm brush -> 6600 x 4600 x 800

# The arena never changes at runtime: bake a static navmesh into the map so it is ready on the first frame.
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
for recast in [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]:
    recast.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.STATIC)

# ---- Gameplay -------------------------------------------------------------------------------------
spawn(unreal.PlayerStart, "PlayerStart", unreal.Vector(0, -1800, 100), unreal.Rotator(0.0, 0.0, 90.0))

enemy_class = unreal.load_class(None, "/Script/TacticalSquadAI.TSEnemyCharacter")
for index, (x, y) in enumerate([(-600, 1550), (0, 1700), (600, 1550), (-1600, 1450), (1600, 1450)]):
    spawn(enemy_class, "Enemy_{}".format(index + 1), unreal.Vector(x, y, 100), unreal.Rotator(0.0, 0.0, -90.0), "Squad")

levels.save_current_level()
unreal.log("Lvl_CombatArena built with {} cover pieces.".format(len(cover)))

# When launched with -ExecutePythonScript, close the editor once the map is saved.
if "-quit_after_build" in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.quit_editor()
