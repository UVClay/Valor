extends Node
## The PVS on main.gd's world mesh (formats.md, The render tree), as tsp_walk applies it: from the
## camera's leaf, every leaf's visibility (ValorAssets.pvs_visibility, each tick), as a texture the
## world shader reads by each face's leaf id. The ids go into the world mesh as CUSTOM0 (a copy of
## main.gd's mesh with that one array added, rebuilt when main.gd builds a new world); a mesh whose
## faces don't match the leaves face for face is left alone (engine/README.md, Coordination).

const NAMES := ["opaque", "semi0", "semi1", "semi2", "semi3"]
const WIDTH := 256

var p
var leaves := 0
var mesh_done: Mesh
var image: Image
var texture: ImageTexture
var on := false
var last := PackedByteArray()
var shown := false


func setup(present) -> void:
	p = present
	leaves = p.assets.pvs_load()
	if leaves == 0:
		return
	image = Image.create_empty(WIDTH, (leaves + WIDTH - 1) / WIDTH, false, Image.FORMAT_R8)
	image.fill(Color.WHITE)
	texture = ImageTexture.create_from_image(image)


func on_tick() -> void:
	if leaves == 0:
		return
	var world: MeshInstance3D = p.main.get("world")
	if world == null or world.mesh == null:
		return
	if world.mesh != mesh_done:
		on = augment(world)
		shown = false
	if not on:
		return
	var cam: Camera3D = p.main.cam
	var e := cam.global_position * 128.0
	var vis: PackedByteArray = p.assets.pvs_visibility(Vector3(e.x, -e.y, -e.z))
	if p.args.has("fx-log"):
		print("Valor pvs: tick %d, %d of %d leaves drawn, camera %s" % [int(p.state.get("tick", 0)), vis.count(255), leaves, Vector3(e.x, -e.y, -e.z)])
	if p.args.has("pvs-none"):
		vis.fill(0)  # a test: nothing drawn
	if vis == last and not p.main.materials.is_empty() and shown:
		return  # the camera's leaf masks didn't change: the texture stands
	last = vis
	shown = true
	vis.resize(image.get_width() * image.get_height())
	image.set_data(image.get_width(), image.get_height(), false, Image.FORMAT_R8, vis)
	texture.update(image)
	for mat in p.main.materials.values():
		mat.set_shader_parameter("pvs_vis", texture)
		mat.set_shader_parameter("pvs_width", WIDTH)


## main.gd's world mesh again with each vertex's leaf id in CUSTOM0, its materials put back.
func augment(world: MeshInstance3D) -> bool:
	var src: ArrayMesh = world.mesh
	var out := ArrayMesh.new()
	for i in src.get_surface_count():
		var name := src.surface_get_name(i)
		var arrays := src.surface_get_arrays(i)
		var ids: PackedFloat32Array = p.assets.pvs_leaf_ids(NAMES.find(name))
		if ids.size() != (arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array).size():
			push_warning("Valor present: the world mesh's faces don't match the PVS leaves; no PVS")
			mesh_done = src
			for mat in p.main.materials.values():
				mat.set_shader_parameter("pvs_width", 0)
			return false
		arrays[Mesh.ARRAY_CUSTOM0] = ids
		out.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {},
			Mesh.ARRAY_CUSTOM_R_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT)
		out.surface_set_name(out.get_surface_count() - 1, name)
	world.mesh = out
	mesh_done = out
	p.main.apply_materials(world)
	return true
