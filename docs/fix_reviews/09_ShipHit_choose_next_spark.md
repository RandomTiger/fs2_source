# Fix review: ShipHit.cpp `choose_next_spark` reads uninitialised spark pairs

**Recommendation: TAKE.** Low risk.

Found by replaying a Release recording (SM1-03) in Debug. The trace diverged at frame 11856 and the replay desynced at frame 11940.

## The bug
When a big ship's spark slots are all full, `choose_next_spark` picks a spark to recycle. Sometimes it does that by sorting every pair of sparks by distance and counting how often the two sparks of the closest pair appear in entries 1-5:
```cpp
	spark_pair spark_pairs[MAX_SPARK_PAIRS];	// 28 entries
	...
	for (i=0; i<num_spark_pairs; i++) { ... }	// initialises num_spark_pairs entries
	...
	for (i=1; i<6; i++) {						// always reads entries 1-5
		if (spark_pairs[i].index1 == index1) {
```
`num_spark_pairs` is `n*(n-1)/2`, where `n` is the ship's spark slot count, kept between 3 and 8. Ships with a radius under 50 have 3 slots, so 3 pairs. Entries 3-5 are uninitialised stack:
- **Debug:** 0xCCCCCCCC, which never matches a slot, so it is effectively ignored.
- **Release:** leftover stack values, which can equal 0, 1 or 2 and tip the choice towards the other spark.

Ships with 4 or more slots have at least 6 pairs and are not affected.

## How it broke a replay
SM1-03, frame 11856: five player laser bolts hit ship 216 in one frame.
1. Hit 2 took the pair branch. The builds recycled different sparks; in Release, hit 1's spark was overwritten.
2. Hit 3 landed about 0.7 units from hit 1. Debug found hit 1's spark and reused it with no random draw. Release found none, so it drew `frand()` and then `rand() % 3`.
3. Every random draw after that was shifted: debris, AI, weapons.

## Proposed fix
```diff
 	// initialize spark pairs
-	for (i=0; i<num_spark_pairs; i++) {
-		spark_pairs[i].index1 = 0;
-		spark_pairs[i].index2 = 0;
+	for (i=0; i<MAX_SPARK_PAIRS; i++) {
+		spark_pairs[i].index1 = -1;
+		spark_pairs[i].index2 = -1;
 		spark_pairs[i].dist = FLT_MAX;
 	}
 ...
-	for (i=1; i<6; i++) {
+	for (i=1; i<6 && i<count; i++) {
```
- The loop only reads pairs that exist.
- Every entry is initialised as well, as a guard. It uses -1, because 0 is a real slot and would be counted.
- Entries `0..count-1` are overwritten by the pair setup either way, so initialising them to -1 changes nothing.

Release now picks what Debug always picked.

## Effect on mission flow
None. Sparks are cosmetic: only spark code reads the spark slots, and their only output is particles. The bug reached gameplay only through the shared random number stream.

## Effect on replays
Release and Debug now choose the same spark. A Release recording that went through this path can replay differently. SM1-03 does (frame 11856), so it has to be re-recorded.
