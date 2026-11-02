# Launcher (batch P): the project's main scene. It decides where the game's data is, then hands over to the runner (runner.tscn).
#
# Packaged exe, no arguments and no environment variables: the data folder is the exe's folder, else the current folder, the
# first valid one wins (valid = it holds .vp files, or Data\*.vp as the FS1 demo does). `--fs2-root <dir>` (or --fs2-root=<dir>)
# names the folder instead, and then only that one is tried. Game DLL: FS2game_<Layer>_64<r|d>.dll beside the exe, named by
# res://fs2_layer.txt (written by tools\godot_package.ps1; never a glob, D-20261101-105802). With no valid folder it
# shows a message naming the folders tried and exits with code 3 (4: the game DLL is missing). Nothing here crashes.
#
# The gate harness and developers keep the environment of runner.gd: FS2_CMD, FS2_DLL, FS2_ROOT. FS2_CMD set means a scripted
# run: it takes FS2_ROOT and FS2_DLL when set, else the folder and DLL the rules above find. Without FS2_CMD this is live play:
# the game's own `-window`, drawn through the host renderer, the game on a fiber (what runner.gd's FS2_GR_HOST, FS2_FIBER do).
#
# Window mode (live play only): fullscreen by default, the borderless kind at the display's own resolution (Godot's WINDOW_MODE_FULLSCREEN;
# it alt-tabs, shows the launcher's message box and suits the Steam Deck's 1280x800 panel and gamescope). The game keeps its aspect and the
# bars show black. The optional `-window` (or `--window`) on the exe's command line gives the 1152x648 window instead. Scripted runs
# (FS2_CMD set) are never touched: they keep whatever Godot's own flags (--windowed, --resolution) or its project default give.
# The values go to runner.gd through the process environment (OS.set_environment), so runner.gd needs no change.
extends Node

const EXIT_NO_DATA := 3
const EXIT_NO_DLL := 4
const TITLE := "FreeSpace 2"
const LAYER_FILE := "res://fs2_layer.txt"	# line 1 the layer, line 2 r or d (tools\godot_package.ps1)

# the game DLL of the layer that was packaged, from LAYER_FILE; "" when the file is missing (then the DLL check fails and names it)
func _dll_name() -> String:
	if not FileAccess.file_exists(LAYER_FILE):
		return ""
	var lines := FileAccess.get_file_as_string(LAYER_FILE).strip_edges().split("\n")
	var cfg := lines[1].strip_edges() if lines.size() > 1 else "r"
	return "FS2game_%s_64%s.dll" % [lines[0].strip_edges(), cfg]

func _ready() -> void:
	var exe_dir := OS.get_executable_path().get_base_dir()
	var tried: PackedStringArray = []
	var root := ""
	var explicit := _arg_root()
	if explicit == "":
		explicit = OS.get_environment("FS2_ROOT")
	if explicit != "":
		tried.append(explicit)
		if _has_data(explicit):
			root = explicit
	else:
		for dir in _default_candidates(exe_dir):
			if not tried.has(dir):
				tried.append(dir)
			if root == "" and _has_data(dir):
				root = dir
	if root == "":
		_fail(EXIT_NO_DATA, "No FreeSpace data folder found.\n\nFolders tried:\n  " + "\n  ".join(tried) +
			"\n\nPut this program in a folder that holds the game's .vp files (FreeSpace 2, FreeSpace 1 or a demo), or start it with\n  --fs2-root <folder>")
		return
	var dll := OS.get_environment("FS2_DLL")
	if dll == "":
		dll = exe_dir.path_join(_dll_name())
	if not FileAccess.file_exists(dll):
		_fail(EXIT_NO_DLL, "The game library is missing:\n  " + dll + "\n\nIts name comes from " + LAYER_FILE + " in the package. Keep that DLL next to the program.")
		return
	# the game's cfile splits paths at backslashes only (cfile_in_root_dir), so it must be given a native Windows path
	OS.set_environment("FS2_ROOT", root.replace("/", "\\").rstrip("\\"))
	OS.set_environment("FS2_DLL", dll.replace("/", "\\"))
	print("fs2 DLL: " + dll)
	if OS.get_environment("FS2_CMD") == "":
		OS.set_environment("FS2_CMD", "-window")	# the game's flag; the host renderer ignores it, the window mode is set below
		OS.set_environment("FS2_GR_HOST", "1")
		OS.set_environment("FS2_FIBER", "1")
		_set_window_mode(_has_window_switch())
	var runner: PackedScene = load("res://runner.tscn")
	add_child(runner.instantiate())

func _has_window_switch() -> bool:
	for a in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		if a == "-window" or a == "--window":
			return true
	return false

# Fullscreen unless windowed. A headless Godot has no window to set. The log line is what the launch tests read.
func _set_window_mode(windowed: bool) -> void:
	if DisplayServer.get_name() == "headless":
		return
	if not windowed:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
	print("launch: window mode requested ", "windowed" if windowed else "fullscreen")
	_report_window_mode.call_deferred()

func _report_window_mode() -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	var m := DisplayServer.window_get_mode()
	print("launch: window mode is ", "fullscreen" if m == DisplayServer.WINDOW_MODE_FULLSCREEN else "exclusive fullscreen" if m == DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN else "windowed", ", size ", DisplayServer.window_get_size(), ", screen ", DisplayServer.screen_get_size())

# The folders to try when nothing names one: the exe's, then the current directory (as the OS gave it to the process).
func _default_candidates(exe_dir: String) -> PackedStringArray:
	var out: PackedStringArray = [exe_dir.simplify_path()]
	var cwd := _cwd()
	if cwd != "":
		out.append(cwd)
	return out

func _cwd() -> String:
	var d := DirAccess.open(".")
	if d == null:
		return ""
	return d.get_current_dir().simplify_path()

func _arg_root() -> String:
	var args := OS.get_cmdline_args() + OS.get_cmdline_user_args()
	for i in args.size():
		var a := args[i]
		if a == "--fs2-root" and i + 1 < args.size():
			return args[i + 1]
		if a.begins_with("--fs2-root="):
			return a.substr("--fs2-root=".length())
	return ""

# A data-set folder (DataSetInstall.cpp) counts too: fs2demo or fs1demo beside the exe, or its installer from which the game makes it.
func _has_data(dir: String) -> bool:
	if _has_vp(dir) or _has_vp(dir.path_join("Data")):
		return true
	for s in [["fs2demo", "fs2demo10.exe"], ["fs1demo", "fsdemo10.exe"]]:
		var sub: String = dir.path_join(s[0])
		if _has_vp(sub) or _has_vp(sub.path_join("Data")) or FileAccess.file_exists(dir.path_join(s[1])):
			return true
	return false

func _has_vp(dir: String) -> bool:
	var d := DirAccess.open(dir)
	if d == null:
		return false
	for f in d.get_files():
		if f.get_extension().to_lower() == "vp":
			return true
	return false

func _fail(code: int, text: String) -> void:
	printerr("launch: ", text)
	if DisplayServer.get_name() != "headless":
		OS.alert(text, TITLE)	# a native message box; scripted --headless runs only get the stderr text and the exit code
	get_tree().quit(code)
