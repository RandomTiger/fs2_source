# Headless runner (G2): runs fs2game.dll inside Godot and quits with the game's exit code.
# Input comes from the environment (no quoting trouble with paths that hold spaces):
#   FS2_DLL   path of FS2game64r.dll or FS2game64d.dll
#   FS2_ROOT  the game folder (cfile root, current directory)
#   FS2_CMD   the game's command line
#   FS2_CMD2  optional: a second run in the same process after the first passed (the restart check)
#   FS2_FATAL_RETURN  optional, 1: the first run must end in a fatal Error() handed back to Godot (P1; force one with
#                     FS2_FORCE_ERROR_FRAME=<n>); the process stays up and FS2_CMD2 then runs. Needs FS2_CMD2.
#   FS2_FIBER  optional: 1 runs the game on a fiber (G3), stress also yields at every os_poll
#   FS2_GR_HOST  optional: 1 draws the game through an Fs2Screen (the host renderer, GR_HOST) instead of the null renderer; under
#               godot --headless the game still takes the "no device" side (D7), so traces equal the null renderer's (gate B)
#   FS2_SHOT   optional: path of a PNG; the window is saved there once the game has run FS2_SHOT_FRAME resumes (default 300), then Godot quits (R1-5)
#   FS2_FRAMES  optional: game frames per Godot process call (default 8)
#   FS2_INJECT  optional: 1 feeds synthetic keyboard and mouse events into Godot's input once the game has run a while (G4 proof that live
#               input reaches the game; use with -record, then count the key and mouse entries in <rec>.record.log)
extends Node

var game
var cmds: Array = []
var expect_fatal := false
# smoke test (G3): FS2_SMOKE=1 in a windowed Godot run prints, once a second, how long Godot's own frames take while the game runs,
# and every window event it gets, so the log shows the window keeps processing events during a mission
var smoke := false
var smoke_label: Label
var smoke_last := 0
var smoke_max_gap := 0
var smoke_calls := 0
var smoke_second := 0
var inject := false
var shot_path := OS.get_environment("FS2_SHOT")
var shot_frame := int(OS.get_environment("FS2_SHOT_FRAME")) if OS.get_environment("FS2_SHOT_FRAME") != "" else 300
var inject_step := 0
var inject_next := 600

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
	if OS.get_environment("FS2_GR_HOST") == "1":
		var scr: Node = ClassDB.instantiate("Fs2Screen")
		add_child(scr)
		game.set_screen(scr)
	smoke = OS.get_environment("FS2_SMOKE") == "1"
	inject = OS.get_environment("FS2_INJECT") == "1"
	if smoke or inject:
		get_tree().auto_accept_quit = false	# G4: a window close request goes to the game (it posts quit-game) instead of killing Godot
	if smoke:
		smoke_label = Label.new()
		smoke_label.position = Vector2(20, 20)
		add_child(smoke_label)
		get_window().size_changed.connect(func(): print("smoke: window size_changed ", get_window().size, " at ", Time.get_ticks_msec(), " ms"))
		get_window().focus_entered.connect(func(): print("smoke: window focus_entered at ", Time.get_ticks_msec(), " ms"))
		get_window().focus_exited.connect(func(): print("smoke: window focus_exited at ", Time.get_ticks_msec(), " ms"))
	game.finished.connect(_on_finished)
	game.fatal.connect(_on_fatal)
	expect_fatal = OS.get_environment("FS2_FATAL_RETURN") == "1"
	if expect_fatal:
		if cmds.size() < 2:
			printerr("runner: FS2_FATAL_RETURN needs FS2_CMD2")
			get_tree().quit(2)
			return
		game.fatal_return = true
	var fib := OS.get_environment("FS2_FIBER").to_lower()
	if fib == "stress":
		game.fiber_mode = 2
	elif fib != "" and fib != "0":
		game.fiber_mode = 1
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

func _inject_tick() -> void:
	# one synthetic event group every 120 game resumes, from game frame 600 on (the mission is running by then)
	if game.get_frame_count() < inject_next or inject_step >= 8:
		return
	inject_next += 120
	var vp := get_viewport().get_visible_rect().size
	match inject_step:
		0, 1, 2:
			var k := InputEventKey.new()
			k.physical_keycode = [KEY_A, KEY_SPACE, KEY_F5][inject_step]
			k.keycode = k.physical_keycode
			k.pressed = true
			Input.parse_input_event(k)
			var u := k.duplicate()
			u.pressed = false
			Input.parse_input_event(u)
		3, 4, 5:
			var m := InputEventMouseMotion.new()
			m.position = vp * Vector2([0.25, 0.5, 0.75][inject_step - 3], 0.5)
			m.global_position = m.position
			Input.parse_input_event(m)
		6:
			var b := InputEventMouseButton.new()
			b.button_index = MOUSE_BUTTON_LEFT
			b.position = vp * 0.5
			b.pressed = true
			Input.parse_input_event(b)
			var bu := b.duplicate()
			bu.pressed = false
			Input.parse_input_event(bu)
		7:
			var b2 := InputEventMouseButton.new()
			b2.button_index = MOUSE_BUTTON_RIGHT
			b2.position = vp * 0.5
			b2.pressed = true
			Input.parse_input_event(b2)
			var b2u := b2.duplicate()
			b2u.pressed = false
			Input.parse_input_event(b2u)
	print("inject: step ", inject_step, " at game frame ", game.get_frame_count())
	inject_step += 1

func _process(_delta: float) -> void:
	if shot_path != "" and game.get_frame_count() >= shot_frame:
		var img := get_viewport().get_texture().get_image()
		print("runner: screenshot ", shot_path, " ", img.get_size(), " save=", img.save_png(shot_path))
		get_tree().quit(0)
		return
	if inject:
		_inject_tick()
	if not smoke:
		return
	var now := Time.get_ticks_msec()
	if smoke_last != 0:
		smoke_max_gap = max(smoke_max_gap, now - smoke_last)
	smoke_last = now
	smoke_calls += 1
	smoke_label.text = "FS2 smoke: game frames %d, Godot frames %d" % [game.get_frame_count(), smoke_calls]
	if now / 1000 != smoke_second:
		smoke_second = now / 1000
		print("smoke: t=", now, " ms, godot frames=", smoke_calls, ", game resumes=", game.get_frame_count(), ", max gap between Godot frames=", smoke_max_gap, " ms")
		smoke_max_gap = 0

func _input(event: InputEvent) -> void:
	if smoke and (event is InputEventMouseMotion or event is InputEventKey or event is InputEventMouseButton):
		print("smoke: input event ", event.as_text(), " at ", Time.get_ticks_msec(), " ms")
