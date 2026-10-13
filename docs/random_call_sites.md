# Random number call sites

Every place the game draws a random number, and which stream it draws from. Cosmetic effects now have their own random stream, so effects and rendering no longer shift gameplay randomness. FRED2 and commented-out code are left out.

## The two streams
- **Gameplay stream.** The C library `rand()`, seeded from the level seed (`srand(Game_level_seed)`). `myrand()` (SystemVars.cpp) wraps it and increments `Rand_count`, which the replay trace records. `frand`, `frand_range`, `rand_chance`, `timestamp_rand`, `vm_vec_rand_vec_quick` and `submodel_get_two_random_points` all draw from it through `myrand()`. Direct `rand()` calls draw from the same stream but are not counted.
- **Cosmetic stream.** `cosmetic_rand()` (SystemVars.cpp) uses the same generator as the MSVC `rand()` but keeps its own seed, set from the level seed in `game_level_init`. It is not counted and not traced. The cosmetic versions of the helpers are `cosmetic_frand`, `cosmetic_frand_range`, `cosmetic_timestamp_rand`, `vm_vec_rand_vec_quick_cosmetic` and `submodel_get_two_random_points_cosmetic`.
- **Not on a stream.** `static_rand()` / `static_randf()` / `static_randvec()` (StaticRand.cpp) look up a fixed table. The table is filled from `myrand()` once, at startup (`init_semirand`).

## What counts as cosmetic
A draw is cosmetic only if its result never reaches simulation state: object creation or lifetime, AI, physics, damage, weapons, or the timing of game events. That covers particles, sparks, the star field, nebula clouds, lights, model arcs, shield-hit effects, radar and HUD effects, screen shake, render flicker, fireball positions, sizes and rotation, and sound choice.

These look cosmetic but **stay on the gameplay stream**:
- **Nebula lightning** (`NebLightning.cpp`, `shipfx_do_lightning_frame`). Bolts apply EMP (`emp_apply`) from `nebl_render_all`, and only when the bolt has points on screen. Bolt shape and placement therefore decide gameplay.
- **Fireball type and next-fireball timers** (`debris_start_death_roll`, `ship_dying_frame`, `maybe_fireball_wipe`). Fireballs are objects. The type sets how long one lives and the timer sets when the next is created, and both change which object slots are in use.
- **Message choice** (persona, praise, lament, scream, rearm, all-alone, arrival message sender and delay). It picks only audio and text, but it is kept on the gameplay stream to be safe.
- **Debris.** Pieces collide and do damage, so all of `debris_create`, `create_subsys_debris`, `shipfx_blow_up_model` and the half-ship debris stay on the gameplay stream. The electrical arcs on debris are cosmetic.

## Counts
| Stream / class | Sites | Raw `rand()` |
|---|---|---|
| Gameplay | 192 | 22 |
| Cosmetic | 137 | 0 |
| Wrapper (helper definitions, and the seed) | 38 | 4 |
| Menu/UI (outside missions) | 16 | 6 |
| Multiplayer | 12 | 10 |
| Debug | 10 | 0 |
| Load | 1 | 0 |

The 22 raw `rand()` calls on the gameplay stream are still not counted by the trace. Changing them to `myrand()` would not change the sequence, only the count, but that is a separate change.

## Things to note
1. **Gameplay effects come from render code.** These must stay on the gameplay stream, and the port has to move them out of rendering:
   - `nebl_render_all` applies the lightning EMP only when the bolt is on screen.
   - `half_ship_render_ship_and_debris` creates debris objects while rendering.
2. **The camera changes how many cosmetic numbers are drawn.** Sparks are not emitted for the ship being viewed from (`Ship.cpp`, `obj != Viewer_obj`), and the star field draws every rendered frame. Both are now on the cosmetic stream.
3. **Menu, multiplayer and debug code** still use the gameplay stream. Missions reseed it at level start, so menu draws don't carry into a mission.

## Call sites

### Asteroid/Asteroid.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 521 | `asteroid_create` | Gameplay |  |  | `asp->check_for_wrap = timestamp_rand(0, ASTEROID_CHECK_WRAP_TIMESTAMP);` |
| 522 | `asteroid_create` | Gameplay |  |  | `asp->check_for_collide = timestamp_rand(0, ASTEROID_UPDATE_COLLIDE_TIMESTAMP);` |
| 536 | `asteroid_create` | Gameplay |  |  | `pos.x = asfieldp->min_bound.x + delta_bound.x * frand();` |
| 537 | `asteroid_create` | Gameplay |  |  | `pos.y = asfieldp->min_bound.y + delta_bound.y * frand();` |
| 538 | `asteroid_create` | Gameplay |  |  | `pos.z = asfieldp->min_bound.z + delta_bound.z * frand();` |
| 542 | `asteroid_create` | Gameplay |  |  | `angs.p = frand() * 2*PI;` |
| 543 | `asteroid_create` | Gameplay |  |  | `angs.b = frand() * 2*PI;` |
| 544 | `asteroid_create` | Gameplay |  |  | `angs.h = frand() * 2*PI;` |
| 589 | `asteroid_create` | Gameplay |  |  | `vm_vec_rand_vec_quick(&rotvel);` |
| 590 | `asteroid_create` | Gameplay |  |  | `vm_vec_scale(&rotvel, frand()/4.0f + 0.1f);` |
| 592 | `asteroid_create` | Gameplay |  |  | `vm_vec_rand_vec_quick(&objp->phys_info.vel);` |
| 604 | `asteroid_create` | Gameplay |  |  | `speed = asteroid_cap_speed(asteroid_type, asfieldp->speed*frand_range(0.5f + (float) Ga...` |
| 666 | `asteroid_sub_create` | Gameplay |  |  | `speed = asteroid_cap_speed(asteroid_type, (frand() + 2.0f) * parent_speed);` |
| 720 | `get_debris_from_same_group` | Gameplay | yes |  | `return group_base + ((group_offset + rand()%2 + 1) % 3);` |
| 824 | `asteroid_create_all` | Gameplay | yes |  | `int subtype = rand() % 3;` |
| 833 | `asteroid_create_all` | Gameplay | yes |  | `int rand_choice = rand() % max_weighted_range;` |
| 959 | `asteroid_aim_at_target` | Gameplay |  |  | `vm_vec_rand_vec_quick(&rand_vec);` |
| 967 | `asteroid_aim_at_target` | Gameplay |  |  | `speed = Asteroid_info[0].max_speed * (frand()/2.0f + 0.5f);` |
| 1000 | `maybe_throw_asteroid` | Gameplay | yes |  | `int subtype = rand() % 3;` |
| 1006 | `maybe_throw_asteroid` | Gameplay |  |  | `asteroid_aim_at_target(A, objp, ASTEROID_MIN_COLLIDE_TIME + frand() * 20.0f);` |
| 1417 | `asc_get_relvec` | Gameplay |  |  | `vm_vec_rand_vec_quick(&rand_vec);` |
| 1587 | `asteroid_hit` | Gameplay |  |  | `vm_vec_rand_vec_quick(&rand_vec);` |
| 1722 | `asteroid_maybe_break_up` | Gameplay |  |  | `while (frand() > 0.6f) {` |
| 1724 | `asteroid_maybe_break_up` | Gameplay |  |  | `vm_vec_rand_vec_quick(&rvec);` |

### CMeasure/CMeasure.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 418 | `cmeasure_create` | Gameplay |  |  | `arand = myrand();` |

### Debris/Debris.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 273 | `debris_start_death_roll` | Gameplay | yes | fireball type sets its lifetime, which decides when the fireball object exists | `int fireball_type = FIREBALL_EXPLOSION_LARGE1 + rand()%FIREBALL_NUM_LARGE_EXPLOSIONS;` |
| 521 | `debris_process_post` | Cosmetic |  | electrical arcs on debris | `db->next_fireball = cosmetic_timestamp_rand(db->arc_frequency,db->arc_frequency*2 );` |
| 526 | `debris_process_post` | Cosmetic |  | electrical arcs on debris | `int n, n_arcs = ((cosmetic_rand()>>5) % 3)+1;` |
| 529 | `debris_process_post` | Cosmetic |  | electrical arcs on debris | `submodel_get_two_random_points_cosmetic( db->model_num, db->submodel_num, &v1, &v2 );` |
| 530 | `debris_process_post` | Cosmetic |  | electrical arcs on debris | `submodel_get_two_random_points_cosmetic( db->model_num, db->submodel_num, &v3, &v4 );` |
| 535 | `debris_process_post` | Cosmetic |  | electrical arcs on debris | `int lifetime = (cosmetic_rand()%((b)-(a)+1))+(a);` |
| 606 | `debris_process_post` | Cosmetic |  | electrical arcs on debris | `int mr = cosmetic_rand();` |
| 609 | `debris_process_post` | Cosmetic |  | electrical arcs on debris | `submodel_get_two_random_points_cosmetic( db->model_num, db->submodel_num, &v1, &v2 );` |
| 702 | `debris_create` | Gameplay | yes | debris objects collide and do damage | `if (rand() < RAND_MAX/6)` |
| 703 | `debris_create` | Gameplay |  | debris objects collide and do damage | `db->lifeleft = 2.0f * ((float) myrand()/(float) RAND_MAX) + 0.5f;` |
| 707 | `debris_create` | Gameplay |  | debris objects collide and do damage | `db->lifeleft = (i2fl(myrand())/i2fl(RAND_MAX))*2.0f+0.1f;` |
| 723 | `debris_create` | Gameplay |  | debris objects collide and do damage | `db->next_distance_check = (myrand() % 2000) + 4*DEBRIS_DISTANCE_CHECK_TIME;` |
| 732 | `debris_create` | Gameplay |  | debris objects collide and do damage | `if ( myrand() < RAND_MAX*2/3 )` |
| 747 | `debris_create` | Gameplay |  | debris objects collide and do damage | `db->submodel_num = (myrand()>>4) % Debris_num_submodels;` |
| 754 | `debris_create` | Gameplay |  | debris objects collide and do damage | `db->next_fireball = timestamp_rand(500,2000);` |
| 799 | `debris_create` | Gameplay |  | debris objects collide and do damage | `scale = exp_force * i2fl((myrand()%20) + 10);` |
| 811 | `debris_create` | Gameplay |  | debris objects collide and do damage | `t = 1000*Objects[db->source_objnum].radius/3 + myrand()%(fl2i(1000*3*Objects[db->source...` |
| 823 | `debris_create` | Gameplay |  | debris objects collide and do damage | `scale = exp_force * i2fl((myrand()%20) + 10);` |
| 827 | `debris_create` | Gameplay |  | debris objects collide and do damage | `vm_vec_rand_vec_quick(&radial_vel);` |
| 884 | `debris_create` | Gameplay |  | debris objects collide and do damage | `scale = ( 6.0f + i2fl(myrand()%4) ) / radius;` |
| 886 | `debris_create` | Gameplay |  | debris objects collide and do damage | `vm_vec_rand_vec_quick(&rotvel);` |

### FREESPACE2/FreeSpace.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1403 | `game_level_init` | Wrapper |  | seeds the cosmetic stream each level | `cosmetic_srand( Game_level_seed );` |
| 2823 | `game_tst_frame` | Debug |  | tst debug overlay | `switch((int)frand_range(0.0f, 3.0)){` |
| 2859 | `game_tst_frame` | Debug |  | tst debug overlay | `tst_y = frand_range(0.0f, (float)gr_screen.max_h - h);` |
| 3178 | `apply_hud_shake` | Cosmetic |  | view shake only | `int r1 = cosmetic_rand();` |
| 3179 | `apply_hud_shake` | Cosmetic |  | view shake only | `int r2 = cosmetic_rand();` |
| 3187 | `apply_hud_shake` | Cosmetic |  | view shake only | `int r1 = cosmetic_rand();` |
| 3188 | `apply_hud_shake` | Cosmetic |  | view shake only | `int r2 = cosmetic_rand();` |
| 3197 | `apply_hud_shake` | Cosmetic |  | view shake only | `vm_vec_rand_vec_quick_cosmetic(&rand_vec);` |
| 3216 | `apply_hud_shake` | Cosmetic |  | view shake only | `int r1 = cosmetic_rand();` |
| 3217 | `apply_hud_shake` | Cosmetic |  | view shake only | `int r2 = cosmetic_rand();` |
| 4816 | `camera_move` | Cosmetic |  |  | `float tmp_angle = cosmetic_frand()*PI2;` |

### Fireball/FireBalls.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1069 | `fireball_create` | Cosmetic |  |  | `fb->orient = (cosmetic_rand()>>8) & 7;` |
| 1076 | `fireball_create` | Cosmetic |  |  | `fb->orient = (cosmetic_rand()>>8) % 360;` |

### Gamesnd/EventMusic.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 527 | `event_music_level_init` | Cosmetic |  | soundtrack choice, at load | `Current_soundtrack_num = cosmetic_rand()%Num_soundtracks;` |

### GlobalIncs/SystemVars.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 253 | `myrand` | Wrapper |  |  | `int myrand()` |
| 256 | `myrand` | Wrapper | yes |  | `rval = rand();` |
| 265 | `cosmetic_rand` | Wrapper |  | cosmetic stream | `int cosmetic_rand()` |
| 271 | `cosmetic_srand` | Wrapper |  | cosmetic stream | `void cosmetic_srand(int seed)` |

### Hud/HUDsquadmsg.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 2125 | `hud_squadmsg_call_reinforcement` | Gameplay |  |  | `delay = (int)(frand() * 3.0) + 3;` |
| 2143 | `hud_squadmsg_call_reinforcement` | Gameplay |  |  | `delay = (int)(frand() * 3.0) + 3;` |

### Hud/HUDtargetbox.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1867 | `hud_targetbox_static_maybe_blit` | Cosmetic |  |  | `Target_static_next = cosmetic_timestamp_rand(50, 750);` |

### Hud/HudArtillery.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 232 | `ssm_create` | Gameplay |  |  | `ssm->sinfo.delay_stamp[idx] = timestamp(200 + (int)frand_range(-199.0f, 1000.0f));` |

### Io/KeyControl.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 938 | `process_debug_keys` | Debug |  |  | `vm_vec_rand_vec_quick(&randvec);` |
| 1574 | `game_process_pause_key` | Menu/UI |  |  | `switch((int)frand_range(0.0f, 3.9f)){ \` |
| 1657 | `game_process_cheats` | Debug |  |  | `add.x += frand_range(-700.0f, 700.0f);` |
| 1658 | `game_process_cheats` | Debug |  |  | `add.y += frand_range(-700.0f, 700.0f);` |
| 1659 | `game_process_cheats` | Debug |  |  | `add.z += frand_range(-700.0f, 700.0f);` |
| 1730 | `game_process_cheats` | Debug |  |  | `add.x = frand_range(-1000.0f, 1000.0f);` |
| 1731 | `game_process_cheats` | Debug |  |  | `add.y = frand_range(-1000.0f, 1000.0f);` |
| 1732 | `game_process_cheats` | Debug |  |  | `add.z = frand_range(-1000.0f, 1000.0f);` |

### Math/Floating.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 148 | `frand` | Wrapper |  |  | `float frand()` |
| 151 | `frand` | Wrapper |  |  | `rval = ((float) myrand()) / (RAND_MAX + 1);` |
| 156 | `frand_range` | Wrapper |  |  | `float frand_range(float min, float max)` |
| 160 | `frand_range` | Wrapper |  |  | `rval = frand();` |
| 167 | `cosmetic_frand` | Wrapper |  | cosmetic stream | `float cosmetic_frand()` |
| 169 | `cosmetic_frand` | Wrapper |  | cosmetic stream | `return ((float) cosmetic_rand()) / (RAND_MAX + 1);` |
| 172 | `cosmetic_frand_range` | Wrapper |  | cosmetic stream | `float cosmetic_frand_range(float min, float max)` |
| 174 | `cosmetic_frand_range` | Wrapper |  | cosmetic stream | `return cosmetic_frand() * (max - min) + min;` |
| 180 | `rand_chance` | Wrapper |  |  | `int rand_chance(float frametime, float chance)` |
| 183 | `rand_chance` | Wrapper |  |  | `if (frand() < frametime)` |
| 186 | `rand_chance` | Wrapper |  |  | `return frand() < (frametime * (chance + 1.0f));` |

### Math/StaticRand.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 51 | `init_semirand` | Load |  | fills the static_rand table at startup | `Semirand[i] = (myrand() << 15) + myrand();` |

### Math/VecMat.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1531 | `vm_vec_rand_vec_quick` | Wrapper |  |  | `void vm_vec_rand_vec_quick(vector *rvec)` |
| 1533 | `vm_vec_rand_vec_quick` | Wrapper |  |  | `rvec->x = (frand() - 0.5f) * 2;` |
| 1534 | `vm_vec_rand_vec_quick` | Wrapper |  |  | `rvec->y = (frand() - 0.5f) * 2;` |
| 1535 | `vm_vec_rand_vec_quick` | Wrapper |  |  | `rvec->z = (frand() - 0.5f) * 2;` |
| 1544 | `vm_vec_rand_vec_quick_cosmetic` | Wrapper |  | cosmetic stream | `void vm_vec_rand_vec_quick_cosmetic(vector *rvec)` |
| 1546 | `vm_vec_rand_vec_quick_cosmetic` | Wrapper |  | cosmetic stream | `rvec->x = (cosmetic_frand() - 0.5f) * 2;` |
| 1547 | `vm_vec_rand_vec_quick_cosmetic` | Wrapper |  | cosmetic stream | `rvec->y = (cosmetic_frand() - 0.5f) * 2;` |
| 1548 | `vm_vec_rand_vec_quick_cosmetic` | Wrapper |  | cosmetic stream | `rvec->z = (cosmetic_frand() - 0.5f) * 2;` |
| 2789 | `vm_vec_random_cone` | Wrapper |  | caller: beam muzzle particles (C) | `vm_rot_point_around_line(&t1, in, fl_radian(frand_range(-max_angle, max_angle)), &vmd_z...` |
| 2792 | `vm_vec_random_cone` | Wrapper |  | caller: beam muzzle particles (C) | `vm_rot_point_around_line(&t2, &t1, fl_radian(frand_range(-max_angle, max_angle)), &vmd_...` |
| 2795 | `vm_vec_random_cone` | Wrapper |  | caller: beam muzzle particles (C) | `vm_rot_point_around_line(out, &t2, fl_radian(frand_range(-max_angle, max_angle)), &vmd_...` |
| 2805 | `vm_vec_random_in_circle` | Wrapper |  | callers: Beam aim, SSM, nebula lightning (all gameplay stream) | `vm_vec_scale_add(&temp, in, &orient->rvec, on_edge ? radius : frand_range(0.0f, radius));` |
| 2808 | `vm_vec_random_in_circle` | Wrapper |  | callers: Beam aim, SSM, nebula lightning (all gameplay stream) | `vm_rot_point_around_line(out, &temp, fl_radian(frand_range(0.0f, 359.0f)), in, &orient-...` |

### MenuUI/Credits.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 520 | `credits_init` | Menu/UI | yes |  | `Credits_artwork_index = rand() % NUM_IMAGES;` |

### MenuUI/MainHallMenu.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1439 | `main_hall_handle_misc_anims` | Menu/UI | yes |  | `(int)(((float)rand()/(float)RAND_MAX) * (float)(Main_hall->misc_anim_delay[idx][2] - Ma...` |
| 1748 | `main_hall_handle_random_intercom_sounds` | Menu/UI | yes |  | `Main_hall_next_intercom_sound_stamp = timestamp((int)(((float)rand()/(float)RAND_MAX) *` |
| 1776 | `main_hall_handle_random_intercom_sounds` | Menu/UI | yes |  | `Main_hall_next_intercom_sound_stamp = timestamp((int)(((float)rand()/(float)RAND_MAX) *` |

### MenuUI/PlayerMenu.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1496 | `player_tips_popup` | Menu/UI |  |  | `tip = (int)frand_range(0.0f, (float)Num_player_tips - 1.0f);` |

### MenuUI/fishtank.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 89 | `fish_generate` | Menu/UI |  |  | `f->left = frand_range(0.0f, 1.0f) < 0.5f ? 0 : 1;` |
| 93 | `fish_generate` | Menu/UI |  |  | `f->x = gr_screen.max_w + frand_range(0.0f, 50.0f);` |
| 95 | `fish_generate` | Menu/UI |  |  | `f->x = frand_range(0.0f, -50.0f) - FISH_ANIM_WIDTH;` |
| 97 | `fish_generate` | Menu/UI |  |  | `f->y = frand_range(-40.0f, (float)gr_screen.max_h + 40.0f);` |
| 101 | `fish_generate` | Menu/UI |  |  | `f->x_speed = frand_range(-1.0f, -15.0f);` |
| 103 | `fish_generate` | Menu/UI |  |  | `f->x_speed = frand_range(1.0f, 15.0f);` |
| 105 | `fish_generate` | Menu/UI |  |  | `f->y_speed = frand_range(0.0f, 1.0f) < 0.5f ? frand_range(1.0f, 4.0f) : frand_range(-1....` |
| 187 | `fishtank_start` | Menu/UI |  |  | `int count = (int)frand_range(1.0f, (float)(MAX_FISH - 1));` |

### Mission/MissionMessage.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1124 | `message_calc_anim_start_frame` | Cosmetic |  |  | `start_frame=cosmetic_rand()%num_frames_extra;` |
| 1694 | `message_get_persona` | Gameplay | yes | message choice; kept on gameplay stream to be safe | `i = (rand() % count);` |
| 2023 | `message_maybe_distort_text` | Cosmetic |  |  | `next_distort = 5+cosmetic_rand()%5;` |
| 2026 | `message_maybe_distort_text` | Cosmetic |  |  | `run = 3+cosmetic_rand()%5;` |
| 2034 | `message_maybe_distort_text` | Cosmetic |  |  | `next_distort = i + (5+cosmetic_rand()%5);` |
| 2043 | `message_maybe_distort_text` | Cosmetic |  |  | `Distort_num = cosmetic_rand()%MAX_DISTORT_PATTERNS;` |

### Mission/MissionParse.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1715 | `parse_create_object` | Cosmetic |  | sparks on ships that start damaged | `submodel_get_two_random_points_cosmetic(sip->modelnum, pm->detail[0], &v1, &v2);` |
| 2354 | `parse_wing_create_ships` | Gameplay |  |  | `time_to_arrive = wingp->wave_delay_min + (int)(frand() * (wingp->wave_delay_max - wingp...` |
| 3376 | `parse_asteroid_fields` | Gameplay |  |  | `vm_vec_rand_vec_quick(&Asteroid_field.vel);` |
| 4142 | `mission_set_arrival_location` | Gameplay |  |  | `vm_vec_rand_vec_quick(&rand_vec);` |
| 4156 | `mission_set_arrival_location` | Gameplay | yes |  | `r1 = rand() < RAND_MAX/2 ? -1 : 1;` |
| 4157 | `mission_set_arrival_location` | Gameplay | yes |  | `r2 = rand() < RAND_MAX/2 ? -1 : 1;` |
| 4378 | `mission_eval_arrivals` | Gameplay |  | arrival message sender and delay; kept on gameplay stream to be safe | `use_terran = ((frand() - 0.75) > 0.0f)?1:0;` |
| 4500 | `mission_eval_arrivals` | Gameplay |  | arrival message sender and delay; kept on gameplay stream to be safe | `Arrival_message_delay_timestamp_m[multi_team_filter] = timestamp_rand(ARRIVAL_MESSAGE_D...` |
| 4541 | `mission_eval_arrivals` | Gameplay |  | arrival message sender and delay; kept on gameplay stream to be safe | `Arrival_message_delay_timestamp = timestamp_rand(ARRIVAL_MESSAGE_DELAY_MIN, ARRIVAL_MES...` |
| 4828 | `get_warp_in_pos` | Gameplay |  |  | `rand_val = frand();` |
| 4962 | `mission_warp_in_support_ship` | Gameplay |  |  | `pobj->arrival_delay = timestamp_rand(WARP_IN_TIME_MIN, WARP_IN_TIME_MAX);` |

### Model/ModelInterp.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1312 | `interp_render_arc_segment` | Cosmetic |  |  | `tmp.x += (cosmetic_frand()-0.5f)*d*scaler;` |
| 1313 | `interp_render_arc_segment` | Cosmetic |  |  | `tmp.y += (cosmetic_frand()-0.5f)*d*scaler;` |
| 1314 | `interp_render_arc_segment` | Cosmetic |  |  | `tmp.z += (cosmetic_frand()-0.5f)*d*scaler;` |
| 1350 | `interp_render_lightning` | Cosmetic |  |  | `if ( (cosmetic_rand()>>4) & 1 )` |
| 1359 | `interp_render_lightning` | Cosmetic |  |  | `if ( (cosmetic_rand()>>4) & 1 )` |
| 2977 | `submodel_get_two_random_points` | Wrapper |  | callers: Beam aim, shipfx_blow_up_model debris | `void submodel_get_two_random_points(int model_num, int submodel_num, vector *v1, vector...` |
| 2981 | `submodel_get_two_random_points` | Wrapper |  | callers: Beam aim, shipfx_blow_up_model debris | `int vn1 = (myrand()>>5) % nv;` |
| 2982 | `submodel_get_two_random_points` | Wrapper |  | callers: Beam aim, shipfx_blow_up_model debris | `int vn2 = (myrand()>>5) % nv;` |
| 2996 | `submodel_get_two_random_points_cosmetic` | Wrapper |  | cosmetic stream | `void submodel_get_two_random_points_cosmetic(int model_num, int submodel_num, vector *v...` |
| 3000 | `submodel_get_two_random_points_cosmetic` | Wrapper |  | cosmetic stream | `int vn1 = (cosmetic_rand()>>5) % nv;` |
| 3001 | `submodel_get_two_random_points_cosmetic` | Wrapper |  | cosmetic stream | `int vn2 = (cosmetic_rand()>>5) % nv;` |

### Nebula/Neb.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 814 | `neb2_gen_slice` | Cosmetic |  |  | `v->x = h_incw + (ws * (float)src) + cosmetic_frand_range(-Nd->wj, Nd->wj);` |
| 815 | `neb2_gen_slice` | Cosmetic |  |  | `v->y = h_inch + (hs * (float)idx1) + cosmetic_frand_range(-Nd->hj, Nd->hj);` |
| 816 | `neb2_gen_slice` | Cosmetic |  |  | `v->z = h_incd + (ds * (float)idx2) + cosmetic_frand_range(-Nd->dj, Nd->dj);` |
| 824 | `neb2_gen_slice` | Cosmetic |  |  | `Neb2_cubes[src][idx1][idx2].rot_speed = cosmetic_frand_range(-max_rotation, max_rotation);` |
| 834 | `neb2_gen_slice` | Cosmetic |  |  | `v->x = h_incw + (ws * (float)idx1) + cosmetic_frand_range(-Nd->wj, Nd->wj);` |
| 835 | `neb2_gen_slice` | Cosmetic |  |  | `v->y = h_inch + (hs * (float)src) + cosmetic_frand_range(-Nd->hj, Nd->hj);` |
| 836 | `neb2_gen_slice` | Cosmetic |  |  | `v->z = h_incd + (ds * (float)idx2) + cosmetic_frand_range(-Nd->dj, Nd->dj);` |
| 844 | `neb2_gen_slice` | Cosmetic |  |  | `Neb2_cubes[idx1][src][idx2].rot_speed = cosmetic_frand_range(-max_rotation, max_rotation);` |
| 854 | `neb2_gen_slice` | Cosmetic |  |  | `v->x = h_incw + (ws * (float)idx1) + cosmetic_frand_range(-Nd->wj, Nd->wj);` |
| 855 | `neb2_gen_slice` | Cosmetic |  |  | `v->y = h_inch + (hs * (float)idx2) + cosmetic_frand_range(-Nd->hj, Nd->hj);` |
| 856 | `neb2_gen_slice` | Cosmetic |  |  | `v->z = h_incd + (ds * (float)src) + cosmetic_frand_range(-Nd->dj, Nd->dj);` |
| 864 | `neb2_gen_slice` | Cosmetic |  |  | `Neb2_cubes[idx1][idx2][src].rot_speed = cosmetic_frand_range(-max_rotation, max_rotation);` |
| 1382 | `neb2_get_bitmap` | Cosmetic |  |  | `count = (int)cosmetic_frand_range(1.0f, 5.0f);` |
| 1761 | `neb2_add_inner` | Cosmetic |  |  | `vm_vec_copy_scale(&pt, &orient->fvec, cosmetic_frand_range(neb->magic_num, neb->inner_r...` |
| 1764 | `neb2_add_inner` | Cosmetic |  |  | `vm_rot_point_around_line(&pt2, &pt, fl_radian(cosmetic_frand_range(-ang, ang)), &vmd_ze...` |
| 1767 | `neb2_add_inner` | Cosmetic |  |  | `vm_rot_point_around_line(&pt3, &pt2, fl_radian(cosmetic_frand_range(-ang, ang)), &vmd_z...` |
| 1772 | `neb2_add_inner` | Cosmetic |  |  | `neb->pts[idx].x = cosmetic_frand_range(-1.0f * neb->inner_radius, neb->inner_radius) + ...` |
| 1773 | `neb2_add_inner` | Cosmetic |  |  | `neb->pts[idx].y = cosmetic_frand_range(-1.0f * neb->inner_radius, neb->inner_radius) + ...` |
| 1774 | `neb2_add_inner` | Cosmetic |  |  | `neb->pts[idx].z = cosmetic_frand_range(-1.0f * neb->inner_radius, neb->inner_radius) + ...` |
| 1777 | `neb2_add_inner` | Cosmetic |  |  | `neb->bmaps[idx] = (int)cosmetic_frand_range(0.0f, (float)2);` |
| 1799 | `neb2_add_outer` | Cosmetic |  |  | `vm_rot_point_around_line(&pt2, &pt, fl_radian(cosmetic_frand_range(-ang, ang)), &vmd_ze...` |
| 1802 | `neb2_add_outer` | Cosmetic |  |  | `vm_rot_point_around_line(&pt3, &pt2, fl_radian(cosmetic_frand_range(-ang, ang)), &vmd_z...` |
| 1808 | `neb2_add_outer` | Cosmetic |  |  | `phi = fl_radian(cosmetic_frand_range(0.0f, 360.0f));` |
| 1809 | `neb2_add_outer` | Cosmetic |  |  | `theta = fl_radian(cosmetic_frand_range(0.0f, 360.f));` |
| 1817 | `neb2_add_outer` | Cosmetic |  |  | `neb->bmaps[idx] = (int)cosmetic_frand_range(0.0f, (float)2);` |

### Nebula/NebLightning.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 581 | `nebl_render_all` | Gameplay |  | nebula lightning applies EMP | `Nebl_alpha = frand();` |
| 582 | `nebl_render_all` | Gameplay |  | nebula lightning applies EMP | `Nebl_glow_alpha = frand();` |
| 626 | `nebl_render_all` | Gameplay |  | nebula lightning applies EMP | `if(frand_range(0.0f, 1.0f) < 0.5f){` |
| 670 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `Nebl_stamp = timestamp((int)frand_range((float)Storm->min, (float)Storm->max));` |
| 677 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `num_bolts = (int)frand_range((float)Storm->min_count, (float)Storm->max_count);` |
| 683 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `s1 = (int)frand_range(0.0f, (float)Neb2_slices);` |
| 684 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `s2 = (int)frand_range(0.0f, (float)Neb2_slices);` |
| 685 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `s3 = (int)frand_range(0.0f, (float)Neb2_slices);` |
| 687 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `e1 = (int)frand_range(0.0f, (float)Neb2_slices);` |
| 688 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `e2 = (int)frand_range(0.0f, (float)Neb2_slices);` |
| 689 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `e3 = (int)frand_range(0.0f, (float)Neb2_slices);` |
| 716 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `if(frand_range(0.0, 1.0f) < 0.5f){` |
| 735 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `int type = (int)frand_range(0.0f, (float)(Storm->num_bolt_types-1));` |
| 740 | `nebl_process` | Gameplay |  | bolt placement; bolts apply EMP | `Nebl_stamp = timestamp((int)frand_range((float)Storm->min, (float)Storm->max));` |
| 969 | `nebl_gen` | Gameplay |  | bolt shape decides whether the EMP fires | `if(!child && (frand() <= Nebl_type->b_rand)){` |
| 990 | `nebl_gen` | Gameplay |  | bolt shape decides whether the EMP fires | `tmp.x += (frand()-0.5f)*d*scaler;` |
| 991 | `nebl_gen` | Gameplay |  | bolt shape decides whether the EMP fires | `tmp.y += (frand()-0.5f)*d*scaler;` |
| 992 | `nebl_gen` | Gameplay |  | bolt shape decides whether the EMP fires | `tmp.z += (frand()-0.5f)*d*scaler;` |
| 1295 | `nebl_jitter` | Gameplay |  | bolt shape decides whether the EMP fires | `vm_vec_random_in_circle(&moveup->pos, &temp, &m, frand_range(0.0f, length * bi->noise),...` |

### Network/MultiUI.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 3096 | `multi_sg_init_gamenet` | Multiplayer | yes |  | `Multi_sg_netgame->security = (rand() % 32766) + 1;` |

### Network/MultiUtil.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 427 | `multi_random_death_word` | Multiplayer | yes |  | `index = rand() % NUM_DEATH_WORDS;` |
| 526 | `multi_random_chat_start` | Multiplayer | yes |  | `index = rand() % NUM_CHAT_START_WORDS;` |

### Network/multi_respawn.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 878 | `multi_respawn_check_ai` | Multiplayer |  |  | `switch((int)frand_range(0.0f, 3.9f)){ \` |
| 932 | `multi_respawn_place` | Multiplayer |  |  | `int d = (int)frand_range(0.0f, 5.9f);` |

### Network/multilag.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 364 | `multi_lag_get_random_lag` | Multiplayer | yes |  | `if((float)rand()/(float)RAND_MAX < 0.5){` |
| 367 | `multi_lag_get_random_lag` | Multiplayer | yes |  | `mod = - (int)((float)(Multi_lag_base - Multi_lag_min) * ((float)rand()/(float)RAND_MAX));` |
| 372 | `multi_lag_get_random_lag` | Multiplayer | yes |  | `mod = (int)((float)(Multi_lag_max - Multi_lag_base) * ((float)rand()/(float)RAND_MAX));` |
| 408 | `multi_lag_should_be_lost` | Multiplayer | yes |  | `if((float)rand()/(float)RAND_MAX < 0.5){` |
| 411 | `multi_lag_should_be_lost` | Multiplayer | yes |  | `mod = - ((Multi_loss_base - Multi_loss_min) * ((float)rand()/(float)RAND_MAX));` |
| 416 | `multi_lag_should_be_lost` | Multiplayer | yes |  | `mod = ((Multi_loss_max - Multi_loss_base) * ((float)rand()/(float)RAND_MAX));` |
| 420 | `multi_lag_should_be_lost` | Multiplayer | yes |  | `if((float)rand()/(float)RAND_MAX <= Multi_loss_base + mod){` |

### Object/CollideDebrisShip.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 233 | `collide_debris_ship` | Gameplay |  |  | `float cap_percent_damage = frand_range(0.1f, 0.2f);` |

### Object/Object.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1517 | `obj_move_all_post` | Cosmetic |  | explosion lights | `light_add_point( &tmp1, 10.0f, 20.0f, cosmetic_frand(), 1.0f, 1.0f, 1.0f, -1 );` |
| 1518 | `obj_move_all_post` | Cosmetic |  | explosion lights | `light_add_point( &tmp2, 10.0f, 20.0f, cosmetic_frand(), 1.0f, 1.0f, 1.0f, -1 );` |
| 1537 | `obj_move_all_post` | Cosmetic |  | explosion lights | `float rad = p*(1.0f+cosmetic_frand()*0.05f)*objp->radius;` |
| 1566 | `obj_move_all_post` | Cosmetic |  | explosion lights | `light_add_point( &tmp1, 10.0f, 20.0f, cosmetic_frand(), 1.0f, 1.0f, 1.0f, -1 );` |
| 1567 | `obj_move_all_post` | Cosmetic |  | explosion lights | `light_add_point( &tmp2, 10.0f, 20.0f, cosmetic_frand(), 1.0f, 1.0f, 1.0f, -1 );` |

### Particle/Particle.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 753 | `particle_emit` | Cosmetic |  |  | `n = (cosmetic_rand() % (n2-n1+1)) + n1;` |
| 763 | `particle_emit` | Cosmetic |  |  | `float radius = (( pe->max_rad - pe->min_rad ) * cosmetic_frand()) + pe->min_rad;` |
| 765 | `particle_emit` | Cosmetic |  |  | `float speed = (( pe->max_vel - pe->min_vel ) * cosmetic_frand()) + pe->min_vel;` |
| 767 | `particle_emit` | Cosmetic |  |  | `float life = (( pe->max_life - pe->min_life ) * cosmetic_frand()) + pe->min_life;` |
| 769 | `particle_emit` | Cosmetic |  |  | `normal.x = pe->normal.x + (cosmetic_frand()*2.0f - 1.0f)*pe->normal_variance;` |
| 770 | `particle_emit` | Cosmetic |  |  | `normal.y = pe->normal.y + (cosmetic_frand()*2.0f - 1.0f)*pe->normal_variance;` |
| 771 | `particle_emit` | Cosmetic |  |  | `normal.z = pe->normal.z + (cosmetic_frand()*2.0f - 1.0f)*pe->normal_variance;` |

### Physics/Physics.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 582 | `physics_sim_rot` | Gameplay | yes |  | `tangles.p += (float) (rand()-RAND_MAX/2)/RAND_MAX * pi->speed/max_speed/64.0f;` |
| 583 | `physics_sim_rot` | Gameplay | yes |  | `tangles.h += (float) (rand()-RAND_MAX/2)/RAND_MAX * pi->speed/max_speed/64.0f;` |
| 592 | `physics_sim_rot` | Gameplay |  |  | `tangles.p += (float) (myrand()-RAND_MAX/2)/RAND_MAX * shock_amplitude;` |
| 593 | `physics_sim_rot` | Gameplay |  |  | `tangles.h += (float) (myrand()-RAND_MAX/2)/RAND_MAX * shock_amplitude;` |

### Playerman/ManagePilot.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1240 | `pilot_set_random_pic` | Menu/UI | yes |  | `int random_index = rand() % Num_pilot_images;` |
| 1255 | `pilot_set_random_squad_pic` | Menu/UI | yes |  | `int random_index = rand() % Num_pilot_squad_images;` |

### Playerman/PlayerControl.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 2038 | `player_maybe_play_all_alone_msg` | Gameplay | yes | message choice; kept on gameplay stream to be safe | `if ( rand()&1 ) {` |

### Radar/Radar.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 643 | `radar_blip_draw_distorted` | Cosmetic |  |  | `xdiff = -10 + cosmetic_rand()%20;` |
| 644 | `radar_blip_draw_distorted` | Cosmetic |  |  | `ydiff = -10 + cosmetic_rand()%20;` |
| 669 | `radar_blip_draw_flicker` | Cosmetic |  |  | `Radar_flicker_timer[flicker_index] = cosmetic_timestamp_rand(50,1000);` |
| 677 | `radar_blip_draw_flicker` | Cosmetic |  |  | `if ( cosmetic_rand() & 1 ) {` |
| 678 | `radar_blip_draw_flicker` | Cosmetic |  |  | `xdiff = -2 + cosmetic_rand()%4;` |
| 679 | `radar_blip_draw_flicker` | Cosmetic |  |  | `ydiff = -2 + cosmetic_rand()%4;` |
| 826 | `radar_frame_render` | Cosmetic |  |  | `Radar_static_next = cosmetic_timestamp_rand(50, 750);` |

### Ship/AiBig.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 455 | `ai_bpap` | Gameplay |  |  | `index = (int) (frand() * (octp->nverts));` |
| 535 | `ai_big_pick_attack_point_turret` | Gameplay |  |  | `ssp->turret_pick_big_attack_point_timestamp = timestamp(2000 + (int) (frand()*500.0f));` |
| 567 | `ai_big_pick_attack_point` | Gameplay |  |  | `attacker_aip->pick_big_attack_point_timestamp = timestamp(2000 + (int) (frand()*500.0f));` |
| 581 | `ai_big_pick_attack_point` | Gameplay |  |  | `wp->pick_big_attack_point_timestamp = timestamp(2000 + (int) (frand()*500.0f));` |
| 627 | `ai_big_evade_ship` | Gameplay |  |  | `vm_vec_rand_vec_quick(&randvec);` |
| 758 | `ai_big_maybe_follow_subsys_path` | Gameplay |  |  | `aip->path_subsystem_next_check = timestamp_rand(5000,8000);` |
| 1542 | `ai_big_strafe_attack` | Gameplay | yes |  | `switch(rand()%4) {` |
| 1652 | `ai_big_strafe_retreat1` | Gameplay | yes |  | `switch(rand()%4) {` |
| 1870 | `ai_big_strafe_maybe_attack_turret` | Gameplay |  |  | `if ( (frand()*100) > (aip->ai_courage-15) )` |

### Ship/AiCode.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 2652 | `evaluate_obj_as_target` | Gameplay |  |  | `if (frand() > (turret_stealth_find_chance + speed_mod)) {` |
| 2864 | `get_enemy_timestamp` | Gameplay |  |  | `return (NUM_SKILL_LEVELS - Game_skill_level) * ( (myrand() % 500) + 500);` |
| 2948 | `find_turret_enemy` | Gameplay |  |  | `if ((frand() < 0.8f) && (aip->target_objnum != -1) && Use_parent_target) {` |
| 3111 | `ai_attack_wing` | Gameplay |  |  | `index = (int) (frand() * count);` |
| 3590 | `maybe_avoid_player` | Gameplay |  |  | `vm_vec_copy_scale(&avoid_vec, &objp->orient.rvec, frand()-0.5f);` |
| 3591 | `maybe_avoid_player` | Gameplay |  |  | `vm_vec_scale_add2(&avoid_vec, &objp->orient.uvec, frand()-0.5f);` |
| 5280 | `evade_ship` | Gameplay |  |  | `if (aip->ai_evasion > myrand()*100.0f/32767.0f) {` |
| 5558 | `ai_fire_primary_weapon` | Gameplay |  |  | `if (frand() > 0.5f) {` |
| 5641 | `ai_fire_primary_weapon` | Gameplay |  |  | `if (frand() < 0.75f) {` |
| 6915 | `ai_stealth_sweep` | Gameplay |  |  | `vm_vec_rand_vec_quick(&aip->stealth_velocity);` |
| 7089 | `ai_chase_es` | Gameplay |  |  | `tvec.x += frand();` |
| 7090 | `ai_chase_es` | Gameplay |  |  | `tvec.y += frand();` |
| 7190 | `ai_set_guard_vec` | Gameplay |  |  | `vm_vec_rand_vec_quick(&aip->guard_vec);` |
| 7208 | `ai_set_guard_vec` | Gameplay |  |  | `vm_vec_rand_vec_quick(&rvec);` |
| 7535 | `set_secondary_fire_delay` | Gameplay |  |  | `t *= frand_range(0.8f, 1.2f);` |
| 7539 | `set_secondary_fire_delay` | Gameplay |  |  | `if (frand() < 0.5f)` |
| 8120 | `ai_chase` | Gameplay |  |  | `(frand() < (float) Game_skill_level/NUM_SKILL_LEVELS)) {` |
| 8137 | `ai_chase` | Gameplay |  |  | `if ((Missiontime - aip->last_hit_target_time > i2f(5)) && (frand() < (float) (aip->ai_c...` |
| 8146 | `ai_chase` | Gameplay |  |  | `if (frand() > 0.5f) {` |
| 8148 | `ai_chase` | Gameplay |  |  | `aip->submode_parm0 = myrand() & 0x0f;` |
| 8220 | `ai_chase` | Gameplay |  |  | `switch (myrand() % 5) {` |
| 8230 | `ai_chase` | Gameplay |  |  | `if (frand() < (float) 0.5f * (aip->ai_class + Game_skill_level)/(Num_ai_classes + NUM_S...` |
| 8427 | `ai_chase` | Gameplay |  |  | `if (frand() < 0.25f/(30.0f*hull_percent) * count)` |
| 8872 | `remove_farthest_attacker` | Gameplay |  |  | `aip->ignore_expire_timestamp = timestamp(((myrand() % 10) + 20) * 1000);` |
| 9664 | `ai_do_objects_repairing_stuff` | Gameplay |  |  | `stamp = timestamp((int) ((30 + 10*frand()) * 1000));` |
| 9673 | `ai_do_objects_repairing_stuff` | Gameplay |  |  | `stamp = timestamp((int) ((30 + 10*frand()) * 1000));` |
| 9714 | `ai_do_objects_repairing_stuff` | Gameplay |  |  | `stamp = timestamp((int) ((30 + 10*frand()) * 1000));` |
| 9726 | `ai_do_objects_repairing_stuff` | Gameplay |  |  | `stamp = timestamp((int) ((30 + 10*frand()) * 1000));` |
| 9839 | `ai_dock_shake` | Gameplay |  |  | `vm_vec_rand_vec_quick(&tangles);` |
| 10532 | `aifft_find_turret_subsys` | Gameplay |  |  | `int offset = (int)frand_range(0.0f, (float)(aifft_list_size % stride));` |
| 10635 | `turret_set_next_fire_timestamp` | Gameplay |  |  | `wait *= frand_range(0.9f, 1.1f);` |
| 10761 | `turret_fire_weapon` | Gameplay |  |  | `float wait = 1000.0f * frand_range(0.9f, 1.1f);` |
| 10948 | `ai_fire_from_turret` | Gameplay |  |  | `if (( num_ships_nearby >= 3 ) \|\| ((num_ships_nearby >= 2) && (frand() < 0.1f))) {` |
| 10984 | `ai_fire_from_turret` | Gameplay |  |  | `ss->turret_next_enemy_check_stamp = timestamp((int) (2000.0f * frand_range(0.9f, 1.1f)));` |
| 12013 | `ai_maybe_launch_cmeasure` | Gameplay |  |  | `float r = frand();` |
| 12272 | `ai_maybe_evade_locked_missile` | Gameplay |  |  | `(frand() < 1.0f - (float) shipp->cmeasure_count/8.0f)) {` |
| 13348 | `aas_1` | Gameplay |  |  | `vm_vec_rand_vec_quick(&rand_vec);` |
| 13436 | `rand_chance_test` | Debug |  | AI debug test | `if (rand_chance(frametime, chance))` |
| 13464 | `ai_avoid_shockwave` | Gameplay |  |  | `if (!rand_chance(flFrametime, (float) aip->ai_class/4.0f + 0.25f))` |
| 14421 | `maybe_process_friendly_hit` | Gameplay |  | friendly-fire warning and traitor tracking | `} else if ((damage > frand()) && (Missiontime - pp->last_warning_message_time > F1_0*4)...` |

### Ship/Shield.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 712 | `get_free_global_shield_index` | Cosmetic |  | shield hit effect slot | `gi = (int) (cosmetic_frand() * MAX_SHIELD_TRI_BUFFER);` |
| 728 | `get_global_shield_tri` | Cosmetic |  | shield hit effect slot | `shnum = cosmetic_rand() % MAX_SHIELD_HITS;` |
| 775 | `copy_shield_to_globals` | Cosmetic |  | shield hit effect slot | `gi = (int) (cosmetic_frand() * MAX_SHIELD_TRI_BUFFER);` |

### Ship/Ship.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 2264 | `subsys_set` | Gameplay |  | initial turret fire delay | `ship_system->turret_next_fire_stamp = timestamp((int) frand_range(1.0f, 500.0f));` |
| 2521 | `ship_render` | Cosmetic |  |  | `ft *= (1.0f + cosmetic_frand()/5.0f - 1.0f/10.0f);` |
| 3037 | `ship_blow_up_area_apply_blast` | Cosmetic |  | shockwave rotation (render) | `sci.rot_angle = cosmetic_frand_range(0.0f, 359.0f);` |
| 3109 | `do_dying_undock_physics` | Gameplay |  |  | `vm_vec_rand_vec_quick(&pos);` |
| 3118 | `do_dying_undock_physics` | Gameplay |  |  | `vm_vec_rand_vec_quick(&pos);` |
| 3191 | `ship_dying_frame` | Cosmetic |  | death-roll fireballs, engine-sound stutter | `submodel_get_two_random_points_cosmetic(sp->modelnum, pm->detail[0], &pnt1, &pnt2 );` |
| 3197 | `ship_dying_frame` | Gameplay | yes | fireball type sets its lifetime, which decides when the fireball object exists | `int fireball_type = FIREBALL_EXPLOSION_LARGE1 + rand()%FIREBALL_NUM_LARGE_EXPLOSIONS;` |
| 3200 | `ship_dying_frame` | Gameplay |  | next fireball time decides when fireball objects are created | `sp->next_fireball = timestamp_rand(333,500);` |
| 3211 | `ship_dying_frame` | Cosmetic |  | death-roll fireballs, engine-sound stutter | `vm_vec_rand_vec_quick_cosmetic(&rand_vec);` |
| 3212 | `ship_dying_frame` | Cosmetic |  | death-roll fireballs, engine-sound stutter | `float scale = -vm_vec_dotprod(&objp->orient.fvec, &rand_vec) * (0.9f + 0.2f * cosmetic_...` |
| 3215 | `ship_dying_frame` | Cosmetic |  | death-roll fireballs, engine-sound stutter | `scale = objp->radius * cosmetic_frand() * 0.717f;` |
| 3220 | `ship_dying_frame` | Gameplay | yes | fireball type sets its lifetime, which decides when the fireball object exists | `int fireball_type = FIREBALL_EXPLOSION_LARGE1 + rand()%FIREBALL_NUM_LARGE_EXPLOSIONS;` |
| 3223 | `ship_dying_frame` | Gameplay |  | next fireball time decides when fireball objects are created | `sp->next_fireball = timestamp_rand(333,500);` |
| 3276 | `ship_dying_frame` | Cosmetic |  | death-roll fireballs, engine-sound stutter | `submodel_get_two_random_points_cosmetic(sp->modelnum, pm->detail[0], &pnt1, &pnt2 );` |
| 3281 | `ship_dying_frame` | Cosmetic |  | death-roll fireballs, engine-sound stutter | `float rad = cosmetic_frand()*0.30f;` |
| 3363 | `ship_dying_frame` | Gameplay | yes | fireball type sets its lifetime, which decides when the fireball object exists | `fireball_type = FIREBALL_EXPLOSION_LARGE1 + rand()%FIREBALL_NUM_LARGE_EXPLOSIONS;` |
| 3432 | `ship_dying_frame` | Cosmetic |  | death-roll fireballs, engine-sound stutter | `sp->next_engine_stutter = cosmetic_timestamp_rand(50, 250);` |
| 4951 | `ship_weapon_maybe_fail` | Gameplay |  |  | `if ( (frand()-0.2f) > weapons_subsys_str )` |
| 7516 | `ship_get_random_player_wing_ship` | Wrapper | yes | callers: message senders, arrival location (all gameplay stream) | `which_one = (rand() % count);` |
| 7566 | `ship_get_random_ship_in_wing` | Wrapper | yes | callers: squad messages, arrival messages (gameplay stream) | `which_one = (rand() % count);` |
| 7624 | `ship_get_random_team_ship` | Wrapper | yes | callers: arrival location, EMP (G) | `which_one = (rand() % num);` |
| 8335 | `ship_maybe_praise_player` | Gameplay |  | message choice; kept on gameplay stream to be safe | `if ( myrand()&1 ) {` |
| 8368 | `ship_maybe_praise_player` | Gameplay |  | message choice; kept on gameplay stream to be safe | `Player->praise_delay_timestamp = timestamp_rand(1000, 2000);` |
| 8491 | `ship_maybe_lament` | Gameplay | yes | message choice; kept on gameplay stream to be safe | `if ( rand()%4 == 0 ) {` |
| 8537 | `ship_maybe_scream` | Gameplay | yes | message choice; kept on gameplay stream to be safe | `if ( rand()&1 )` |
| 8608 | `ship_maybe_tell_about_rearm` | Gameplay | yes | message choice; kept on gameplay stream to be safe | `if ( rand() & 1 ) {` |
| 9019 | `ship_get_random_ship` | Gameplay |  | EMP and debug target-random | `rand_ship = (int)frand_range(0.0f, (float)(num_ships - 1));` |

### Ship/ShipFX.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 330 | `shipfx_subsystem_mabye_create_live_debris` | Gameplay |  | live debris objects | `float vel_mag = vm_vec_mag_quick(&radial_vel) * 1.3f * (0.9f + 0.2f*frand());` |
| 332 | `shipfx_subsystem_mabye_create_live_debris` | Gameplay |  | live debris objects | `vm_vec_rand_vec_quick(&rand_vec);` |
| 350 | `shipfx_subsystem_mabye_create_live_debris` | Gameplay |  | live debris objects | `float scale = (1.2f + 0.2f * frand()) * vel_mag / rotvel_mag;` |
| 548 | `shipfx_blow_up_model` | Gameplay |  | debris objects | `submodel_get_two_random_points(model, submodel, &pnt1, &pnt2 );` |
| 1614 | `shipfx_emit_spark` | Cosmetic |  |  | `spark_num = cosmetic_rand() % shipp->num_hits;` |
| 1689 | `shipfx_emit_spark` | Cosmetic |  |  | `float spark_duration = (float)pow(2.0f, -5.0f*(hull_percent-1.3f)) * (1.0f + 0.6f*(cosm...` |
| 1734 | `shipfx_emit_spark` | Cosmetic |  |  | `shipp->next_hit_spark = cosmetic_timestamp_rand(50,100);` |
| 1820 | `split_ship_init` | Gameplay |  | big-ship break-up timing; spawns debris | `split_ship->front_ship.next_fireball = timestamp_rand(0, 100);` |
| 1821 | `split_ship_init` | Gameplay |  | big-ship break-up timing; spawns debris | `split_ship->back_ship.next_fireball  = timestamp_rand(0, 100);` |
| 1830 | `split_ship_init` | Gameplay | yes | big-ship break-up timing; spawns debris | `int index = rand()%pm->num_split_plane;` |
| 1833 | `split_ship_init` | Gameplay |  | big-ship break-up timing; spawns debris | `init_clip_plane_dist = 0.5f * (0.5f - frand())*pm->core_radius;` |
| 1892 | `split_ship_init` | Gameplay |  | big-ship break-up timing; spawns debris | `float explosion_time = (3.0f + expl_length_scale + (frand()-0.5f)) * speed_reduction_fa...` |
| 1898 | `split_ship_init` | Gameplay |  | big-ship break-up timing; spawns debris | `float rel_vel = (0.6f + 0.2f*frand()) * expl_vel * speed_reduction_factor;` |
| 1905 | `split_ship_init` | Gameplay |  | big-ship break-up timing; spawns debris | `vm_vec_rand_vec_quick(&rotvel);` |
| 2022 | `half_ship_render_ship_and_debris` | Gameplay |  | render function that creates debris objects | `float radial_mag = 10.0f + 30.0f*frand();` |
| 2131 | `do_sub_expl_sound` | Cosmetic |  |  | `int handle_index = cosmetic_rand()%NUM_SUB_EXPL_HANDLES;` |
| 2176 | `maybe_fireball_wipe` | Cosmetic |  |  | `vm_vec_rand_vec_quick_cosmetic(&temp);` |
| 2177 | `maybe_fireball_wipe` | Cosmetic |  |  | `vm_vec_scale(&temp, 0.1f*cosmetic_frand());` |
| 2182 | `maybe_fireball_wipe` | Cosmetic |  |  | `rad = half_ship->parent_obj->radius * cosmetic_frand_range(0.4f, 0.6f);` |
| 2185 | `maybe_fireball_wipe` | Cosmetic |  |  | `rad *= cosmetic_frand_range(1.4f, 1.6f);` |
| 2192 | `maybe_fireball_wipe` | Gameplay | yes | fireball type sets its lifetime, which decides when the fireball object exists | `int fireball_type = FIREBALL_EXPLOSION_LARGE1 + rand()%FIREBALL_NUM_LARGE_EXPLOSIONS;` |
| 2200 | `maybe_fireball_wipe` | Gameplay |  | next fireball time decides when fireball objects are created | `half_ship->next_fireball = timestamp_rand(time_low, time_high);` |
| 2390 | `shipfx_do_damaged_arcs_frame` | Cosmetic |  |  | `shipp->arc_next_time = cosmetic_timestamp_rand(freq*2,freq*4);` |
| 2399 | `shipfx_do_damaged_arcs_frame` | Cosmetic |  |  | `int n, n_arcs = ((cosmetic_rand()>>5) % 3)+1;` |
| 2402 | `shipfx_do_damaged_arcs_frame` | Cosmetic |  |  | `submodel_get_two_random_points_cosmetic( shipp->modelnum, -1, &v1, &v2 );` |
| 2403 | `shipfx_do_damaged_arcs_frame` | Cosmetic |  |  | `submodel_get_two_random_points_cosmetic( shipp->modelnum, -1, &v3, &v4 );` |
| 2442 | `shipfx_do_damaged_arcs_frame` | Cosmetic |  |  | `int lifetime = (cosmetic_rand()%((b)-(a)+1))+(a);` |
| 2512 | `shipfx_do_damaged_arcs_frame` | Cosmetic |  |  | `int mr = cosmetic_rand();` |
| 2515 | `shipfx_do_damaged_arcs_frame` | Cosmetic |  |  | `submodel_get_two_random_points_cosmetic( shipp->modelnum, -1, &v1, &v2 );` |
| 2592 | `shipfx_do_lightning_frame` | Gameplay |  | creates nebula lightning bolts (EMP) | `stamp = (int)((float)(Nebl_cruiser_min + ((Nebl_cruiser_max - Nebl_cruiser_min) * Nebl_...` |
| 2597 | `shipfx_do_lightning_frame` | Gameplay |  | creates nebula lightning bolts (EMP) | `stamp = (int)((float)(Nebl_supercap_min + ((Nebl_supercap_max - Nebl_supercap_min) * Ne...` |
| 2600 | `shipfx_do_lightning_frame` | Gameplay |  | creates nebula lightning bolts (EMP) | `stamp = (int)((float)(Nebl_cap_min + ((Nebl_cap_max - Nebl_cap_min) * Nebl_intensity)) ...` |
| 2621 | `shipfx_do_lightning_frame` | Gameplay |  | creates nebula lightning bolts (EMP) | `count = (int)frand_range(0.0f, (float)count);` |
| 2624 | `shipfx_do_lightning_frame` | Gameplay |  | creates nebula lightning bolts (EMP) | `submodel_get_two_random_points(shipp->modelnum, 0, &v1, &v2, &n1, &n2);` |
| 2643 | `shipfx_do_lightning_frame` | Gameplay |  | creates nebula lightning bolts (EMP) | `binfo.delay = (int)frand_range(0.0f, 1600.0f);` |
| 2669 | `shipfx_do_lightning_frame` | Gameplay |  | creates nebula lightning bolts (EMP) | `binfo.delay = (int)frand_range(0.0f, 1600.0f);` |
| 2752 | `shipfx_do_shockwave_stuff` | Gameplay |  | shockwave damage, blast, speed | `sci2.blast = (sci->blast / (float)sip->shockwave_count) * frand_range(0.75f, 1.25f);` |
| 2753 | `shipfx_do_shockwave_stuff` | Gameplay |  | shockwave damage, blast, speed | `sci2.damage = (sci->damage / (float)sip->shockwave_count) * frand_range(0.75f, 1.25f);` |
| 2756 | `shipfx_do_shockwave_stuff` | Gameplay |  | shockwave damage, blast, speed | `sci2.speed = sci->speed * frand_range(0.75f, 1.25f);` |
| 2757 | `shipfx_do_shockwave_stuff` | Gameplay |  | shockwave damage, blast, speed | `sci2.rot_angle = frand_range(0.0f, 359.0f);` |
| 2759 | `shipfx_do_shockwave_stuff` | Gameplay |  | shockwave damage, blast, speed | `shockwave_create(shipp->objnum, &shockwave_pos, &sci2, SW_SHIP_DEATH, (int)frand_range(...` |

### Ship/ShipHit.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 548 | `do_subobj_destroyed_stuff` | Cosmetic |  | fireball positions | `vm_vec_rand_vec_quick_cosmetic(&rand_vec);` |
| 715 | `create_subsys_debris` | Gameplay |  |  | `float show_debris = frand();` |
| 735 | `create_vaporize_debris` | Gameplay |  |  | `float show_debris = frand();` |
| 1208 | `ship_hit_sparks_no_rotate` | Cosmetic |  |  | `n = cosmetic_rand() % MAX_SHIP_HITS;` |
| 1325 | `choose_next_spark` | Cosmetic |  | this desync | `if (cosmetic_frand() > 0.5f) {` |
| 1326 | `choose_next_spark` | Cosmetic |  | this desync | `return (cosmetic_rand() % num_sparks);` |
| 1401 | `ship_hit_create_sparks` | Cosmetic |  |  | `n = cosmetic_rand() % max_sparks;` |
| 1626 | `ship_generic_kill_stuff` | Gameplay |  | death-roll time and spin | `delta_time = 7000 + (int)(frand() * 3000.0f);` |
| 1678 | `ship_generic_kill_stuff` | Gameplay |  | death-roll time and spin | `sp->deathroll_rotvel.x += (frand() - 0.5f) * 2.0f * rotvel_mag;` |
| 1680 | `ship_generic_kill_stuff` | Gameplay |  | death-roll time and spin | `sp->deathroll_rotvel.y += (frand() - 0.5f) * 3.0f * rotvel_mag;` |
| 1682 | `ship_generic_kill_stuff` | Gameplay |  | death-roll time and spin | `sp->deathroll_rotvel.z += (frand() - 0.5f) * 6.0f * rotvel_mag;` |
| 2461 | `ship_apply_wash_damage` | Gameplay |  |  | `vm_vec_rand_vec_quick(&rand_vec);` |

### Starfield/StarField.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 501 | `stars_level_init` | Cosmetic |  | at load | `v.x = (float) ((cosmetic_rand() & RND_MAX_MASK) - HALF_RND_MAX);` |
| 502 | `stars_level_init` | Cosmetic |  | at load | `v.y = (float) ((cosmetic_rand() & RND_MAX_MASK) - HALF_RND_MAX);` |
| 503 | `stars_level_init` | Cosmetic |  | at load | `v.z = (float) ((cosmetic_rand() & RND_MAX_MASK) - HALF_RND_MAX);` |
| 1209 | `stars_draw` | Cosmetic |  | per rendered frame | `d->pos.x = f2fl(cosmetic_rand() - RAND_MAX/2);` |
| 1210 | `stars_draw` | Cosmetic |  | per rendered frame | `d->pos.y = f2fl(cosmetic_rand() - RAND_MAX/2);` |
| 1211 | `stars_draw` | Cosmetic |  | per rendered frame | `d->pos.z = f2fl(cosmetic_rand() - RAND_MAX/2);` |
| 1223 | `stars_draw` | Cosmetic |  | per rendered frame | `d->size = i2fl(cosmetic_rand() % 4)*BASE_SIZE_NEB;` |
| 1225 | `stars_draw` | Cosmetic |  | per rendered frame | `d->size = i2fl(cosmetic_rand() % 4)*BASE_SIZE;` |

### Starfield/Supernova.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 171 | `supernova_do_particles` | Cosmetic |  |  | `submodel_get_two_random_points_cosmetic(Player_ship->modelnum, 0, &ta, &tb);` |
| 329 | `supernova_apply_shake` | Cosmetic |  |  | `int r1 = cosmetic_rand();` |
| 330 | `supernova_apply_shake` | Cosmetic |  |  | `int r2 = cosmetic_rand();` |

### Weapon/Beam.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1377 | `beam_render` | Cosmetic |  |  | `scale = cosmetic_frand_range(1.0f - bwi->sections[s_idx].flicker, 1.0f + bwi->sections[...` |
| 1431 | `beam_generate_muzzle_particles` | Cosmetic |  |  | `particle_count = (int)cosmetic_frand_range(0.0f, (float)wip->b_info.beam_particle_count);` |
| 1443 | `beam_generate_muzzle_particles` | Cosmetic |  |  | `vm_vec_scale_add(&p_temp, &turret_pos, &particle_dir, wip->b_info.beam_muzzle_radius * ...` |
| 1453 | `beam_generate_muzzle_particles` | Cosmetic |  |  | `float p_life = cosmetic_frand_range(p_time_ref * 0.5f, p_time_ref * 0.7f);` |
| 1454 | `beam_generate_muzzle_particles` | Cosmetic |  |  | `float p_vel = (wip->b_info.beam_muzzle_radius / p_life) * cosmetic_frand_range(0.85f, 1...` |
| 1500 | `beam_render_muzzle_glow` | Cosmetic |  |  | `rand_val = cosmetic_frand_range(0.90f, 1.0f);` |
| 1613 | `beam_add_light_small` | Cosmetic |  |  | `noise = cosmetic_frand_range(1.0f - bwi->sections[0].flicker, 1.0f + bwi->sections[0].f...` |
| 1666 | `beam_add_light_large` | Cosmetic |  |  | `noise = cosmetic_frand_range(1.0f - bwi->sections[0].flicker, 1.0f + bwi->sections[0].f...` |
| 2028 | `beam_get_binfo` | Gameplay |  | beam aim | `b->binfo.shot_aim[0] = frand_range(0.0f, 1.0f + bwi->beam_miss_factor[skill_level] * ac...` |
| 2032 | `beam_get_binfo` | Gameplay |  | beam aim | `submodel_get_two_random_points(model_num, 0, &b->binfo.dir_a, &b->binfo.dir_b);` |
| 2037 | `beam_get_binfo` | Gameplay |  | beam aim | `beam_get_octant_points(model_num, b->target, (int)frand_range(0.0f, BEAM_NUM_GOOD_OCTAN...` |
| 2061 | `beam_get_binfo` | Gameplay |  | beam aim | `b->binfo.shot_aim[idx] = frand_range(0.0f, 1.0f + r);` |

### Weapon/Corkscrew.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 141 | `cscrew_create` | Gameplay |  |  | `if(frand_range(0.0f, 1.0f) < 0.5f){` |

### Weapon/Emp.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 490 | `emp_process_local` | Gameplay |  | retargets the player | `Emp_wacky_target_timestamp = timestamp((int)frand_range(100.0f, 750.0f * (1.0f - Emp_in...` |
| 503 | `emp_should_blit_gauge` | Cosmetic |  |  | `return cosmetic_frand_range(0.0f, 1.0f) > Emp_intensity;` |
| 560 | `emp_maybe_reformat_text` | Cosmetic |  |  | `if(cosmetic_frand_range(0.0f, 1.0f) > Emp_intensity){` |
| 580 | `emp_maybe_reformat_text` | Cosmetic |  |  | `wep_index = (int)cosmetic_frand_range(0.0f, (float)(MAX_WEAPON_TYPES - 1));` |
| 625 | `emp_maybe_reformat_text` | Cosmetic |  |  | `wt->stamp = timestamp((int)cosmetic_frand_range(100.0f, 750.0f * (1.0f - Emp_intensity)));` |
| 644 | `emp_randomize_chars` | Cosmetic |  |  | `if(cosmetic_frand_range(0.0f, 1.0f) < Emp_intensity){` |
| 645 | `emp_randomize_chars` | Cosmetic |  |  | `char_index = Emp_random_char[(int)cosmetic_frand_range(0.0f, (float)(NUM_RANDOM_CHARS -...` |
| 660 | `emp_hud_jitter` | Cosmetic |  |  | `*x += (int)cosmetic_frand_range(-8.0f * Emp_intensity, 8.0f * Emp_intensity);` |
| 661 | `emp_hud_jitter` | Cosmetic |  |  | `*y += (int)cosmetic_frand_range(-8.0f * Emp_intensity, 8.0f * Emp_intensity);` |

### Weapon/Flak.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 165 | `flak_pick_range` | Gameplay |  |  | `final_range += (Flak_range + (Flak_range * 0.65f * (1.0f - weapon_subsys_strength))) * ...` |
| 192 | `flak_jitter_aim` | Gameplay |  |  | `float rand_dist = frand_range(0.0f, error_val);` |
| 200 | `flak_jitter_aim` | Gameplay |  |  | `vm_rot_point_around_line(&rand_twist_post, &rand_twist_pre, fl_radian(359.0f * frand_ra...` |

### Weapon/Swarm.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 322 | `swarm_update_direction` | Gameplay |  |  | `swarmp->path_num` |
| 338 | `swarm_update_direction` | Gameplay |  |  | `swarmp->change_time = fl2i(SWARM_CHANGE_DIR_TIME + SWARM_TIME_VARIANCE*(frand() - 0.5f)...` |
| 390 | `swarm_update_direction` | Gameplay |  |  | `swarmp->path_num = swarmp->path_num + myrand()%3;` |

### Weapon/Weapons.cpp

| Line | Function | Stream | Raw `rand()` | Note | Code |
|---|---|---|---|---|---|
| 1542 | `weapon_render` | Cosmetic |  |  | `ft *= (1.0f + cosmetic_frand()/5.0f - 1.0f/10.0f);` |
| 1776 | `find_homing_object_cmeasures_1` | Gameplay |  |  | `if (frand() < chance) {` |
| 2029 | `weapon_home` | Gameplay |  |  | `} else if ( pick_homing_point \|\| (dist < 500.0f) \|\| (rand_chance(flFrametime, 2.0f)) ) {` |
| 2637 | `weapon_create` | Gameplay |  |  | `rand_val = frand();` |
| 2790 | `spawn_child_weapons` | Gameplay |  |  | `vm_vec_rand_vec_quick(&tvec);` |
| 2802 | `spawn_child_weapons` | Gameplay |  |  | `rand_val = frand();` |
| 2950 | `weapon_do_electronics_affect` | Gameplay |  |  | `ship_subsys_set_disrupted(ss, fl2i(6000.0f + frand()*4000.0f));` |
| 3458 | `weapon_maybe_spew_particle` | Cosmetic |  |  | `ang = fl_radian(cosmetic_frand_range(-90.0f, 90.0f));` |
| 3464 | `weapon_maybe_spew_particle` | Cosmetic |  |  | `ang = fl_radian(cosmetic_frand_range(-90.0f, 90.0f));` |
| 3470 | `weapon_maybe_spew_particle` | Cosmetic |  |  | `ang = fl_radian(cosmetic_frand_range(-90.0f, 90.0f));` |
