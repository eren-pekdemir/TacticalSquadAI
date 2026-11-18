"""World-aligned ground and concrete materials for the showcase map (no UV stretching on scaled cubes)."""
import unreal

MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
FOLDER = "/Game/Showcase/Materials"


def sampler_for(texture):
    if texture.get_editor_property("compression_settings") == unreal.TextureCompressionSettings.TC_NORMALMAP:
        return unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    if texture.get_editor_property("srgb"):
        return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    if texture.get_editor_property("compression_settings") == unreal.TextureCompressionSettings.TC_MASKS:
        return unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
    return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def new_material(name):
    """Create the material, or empty an existing one in place so level references stay intact."""
    path = "{}/{}".format(FOLDER, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.load_asset(path)
        MEL.delete_all_material_expressions(material)
        return material
    return TOOLS.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def expr(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


def scalar(material, value, x, y):
    node = expr(material, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", value)
    return node


def planar_uv(material, axes, tile, x, y):
    """World position projected on two axes ("RG" = XY, "RB" = XZ, "GB" = YZ) divided by tile size."""
    world = expr(material, unreal.MaterialExpressionWorldPosition, x - 400, y)
    mask = expr(material, unreal.MaterialExpressionComponentMask, x - 200, y)
    mask.set_editor_property("r", "R" in axes)
    mask.set_editor_property("g", "G" in axes)
    mask.set_editor_property("b", "B" in axes)
    MEL.connect_material_expressions(world, "", mask, "")
    divide = expr(material, unreal.MaterialExpressionDivide, x, y)
    divide.set_editor_property("const_b", tile)
    MEL.connect_material_expressions(mask, "", divide, "A")
    return divide


def sample(material, texture, uv, x, y):
    node = expr(material, unreal.MaterialExpressionTextureSample, x, y)
    node.set_editor_property("texture", texture)
    node.set_editor_property("sampler_type", sampler_for(texture))
    MEL.connect_material_expressions(uv, "", node, "UVs")
    return node


def tint(material, source, color, x, y):
    constant = expr(material, unreal.MaterialExpressionConstant3Vector, x - 200, y + 120)
    constant.set_editor_property("constant", color)
    multiply = expr(material, unreal.MaterialExpressionMultiply, x, y)
    MEL.connect_material_expressions(source, "RGB", multiply, "A")
    MEL.connect_material_expressions(constant, "", multiply, "B")
    return multiply


def load(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError("Missing texture " + path)
    return asset


# ---- Ground: rocky mud, top-down projection ---------------------------------------------------------
TX = "/Game/Construction_VOL1/Textures/Tileable/"
ground = new_material("M_TS_Ground")
uv = planar_uv(ground, "RG", 350.0, -900, 0)
albedo = sample(ground, load(TX + "TX_RockyMud_01_ALB"), uv, -500, -200)
normal = sample(ground, load(TX + "TX_RockyMud_01_NRM"), uv, -500, 100)
masks = sample(ground, load(TX + "TX_RockyMud_01_RHA"), uv, -500, 400)
MEL.connect_material_property(tint(ground, albedo, unreal.LinearColor(0.82, 0.78, 0.72, 1.0), -200, -200), "", unreal.MaterialProperty.MP_BASE_COLOR)
MEL.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
MEL.connect_material_property(masks, "R", unreal.MaterialProperty.MP_ROUGHNESS)
MEL.connect_material_property(masks, "B", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
MEL.recompile_material(ground)

# ---- Concrete: box projection picked by the surface normal ------------------------------------------
concrete = new_material("M_TS_Concrete")
asphalt = load("/Game/Promenta/Models/Street/Asphaltroad2/asphalt_01_diff_1k")
asphalt_n = load("/Game/Promenta/Models/Street/Asphaltroad2/asphalt_01_normal_1k")
uv_xy = planar_uv(concrete, "RG", 250.0, -1300, -300)
uv_xz = planar_uv(concrete, "RB", 250.0, -1300, 0)
uv_yz = planar_uv(concrete, "GB", 250.0, -1300, 300)
normal_ws = expr(concrete, unreal.MaterialExpressionVertexNormalWS, -1300, 600)
abs_n = expr(concrete, unreal.MaterialExpressionAbs, -1100, 600)
MEL.connect_material_expressions(normal_ws, "", abs_n, "")
round_n = expr(concrete, unreal.MaterialExpressionRound, -950, 600)
MEL.connect_material_expressions(abs_n, "", round_n, "")
mask_x = expr(concrete, unreal.MaterialExpressionComponentMask, -800, 550)
mask_x.set_editor_property("r", True)
mask_y = expr(concrete, unreal.MaterialExpressionComponentMask, -800, 650)
mask_y.set_editor_property("g", True)
MEL.connect_material_expressions(round_n, "", mask_x, "")
MEL.connect_material_expressions(round_n, "", mask_y, "")
lerp_a = expr(concrete, unreal.MaterialExpressionLinearInterpolate, -800, -100)
MEL.connect_material_expressions(uv_xy, "", lerp_a, "A")
MEL.connect_material_expressions(uv_xz, "", lerp_a, "B")
MEL.connect_material_expressions(mask_y, "", lerp_a, "Alpha")
lerp_b = expr(concrete, unreal.MaterialExpressionLinearInterpolate, -650, 0)
MEL.connect_material_expressions(lerp_a, "", lerp_b, "A")
MEL.connect_material_expressions(uv_yz, "", lerp_b, "B")
MEL.connect_material_expressions(mask_x, "", lerp_b, "Alpha")
c_albedo = sample(concrete, asphalt, lerp_b, -450, -200)
c_normal = sample(concrete, asphalt_n, lerp_b, -450, 100)
# Lighten and cool the asphalt grain so it reads as poured concrete.
desat = expr(concrete, unreal.MaterialExpressionDesaturation, -250, -200)
MEL.connect_material_expressions(c_albedo, "RGB", desat, "")
desat_frac = scalar(concrete, 0.7, -450, -50)
MEL.connect_material_expressions(desat_frac, "", desat, "Fraction")
lighten = expr(concrete, unreal.MaterialExpressionMultiply, -100, -200)
lighten.set_editor_property("const_b", 1.45)
MEL.connect_material_expressions(desat, "", lighten, "A")
# Grime: darker and damp near the ground, fading out over the first ~1.6 m.
world_z = expr(concrete, unreal.MaterialExpressionWorldPosition, -700, -500)
z_only = expr(concrete, unreal.MaterialExpressionComponentMask, -550, -500)
z_only.set_editor_property("b", True)
MEL.connect_material_expressions(world_z, "", z_only, "")
z_scaled = expr(concrete, unreal.MaterialExpressionDivide, -400, -500)
z_scaled.set_editor_property("const_b", 160.0)
MEL.connect_material_expressions(z_only, "", z_scaled, "A")
z_clamped = expr(concrete, unreal.MaterialExpressionSaturate, -250, -500)
MEL.connect_material_expressions(z_scaled, "", z_clamped, "")
grime = expr(concrete, unreal.MaterialExpressionLinearInterpolate, -100, -450)
grime.set_editor_property("const_a", 0.45)
grime.set_editor_property("const_b", 1.0)
MEL.connect_material_expressions(z_clamped, "", grime, "Alpha")
weathered = expr(concrete, unreal.MaterialExpressionMultiply, 50, -250)
MEL.connect_material_expressions(lighten, "", weathered, "A")
MEL.connect_material_expressions(grime, "", weathered, "B")
# Precast panel seams: thin dark lines every 3 m along the wall and every 1.5 m up it.
def seam(coordinate_mask, period, x, y):
    position = expr(concrete, unreal.MaterialExpressionWorldPosition, x - 600, y)
    channel = expr(concrete, unreal.MaterialExpressionComponentMask, x - 450, y)
    for axis in coordinate_mask:
        channel.set_editor_property(axis, True)
    MEL.connect_material_expressions(position, "", channel, "")
    if len(coordinate_mask) > 1:  # X + Y, so seams appear on walls facing either axis
        summed = expr(concrete, unreal.MaterialExpressionDotProduct, x - 300, y)
        ones = expr(concrete, unreal.MaterialExpressionConstant2Vector, x - 450, y + 80)
        ones.set_editor_property("r", 1.0)
        ones.set_editor_property("g", 1.0)
        MEL.connect_material_expressions(channel, "", summed, "A")
        MEL.connect_material_expressions(ones, "", summed, "B")
        channel = summed
    scaled = expr(concrete, unreal.MaterialExpressionDivide, x - 200, y)
    scaled.set_editor_property("const_b", period)
    MEL.connect_material_expressions(channel, "", scaled, "A")
    fraction = expr(concrete, unreal.MaterialExpressionFrac, x - 100, y)
    MEL.connect_material_expressions(scaled, "", fraction, "")
    line = expr(concrete, unreal.MaterialExpressionSmoothStep, x, y)
    line.set_editor_property("const_min", 0.975)
    line.set_editor_property("const_max", 0.995)
    MEL.connect_material_expressions(fraction, "", line, "Value")
    return line

vertical = seam("rg", 300.0, 200, -800)
horizontal = seam("b", 150.0, 200, -650)
seams = expr(concrete, unreal.MaterialExpressionMax, 350, -720)
MEL.connect_material_expressions(vertical, "", seams, "A")
MEL.connect_material_expressions(horizontal, "", seams, "B")
seam_shade = expr(concrete, unreal.MaterialExpressionLinearInterpolate, 450, -600)
seam_shade.set_editor_property("const_a", 1.0)
seam_shade.set_editor_property("const_b", 0.55)
MEL.connect_material_expressions(seams, "", seam_shade, "Alpha")
panelled = expr(concrete, unreal.MaterialExpressionMultiply, 600, -300)
MEL.connect_material_expressions(weathered, "", panelled, "A")
MEL.connect_material_expressions(seam_shade, "", panelled, "B")
MEL.connect_material_property(panelled, "", unreal.MaterialProperty.MP_BASE_COLOR)
MEL.connect_material_property(c_normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
MEL.connect_material_property(scalar(concrete, 0.85, -250, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)
MEL.recompile_material(concrete)

unreal.EditorAssetLibrary.save_directory(FOLDER)
print("materials built:", ground.get_path_name(), concrete.get_path_name())
