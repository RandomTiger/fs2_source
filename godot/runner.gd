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
#   FS2_SHOT_DIR optional: a folder; every FS2_SHOT_EVERY game resumes (default 300) a numbered PNG is saved there until the game finishes (menu tour)
#   FS2_FRAMES  optional: game frames per Godot process call (default 8)
#   FS2_STATS  optional: 1 measures the run (R1-feel) and prints one "feel:" summary at the end: Godot frame time spread, game and Godot frames per second,
#               input-to-draw latency (needs FS2_LAT_EVERY: a synthetic key every N Godot frames; stamps from Godot's _input to the frame_post_draw that follows the game taking it)
#   FS2_PACE  optional: 1 steps the game by the wall clock (120 game frames per second, whatever the Godot frame rate) and the game no longer sleeps to its frame cap; default 1 for live play (window, no -replay, no -autotest), 0 otherwise
#   FS2_SKIP_STALE  optional: 0 draws every game frame of a batch (the old behaviour); default 1 draws only the last
#   FS2_VSYNC  optional: 0 off, 1 on (default: the project setting); FS2_MAXFPS caps Godot's frame rate
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
var shot_dir := OS.get_environment("FS2_SHOT_DIR")
var shot_every := int(OS.get_environment("FS2_SHOT_EVERY")) if OS.get_environment("FS2_SHOT_EVERY") != "" else 300
var shot_next := 0
var shot_n := 0
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
	# live play (a window, no replay, no autotest) is stepped by the wall clock; FS2_PACE=0 or 1 overrides
	var pace_env := OS.get_environment("FS2_PACE")
	if pace_env == "1" or (pace_env == "" and DisplayServer.get_name() != "headless" and not (" -replay" in " " + cmds[0] or " -autotest" in " " + cmds[0])):
		game.set_pace_realtime(true)
	if OS.get_environment("FS2_SKIP_STALE") == "0":
		game.set_skip_stale_draws(false)
	if OS.get_environment("FS2_VSYNC") != "":
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED if OS.get_environment("FS2_VSYNC") == "1" else DisplayServer.VSYNC_DISABLED)
	if OS.get_environment("FS2_MAXFPS") != "":
		Engine.max_fps = int(OS.get_environment("FS2_MAXFPS"))
	if st_on:
		_stats_setup()
	print("runner: Godot ", Engine.get_version_info().string)
	if not game.start(dll, root, cmds[0]):
		get_tree().quit(1)
		return
	# performance pass (measurement only): FS2_PERF_DIR + FS2_PERF_TAG, optional FS2_PERF_SAMPLE_HZ (stack sampler, 0 = off)
	if OS.get_environment("FS2_PERF_DIR") != "":
		var hz := int(OS.get_environment("FS2_PERF_SAMPLE_HZ")) if OS.get_environment("FS2_PERF_SAMPLE_HZ") != "" else 0
		perf_on = game.perf_start(OS.get_environment("FS2_PERF_DIR"), OS.get_environment("FS2_PERF_TAG") if OS.get_environment("FS2_PERF_TAG") != "" else "run", hz)

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

var perf_on := false

func _on_finished(code: int) -> void:
	if perf_on:
		perf_on = false
		game.perf_finish()
	if st_on:
		_stats_report()
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
	if st_on:
		_stats_tick()
	if shot_dir != "" and game.get_frame_count() >= shot_next:
		shot_next = game.get_frame_count() + shot_every
		var im := get_viewport().get_texture().get_image()
		im.save_png("%s/shot_%03d.png" % [shot_dir, shot_n])
		shot_n += 1
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

# ---- R1-feel measurement (FS2_STATS=1) ----
var st_on := OS.get_environment("FS2_STATS") == "1"
var st_deltas: PackedFloat64Array = PackedFloat64Array()
var st_last_usec := 0
var st_first_usec := 0
var st_lat_every := int(OS.get_environment("FS2_LAT_EVERY")) if OS.get_environment("FS2_LAT_EVERY") != "" else 0
var st_lat_pending := false
var st_lat_in := 0
var st_lat_before := 0
var st_lat_next := 0
var st_lats: PackedFloat64Array = PackedFloat64Array()
var st_lat_queue: PackedFloat64Array = PackedFloat64Array()
var st_calls0 := 0
var st_frames0 := 0
var st_warm := int(OS.get_environment("FS2_STATS_WARM")) if OS.get_environment("FS2_STATS_WARM") != "" else 300	# Godot frames ignored at the start (loading, mission start)
var st_gms := PackedFloat64Array()
var st_sink_prev := 0

func _stats_setup() -> void:
	RenderingServer.frame_post_draw.connect(_stats_post_draw)

func _stats_post_draw() -> void:
	if st_lat_pending:
		var s: Dictionary = game.get_stats()
		if s["input_consumed"] > st_lat_before:
			var now := Time.get_ticks_usec()
			st_lats.append((now - st_lat_in) / 1000.0)
			st_lat_queue.append((s["input_consumed_usec"] - st_lat_in) / 1000.0)
			st_lat_pending = false

func _stats_tick() -> void:
	var now := Time.get_ticks_usec()
	if st_last_usec != 0:
		var n := st_deltas.size()
		if n == 0 and st_warm > 0:
			st_warm -= 1
		else:
			if n == 0:
				st_first_usec = now
				var s0: Dictionary = game.get_stats()
				st_calls0 = s0["calls"]
				st_frames0 = s0["game_frames"]
			st_deltas.append((now - st_last_usec) / 1000.0)
			st_gms.append(float(game.get_stats()["last_step_usec"]) / 1000.0)
			var sk: int = int(game.get_stats().get("sink_usec", 0))
			if st_gms[-1] > 9.0 and OS.get_environment("FS2_STATS_SLOW") == "1":
				print("slow: step %.1f ms sink %.1f ms godot_dt %.1f" % [st_gms[-1], (sk - st_sink_prev) / 1000.0, st_deltas[-1]])
			st_sink_prev = sk
	st_last_usec = now
	if st_lat_every > 0 and st_deltas.size() > 0 and not st_lat_pending and st_deltas.size() >= st_lat_next:
		# a key press the game does not act on (Scroll Lock), taken from the queue like any other
		var k := InputEventKey.new()
		k.physical_keycode = KEY_SCROLLLOCK
		k.keycode = KEY_SCROLLLOCK
		k.pressed = true
		st_lat_before = game.get_stats()["input_consumed"]
		st_lat_in = Time.get_ticks_usec()
		st_lat_pending = true
		Input.parse_input_event(k)
		var u := k.duplicate()
		u.pressed = false
		Input.parse_input_event(u)
		st_lat_before = st_lat_before	# the release counts too: the press is the first of two
		st_lat_next = st_deltas.size() + st_lat_every + (randi() % 7)

func _pct(a: PackedFloat64Array, p: float) -> float:
	if a.size() == 0:
		return 0.0
	var b := a.duplicate()
	b.sort()
	return b[min(b.size() - 1, int(p * b.size()))]

func _stats_report() -> void:
	if st_deltas.size() < 10:
		print("feel: too few frames (", st_deltas.size(), ")")
		return
	var sum := 0.0
	for d in st_deltas:
		sum += d
	var mean := sum / st_deltas.size()
	var v := 0.0
	for d in st_deltas:
		v += (d - mean) * (d - mean)
	var sd := sqrt(v / st_deltas.size())
	var secs := (Time.get_ticks_usec() - st_first_usec) / 1e6
	var s: Dictionary = game.get_stats()
	var gf: int = int(s["game_frames"]) - st_frames0
	var calls: int = int(s["calls"]) - st_calls0
	print("feel: godot_frames=%d secs=%.2f godot_fps=%.1f game_frames=%d game_fps=%.1f game_per_call=%.2f" % [calls, secs, calls / secs, gf, gf / secs, float(gf) / max(1, calls)])
	print("feel: godot_frame_ms mean=%.2f sd=%.2f p50=%.2f p95=%.2f p99=%.2f max=%.2f spread(p99-p50)=%.2f" % [mean, sd, _pct(st_deltas, 0.5), _pct(st_deltas, 0.95), _pct(st_deltas, 0.99), _pct(st_deltas, 1.0), _pct(st_deltas, 0.99) - _pct(st_deltas, 0.5)])
	print("feel: game_ms_per_call=%.2f (time inside the game DLL per Godot frame)" % [float(s["game_usec"]) / 1000.0 / max(1, int(s["calls"]))])
	print("feel: sink_ms_per_drawn_frame=%.2f drawn_frames=%d (of all game frames %d)" % [float(s.get("sink_usec", 0)) / 1000.0 / max(1, int(s.get("sink_frames", 1))), int(s.get("sink_frames", 0)), int(s["game_frames"])])
	print("feel: game_step_ms p50=%.2f p95=%.2f p99=%.2f max=%.2f" % [_pct(st_gms, 0.5), _pct(st_gms, 0.95), _pct(st_gms, 0.99), _pct(st_gms, 1.0)])
	if st_lats.size() > 0:
		print("feel: input_to_draw_ms n=%d mean=%.1f p50=%.1f p95=%.1f max=%.1f ; of which waiting for the game to take it: p50=%.1f" % [st_lats.size(), _mean(st_lats), _pct(st_lats, 0.5), _pct(st_lats, 0.95), _pct(st_lats, 1.0), _pct(st_lat_queue, 0.5)])

func _mean(a: PackedFloat64Array) -> float:
	var t := 0.0
	for x in a:
		t += x
	return t / max(1, a.size())
