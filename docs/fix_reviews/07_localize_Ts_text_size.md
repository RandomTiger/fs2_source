# Fix review: localize.cpp `lcl_ext_lookup` `Ts_text_size`

**Recommendation: SKIP.** It has no behavioural effect.

## The bug
```cpp
	Ts_current_state = TS_SCANNING;
	Ts_id_text_size = 0;
	Ts_text_size;
```
This is a bare expression where `= 0` was intended.

## Why it doesn't matter
The parser only reaches `TS_STRING`, where `Ts_text_size` is used, through `TS_OPEN_QUOTE`. That state resets it anyway when it sees the opening quote:
```cpp
				if(*p == '\"'){
					memset(Ts_text, 0, PARSE_TEXT_STRING_LEN);
					Ts_text_size = 0;
					Ts_current_state = TS_STRING;
```
So the value is always reset before it is read. Also, `lcl_ext_lookup` only runs for non-default languages (localize.cpp:829). The English game never calls it.

## Effect on mission flow
None.

## Effect on replays
None.

## Why skip
The fix would be cosmetic. It changes nothing at runtime, so it doesn't meet the minimum-changes rule.
