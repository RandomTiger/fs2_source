# Fix review: debrief awards read Campaign.missions[-1] and can write past the award text

**Recommendation: TAKE.** Two small bounds bugs in how the debriefing shows awards. Found with `/analyze` (C6385) on 2026-10-13. Both are in the same few award functions, so they share a review.

## The bugs
**Badge voice.** `debrief_choose_badge_voice` (`MissionUI/MissionDebrief.cpp`) picks the voice for a newly earned badge from the current campaign mission. Outside a campaign mission (`Campaign.current_mission < 0`) it sets the default voice, but has no `return`:
```cpp
if(Campaign.current_mission < 0){
	// default to petrarch
	sprintf(Badge_stage.voice, NOX("9_%s"), Badge_info[Player->stats.m_badge_earned].voice_base);
}

if ((Campaign.missions[Campaign.current_mission].name) && (Campaign.filename)) {
```
so it goes on to read `Campaign.missions[-1].name`, the bytes before the array inside the `Campaign` struct, and may pass them to `stricmp` as a string. `debrief_choose_promotion_voice`, just above it and written the same way, has the `return`.

**Award text.** `debrief_add_award_text` adds up to two lines to `Debrief_award_text[AWARD_TEXT_MAX_LINES]` (5 lines). Its guard allows one line too many:
```cpp
Assert(Debrief_award_text_num_lines <= AWARD_TEXT_MAX_LINES);
if (Debrief_award_text_num_lines > AWARD_TEXT_MAX_LINES) {
	return;
}
...
strcpy(Debrief_award_text[Debrief_award_text_num_lines], str);
```
With 5 lines already in use it writes a sixth, past the end.

## Proposed fix
- Badge voice: add the missing `return`.
- Award text: the Assert and the check use `<` and `>=`.

## What was checked
- A debriefing shows at most a medal, a promotion and a badge, each taking at most two lines; the second line is only added while there is room. Medal and promotion use 4 lines, so the badge starts at line 4 and fits. The award text overrun is therefore not reached by the game as it is.
- The badge voice is chosen whenever the debriefing shows a newly earned badge.

## Not changed
Nothing else in the award code.

## Effect on mission flow
None on gameplay: this is debriefing display and voice choice. A badge earned outside a campaign mission now always gets the default voice, without reading memory before the mission array.
