# Headless runner (G2): runs fs2game.dll inside Godot and quits with the game's exit code.
# Input comes from the environment (no quoting trouble with paths that hold spaces):
#   FS2_DLL   path of FS2game64r.dll or FS2game64d.dll
#   FS2_ROOT  the game folder (cfile root, current directory)
#   FS2_CMD   the game's command line
#   FS2_CMD2  optional: a second run in the same process after the first passed (the restart check)
#   FS2_FRAMES  optional: game frames per Godot process call (default 8)
extends Node

var game
var cmds: Array = []

func _ready() -> void:
	var dll := OS.get_environment("FS2_DLL")
	var root := OS.get_environment("FS2_ROOT")
	cmds.append(OS.get_environment("FS2_CMD"))
	if OS.get_environment("FS2_CMD2") != "":
		cmds.append(OS.get_environment("FS2_CMD2"))
	if dll == "" or root == "" or cmds[0] == "":
		printerr("runner: set FS2_DLL, FS2_ROOT and FS2_CMD")
		get_tree().quit(2)
		return
	game = ClassDB.instantiate("Fs2Game")
	if game == null:
		printerr("runner: the Fs2Game class is not registered (extension not loaded)")
		get_tree().quit(2)
		return
	add_child(game)
	game.finished.connect(_on_finished)
	if OS.get_environment("FS2_FRAMES") != "":
		game.frames_per_process = int(OS.get_environment("FS2_FRAMES"))
	print("runner: Godot ", Engine.get_version_info().string)
	if not game.start(dll, root, cmds[0]):
		get_tree().quit(1)

func _on_finished(code: int) -> void:
	print("runner: game finished, exit code ", code, ", frames ", game.get_frame_count())
	if code == 0 and cmds.size() > 1:
		cmds.remove_at(0)
		call_deferred("_restart")
		return
	get_tree().quit(code)

func _restart() -> void:
	if not game.start(OS.get_environment("FS2_DLL"), OS.get_environment("FS2_ROOT"), cmds[0]):
		get_tree().quit(1)
