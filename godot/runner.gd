# Headless runner (G2): runs fs2game.dll inside Godot and quits with the game's exit code.
# Input comes from the environment (no quoting trouble with paths that hold spaces):
#   FS2_DLL   path of FS2game64r.dll or FS2game64d.dll
#   FS2_ROOT  the game folder (cfile root, current directory)
#   FS2_CMD   the game's command line
#   FS2_CMD2  optional: a second run in the same process after the first passed (the restart check)
#   FS2_FATAL_RETURN  optional, 1: the first run must end in a fatal Error() handed back to Godot (P1; force one with
#                     FS2_FORCE_ERROR_FRAME=<n>); the process stays up and FS2_CMD2 then runs. Needs FS2_CMD2.
#   FS2_FRAMES  optional: game frames per Godot process call (default 8)
extends Node

var game
var cmds: Array = []
var expect_fatal := false

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
	game.fatal.connect(_on_fatal)
	expect_fatal = OS.get_environment("FS2_FATAL_RETURN") == "1"
	if expect_fatal:
		if cmds.size() < 2:
			printerr("runner: FS2_FATAL_RETURN needs FS2_CMD2")
			get_tree().quit(2)
			return
		game.fatal_return = true
	if OS.get_environment("FS2_FRAMES") != "":
		game.frames_per_process = int(OS.get_environment("FS2_FRAMES"))
	print("runner: Godot ", Engine.get_version_info().string)
	if not game.start(dll, root, cmds[0]):
		get_tree().quit(1)

func _on_fatal(message: String) -> void:
	print("runner: fatal error handed back to Godot: ", message)
	if not expect_fatal:
		get_tree().quit(1)
		return
	print("runner: Godot still alive after the fatal error, starting the second run")
	expect_fatal = false
	game.fatal_return = false
	cmds.remove_at(0)
	call_deferred("_restart")

func _on_finished(code: int) -> void:
	if expect_fatal:
		printerr("runner: expected a fatal error in the first run, the game finished with ", code)
		get_tree().quit(1)
		return
	print("runner: game finished, exit code ", code, ", frames ", game.get_frame_count())
	if code == 0 and cmds.size() > 1:
		cmds.remove_at(0)
		call_deferred("_restart")
		return
	get_tree().quit(code)

func _restart() -> void:
	if not game.start(OS.get_environment("FS2_DLL"), OS.get_environment("FS2_ROOT"), cmds[0]):
		get_tree().quit(1)
