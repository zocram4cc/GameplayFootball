extends SceneTree
## PES .ftex -> the matching Godot texture type, all layers decoded.
## Exit 0 pass, 1 fail, 77 skipped.

const SKIP := 77
const KIT := "Asset/model/character/uniform/texture/#windx11/u0767p1.ftex"
const PATTERN_ARRAY := "Asset/model/character/common/sourceimages/#windx11/uni_pattern.ftex"
const PATTERN_LAYERS := 18   # pes-re-ftex.md 3.2
const PATTERN_SIZE := 256
const WRINKLE_ARRAY := "Asset/model/character/common/sourceimages/#windx11/shirts_normal.ftex"
const CUBE := "Asset/model/character/common/sourceimages/#windx11/eye_catch_cbm.ftex"
const CUBE_FACES := 6

var failures := 0


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		printerr("FAIL: ", what)


func _init() -> void:
	var root := OS.get_environment("PES21_ROOT")
	if root.is_empty():
		print("SKIP: PES21_ROOT not set")
		quit(SKIP)
		return
	var library := PesLibrary.new()
	check(library.open(root) == OK, "library opens")

	var kit = library.load_texture(KIT)
	check(kit is ImageTexture, "a 2D ftex becomes an ImageTexture")

	var pattern = library.load_texture(PATTERN_ARRAY)
	check(pattern is Texture2DArray, "an array ftex becomes a Texture2DArray")
	if pattern is Texture2DArray:
		check(pattern.get_layers() == PATTERN_LAYERS, "uni_pattern has %d layers (%d)" % [PATTERN_LAYERS, pattern.get_layers()])
		check(pattern.get_width() == PATTERN_SIZE, "uni_pattern is %d wide" % PATTERN_SIZE)

	# Normal arrays store a partial mip chain (shirts_normal: 10 of 12 levels);
	# without mipmaps the wrinkle normals alias into moire on screen.
	var wrinkles = library.load_texture(WRINKLE_ARRAY)
	check(wrinkles is Texture2DArray and wrinkles.has_mipmaps(), "shirts_normal keeps mipmaps")
	check(pattern is Texture2DArray and pattern.has_mipmaps(), "uni_pattern keeps mipmaps")

	var cube = library.load_texture(CUBE)
	check(cube is Cubemap, "a cube ftex becomes a Cubemap")
	if cube is Cubemap:
		check(cube.get_layers() == CUBE_FACES, "cube has 6 faces")

	check(library.load_texture("no/such/texture.ftex") == null, "missing texture is null")
	quit(1 if failures > 0 else 0)
