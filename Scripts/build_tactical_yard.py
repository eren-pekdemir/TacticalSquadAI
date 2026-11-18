"""Builds /Game/Maps/Lvl_TacticalYard: the combat arena layout dressed as a construction yard at sunset.

Same gameplay footprint as Lvl_CombatArena (so AI tuning carries over), with real props as cover.
Needs the local-only showcase content (Construction_VOL1 pack, asphalt textures, Showcase materials).
Run inside the editor: build_showcase_materials.py first, then this script.
"""
import random
import unreal

random.seed(7)
MAP_PATH = "/Game/Maps/Lvl_TacticalYard"
MESHES = "/Game/Construction_VOL1/Meshes/"

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
MAT_GROUND = unreal.load_asset("/Game/Showcase/Materials/M_TS_Ground.M_TS_Ground")
MAT_CONCRETE = unreal.load_asset("/Game/Showcase/Materials/M_TS_Concrete.M_TS_Concrete")


def mesh(name):
    asset = unreal.load_asset(MESHES + name)
    if not asset:
        raise RuntimeError("Missing mesh " + name)
    return asset


def ensure_collision(static_mesh):
    """Cover and sight traces use simple collision; give props without any a box."""
    if unreal.EditorStaticMeshLibrary.get_simple_collision_count(static_mesh) == 0:
        unreal.EditorStaticMeshLibrary.add_simple_collisions(static_mesh, unreal.ScriptingCollisionShapeType.BOX)
        unreal.EditorAssetLibrary.save_loaded_asset(static_mesh)


def prop(name, x, y, yaw=0.0, folder="Props", collide=True, roll=0.0, pitch=0.0, z=0.0):
    """Spawn a mesh, then move it so its world bounds are centred on (x, y) and rest on z."""
    static_mesh = mesh(name)
    if collide:
        ensure_collision(static_mesh)
    actor = actors.spawn_actor_from_object(static_mesh, unreal.Vector(0, 0, 0), unreal.Rotator(roll, pitch, yaw))
    origin, extent = actor.get_actor_bounds(False)
    offset = unreal.Vector(x - origin.x, y - origin.y, z - (origin.z - extent.z))
    actor.set_actor_location(actor.get_actor_location() + offset, False, False)
    component = actor.static_mesh_component
    if not collide:
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_editor_property("can_ever_affect_navigation", False)
    actor.set_actor_label(name.replace("SM_", ""))
    actor.set_folder_path(folder)
    return actor


def block(label, x, y, sx, sy, h, material, folder, z0=0.0):
    actor = actors.spawn_actor_from_object(CUBE, unreal.Vector(x, y, z0 + h * 0.5), unreal.Rotator())
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


# ---- Level ----------------------------------------------------------------------------------------
if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
    levels.load_level(MAP_PATH)
    for existing in actors.get_all_level_actors():
        if not isinstance(existing, (unreal.WorldSettings, unreal.Brush)) or isinstance(existing, unreal.Volume):
            actors.destroy_actor(existing)
elif not levels.new_level(MAP_PATH):
    raise RuntimeError("Could not create " + MAP_PATH)

# ---- Ground and perimeter -------------------------------------------------------------------------
block("Ground", 0, 0, 9000, 7000, 20, MAT_GROUND, "Yard", z0=-20)
for label, x, y, sx, sy in [
    ("Wall_N", 0, 2200, 6400, 60), ("Wall_S", 0, -2200, 6400, 60),
    ("Wall_E", 3200, 0, 60, 4400), ("Wall_W", -3200, 0, 60, 4400),
]:
    block(label, x, y, sx, sy, 450, MAT_CONCRETE, "Yard/Perimeter")

# Site fence just inside the long walls (decoration only).
for x in range(-2950, 3000, 310):
    prop("SM_construction_fence_01a", x, 2120, 0, "Yard/Fence", collide=False)
    prop("SM_construction_fence_01a", x, -2120, 180, "Yard/Fence", collide=False)

# ---- Tall concrete cover (same footprint as the prototype) ---------------------------------------
TALL = 300
for label, x, y, sx, sy in [
    ("M_Bunker_W", -420, 0, 40, 520), ("M_Bunker_E", 420, 0, 40, 520), ("M_Bunker_N", 0, 260, 880, 40),
    ("M_LaneWall_W", -1900, -250, 40, 700), ("M_LaneWall_E", 1900, -250, 40, 700),
    ("N_Wall_W", -950, 950, 520, 40), ("N_Wall_E", 950, 950, 520, 40),
]:
    block(label, x, y, sx, sy, TALL, MAT_CONCRETE, "Cover/Concrete")

# ---- Low cover: rows of brick pallets (139 cm, tall enough to hide a torso) ----------------------
BRICKS = ["SM_BrickPaletteStack_01a", "SM_BrickPaletteStack_01b", "SM_BrickPaletteStack_01c", "SM_BrickPaletteStack_01d"]
for label, x, y, length, along_x in [
    ("S_Low_W", -900, -1200, 420, True), ("S_Low_E", 900, -1200, 420, True),
    ("M_Low_W", -1150, 150, 320, True), ("M_Low_E", 1150, 150, 320, True),
    ("N_Low_C", 0, 1250, 420, True), ("N_Low_W", -1800, 1100, 320, True), ("N_Low_E", 1800, 1100, 320, True),
]:
    count = max(2, round(length / 120))
    for index in range(count):
        offset = (index - (count - 1) * 0.5) * 120
        px, py = (x + offset, y) if along_x else (x, y + offset)
        prop(random.choice(BRICKS), px, py, random.uniform(-4, 4) + (0 if along_x else 90), "Cover/Bricks")

# ---- Barrel clusters where the prototype had crates ----------------------------------------------
for x, y in [(0, -1350), (-2450, -1100), (2450, -1100), (-750, -550), (750, -550), (-320, 720), (320, 720)]:
    for dx, dy in [(-45, -25), (45, -25), (0, 50)]:
        prop("SM_Barrel_01", x + dx, y + dy, random.uniform(0, 360), "Cover/Barrels")

# ---- Big concrete pipes standing upright as pillars, and lying in the lanes ----------------------
for x, y in [(-2500, 450), (2500, 450), (-2600, 1500), (2600, 1500)]:
    prop("SM_ConcretePipe_02", x, y, 0, "Cover/Pipes", roll=90)
for x, y, yaw in [(-2550, -300, 90), (2550, -300, 90)]:
    prop("SM_ConcretePipe_02", x, y, yaw, "Cover/Pipes")

# ---- Set dressing (no gameplay collision) ---------------------------------------------------------
DRESSING = [
    ("SM_Tarp_01", -1450, -700, 20), ("SM_Tarp_03", 1350, 600, -35), ("SM_Tarp_01", 600, 1700, 70),
    ("SM_woodenpalette_01", -2200, 1750, 10), ("SM_woodenpalette_01", -2050, 1760, 4), ("SM_woodenpalette_01", 2300, -1700, 40),
    ("SM_ConcreteBag_01a", -820, -1320, 15), ("SM_ConcreteBag_01a", -760, -1300, 60), ("SM_ConcreteBag_02a", 980, -1310, 0),
    ("SM_Spool_01a", 1500, -900, 0), ("SM_Spool_01a", -1500, 1300, 0), ("SM_PlasticBarrel_01a", 2050, 1650, 0),
    ("SM_PlasticBarrel_01a", 2120, 1700, 0), ("SM_Boxes_01a", -2700, -1700, 20), ("SM_Boxes_02a", -2600, -1780, 70),
    ("SM_Crate_01a", 2750, 1800, 15), ("SM_Crate_01a", 2650, 1700, 80), ("SM_Tarp_03", -300, -1700, 10),
]
for name, x, y, yaw in DRESSING:
    prop(name, x, y, yaw, "Dressing", collide=False)

# Traffic barriers and cones mark the lanes; barriers block movement but are too low to count as cover.
for x, y, yaw in [(-1450, -1500, 0), (-1270, -1500, 0), (1450, -1500, 0), (1270, -1500, 0), (0, 400, 0)]:
    prop("SM_Trafficbarrier_01", x, y, yaw + 90, "Dressing")
for i in range(6):
    prop("SM_ConstructionPylons_01a", -2300 + i * 120, -450, 0, "Dressing", collide=False)
    prop("SM_ConstructionPylons_01a", 1700 + i * 120, -450, 0, "Dressing", collide=False)

# ---- Sunset lighting ------------------------------------------------------------------------------
sun = spawn(unreal.DirectionalLight, "Sun", unreal.Vector(0, 0, 1000), unreal.Rotator(0.0, -11.0, 128.0), "Lighting")
light = sun.light_component
light.set_editor_property("atmosphere_sun_light", True)
light.set_intensity(7.0)
light.set_editor_property("use_temperature", True)
light.set_editor_property("temperature", 4300.0)
light.set_editor_property("light_source_angle", 1.2)
spawn(unreal.SkyAtmosphere, "SkyAtmosphere", unreal.Vector(0, 0, 0), folder="Lighting")
sky = spawn(unreal.SkyLight, "SkyLight", unreal.Vector(0, 0, 500), folder="Lighting")
sky.light_component.set_editor_property("real_time_capture", True)
sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
sky.light_component.set_intensity(1.2)
fog = spawn(unreal.ExponentialHeightFog, "HeightFog", unreal.Vector(0, 0, -50), folder="Lighting")
fog_component = fog.component
fog_component.set_editor_property("fog_density", 0.025)
fog_component.set_editor_property("fog_height_falloff", 0.35)
fog_component.set_editor_property("enable_volumetric_fog", True)
fog_component.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.9, 0.6, 0.4, 1.0))
spawn(unreal.VolumetricCloud, "Clouds", unreal.Vector(0, 0, 0), folder="Lighting")

post = spawn(unreal.PostProcessVolume, "PostProcess", unreal.Vector(0, 0, 0), folder="Lighting")
post.set_editor_property("unbound", True)
settings = post.get_editor_property("settings")
for flag, prop_name, value in [
    ("override_bloom_intensity", "bloom_intensity", 0.9),
    ("override_vignette_intensity", "vignette_intensity", 0.45),
    ("override_film_grain_intensity", "film_grain_intensity", 0.15),
    ("override_auto_exposure_bias", "auto_exposure_bias", 0.6),
    ("override_color_saturation", "color_saturation", unreal.Vector4(1.05, 1.0, 0.95, 1.0)),
    ("override_color_gain_highlights", "color_gain_highlights", unreal.Vector4(1.04, 1.0, 0.94, 1.0)),
]:
    settings.set_editor_property(flag, True)
    settings.set_editor_property(prop_name, value)
post.set_editor_property("settings", settings)

# ---- Gameplay -------------------------------------------------------------------------------------
nav = spawn(unreal.NavMeshBoundsVolume, "NavBounds", unreal.Vector(0, 0, 150), folder="Navigation")
nav.set_actor_scale3d(unreal.Vector(33.0, 23.0, 4.0))
spawn(unreal.PlayerStart, "PlayerStart", unreal.Vector(0, -1800, 100), unreal.Rotator(0.0, 0.0, 90.0))
enemy_class = unreal.load_class(None, "/Script/TacticalSquadAI.TSEnemyCharacter")
for index, (x, y) in enumerate([(-600, 1550), (0, 1700), (600, 1550), (-1600, 1450), (1600, 1450)]):
    spawn(enemy_class, "Enemy_{}".format(index + 1), unreal.Vector(x, y, 100), unreal.Rotator(0.0, 0.0, -90.0), "Squad")

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
for recast in [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]:
    recast.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.STATIC)

levels.save_current_level()
print("Lvl_TacticalYard built")
