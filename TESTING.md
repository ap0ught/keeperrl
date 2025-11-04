# KeeperRL Testing Guide

This document provides guidance for testing features in KeeperRL.

## General Testing Approach

When implementing new features in KeeperRL, follow these testing guidelines:

1. **Identify the Feature Scope**: Determine what components are affected (gameplay, UI, data configuration, etc.)
2. **Create Test Scenarios**: Design specific scenarios that exercise the new functionality
3. **Manual Testing**: Since KeeperRL is primarily a game, manual testing is essential
4. **Configuration Testing**: Test data-driven features through game configuration files
5. **Integration Testing**: Verify the feature works with existing game systems

## Testing Workflow

1. **Build the Game**:
   ```bash
   # Debug build for testing
   make -j 8 DEBUG=true
   
   # Or optimized build for performance testing
   make -j 8 OPT=true RELEASE=true
   ```

2. **Prepare Test Data**: Create or modify game configuration files in `data_free/game_config/`

3. **Run the Game**: 
   ```bash
   ./keeper
   ```

4. **Execute Test Scenarios**: Follow the specific test steps for your feature

5. **Verify Results**: Check that the expected behavior occurs

6. **Document Results**: Record any issues or unexpected behavior

---

## Testing the petEffect Feature

### Overview
The `petEffect` feature allows creatures to apply effects when petted. This section describes how to test this feature.

### Prerequisites
- Built KeeperRL binary (`./keeper`)
- Access to `data_free/game_config/creatures.txt` for configuration
- Basic understanding of KeeperRL gameplay mechanics

### Test Setup

1. **Create a Test Creature with petEffect**

   Edit `data_free/game_config/creatures.txt` and add or modify a creature definition:

   ```
   "TEST_DOG"
     {
       viewId = { "dog" }
       attr = {
         DEFENSE 10
         DAMAGE 5
       }
       body = {
         type = Humanoid
         size = MEDIUM
         material = FLESH
       }
       name = {
         name = "friendly test dog"
         groupName = "pack of test dogs"
       }
       petReaction = "\"WOOF! *tail wagging intensifies*\""
       petEffect = Lasting 200 "HIGH_MORALE"
       aiType = WILDLIFE
       permanentEffects = {
         PEACEFULNESS 1
       }
     }
   ```

2. **Add the Creature to a Game**

   You can add the creature to an existing keeper game or create a scenario where the test creature spawns.

### Test Cases

#### Test Case 1: Basic petEffect Application

**Objective**: Verify that petting a creature applies the configured effect.

**Steps**:
1. Start KeeperRL
2. Load or start a game where the test creature is present
3. Approach the test creature with a humanoid character
4. Select the "Pet" action from the context menu
5. Observe the pet reaction message: "WOOF! *tail wagging intensifies*"
6. Check that the petting character receives the "HIGH_MORALE" buff
7. Verify the buff lasts for approximately 200 game turns

**Expected Results**:
- Pet action succeeds when adjacent to friendly creature
- Pet reaction message displays
- HIGH_MORALE buff is applied to the petting character
- Buff duration matches configuration (200 turns)

**Pass/Fail**: _____

---

#### Test Case 2: petEffect with Multiple Effects

**Objective**: Verify that chain effects work correctly.

**Setup**: Modify the test creature to use a Chain effect:
```
petEffect = Chain {
  Lasting 200 "HIGH_MORALE"
  Message "You feel the love!"
  Heal
}
```

**Steps**:
1. Restart the game to load the new configuration
2. Pet the test creature
3. Verify all three effects apply:
   - HIGH_MORALE buff appears
   - Message "You feel the love!" displays
   - Character's health increases (if damaged)

**Expected Results**:
- All effects in the chain apply in order
- Each effect functions correctly

**Pass/Fail**: _____

---

#### Test Case 3: petEffect Requires Friendly Creatures

**Objective**: Verify that petEffect only applies to friendly creatures.

**Steps**:
1. Modify the test creature to be hostile (remove PEACEFULNESS)
2. Attempt to pet the now-hostile creature
3. Verify that the pet action is not available or fails

**Expected Results**:
- Pet action is not available for hostile creatures
- No effect is applied

**Pass/Fail**: _____

---

#### Test Case 4: petEffect Requires Adjacency

**Objective**: Verify that petting requires being adjacent.

**Steps**:
1. Position character 2+ tiles away from friendly creature
2. Verify pet action is not available
3. Move to adjacent tile
4. Verify pet action becomes available

**Expected Results**:
- Pet action only available when adjacent (within 1 tile)

**Pass/Fail**: _____

---

#### Test Case 5: petEffect Requires Humanoid Body

**Objective**: Verify that only humanoid creatures can pet.

**Steps**:
1. Create or control a non-humanoid creature
2. Attempt to pet the test creature
3. Verify the action is not available

**Expected Results**:
- Non-humanoid creatures cannot pet other creatures

**Pass/Fail**: _____

---

#### Test Case 6: No petEffect Configured

**Objective**: Verify graceful handling when petEffect is not defined.

**Setup**: Remove `petEffect` from the creature definition (keep `petReaction`).

**Steps**:
1. Restart game with modified configuration
2. Pet the creature
3. Verify pet reaction displays but no effect applies

**Expected Results**:
- Pet action succeeds
- Pet reaction message displays
- No additional effects apply
- No crashes or errors

**Pass/Fail**: _____

---

### Regression Testing

After implementing petEffect, verify these existing features still work:

1. **chatEffect**: Test that creatures with chatEffect still apply effects when chatted with
2. **petReaction**: Test that petReaction text still displays without petEffect
3. **Serialization**: Save and load a game with petEffect creatures
4. **Combat**: Verify petting doesn't interfere with combat mechanics

### Performance Testing

For performance-sensitive scenarios:

1. Create multiple creatures with petEffect
2. Pet several creatures in rapid succession
3. Monitor frame rate and responsiveness
4. Verify no memory leaks over extended gameplay

### Bug Reporting

If issues are found during testing, document:
- Test case that failed
- Steps to reproduce
- Expected vs. actual behavior
- Game version/commit hash
- Configuration used
- Save files if applicable

---

## Testing the Mining Activity Fix

### Overview
The Mining activity now correctly mines ore from rock deposits instead of only collecting pre-existing ore items on the ground. Miners will use the DIG destroy action to extract ore from MOUNTAIN, IRON_ORE, GOLD_ORE, and other ore-bearing furniture, similar to how lumberjacks cut down trees.

### Prerequisites
- Built KeeperRL binary (`./keeper`)
- Access to game configuration files
- Understanding of KeeperRL keeper gameplay

### Key Changes
- **Fixed**: Mining task now destroys ore deposits (IRON_ORE, GOLD_ORE, ADAMANTIUM_ORE, etc.) using DIG action
- **Added**: MINING and LIGHTBRINGING minion activities
- **Added**: WORKMAN, MINER, and LIGHT_BRINGER minion traits
- **Added**: Mining follows same pattern as woodcutting (find deposit → destroy → collect items → return to cabin)

### Test Setup

The Mining activity requires:
1. A miner creature with MINER trait
2. A stone cabin (for ore storage and miner quarters)
3. Accessible ore deposits or mountains on the map
4. Known tiles that contain ore (explored territory)

### Test Cases

#### Test Case 1: Basic Ore Mining from Ore Deposits

**Objective**: Verify that miners can extract ore from IRON_ORE, GOLD_ORE, and other ore furniture.

**Steps**:
1. Start a keeper game with ore deposits visible on the map
2. Build a STONE_CABIN furniture (requires configuration from PR #15 or create manually)
3. Recruit or assign a creature with MINER trait to MINING activity
4. Assign the miner quarters in the stone cabin
5. Enable MINING activity for the miner
6. Observe the miner's behavior:
   - Should navigate toward visible ore deposits
   - Should move adjacent to ore deposit
   - Should perform DIG destroy action
   - Should collect dropped ore items
   - Should return to cabin when carrying sufficient ore (default: 3 loads)
   - Should drop ore items in cabin

**Expected Results**:
- Miner successfully destroys ore deposits using DIG action
- Ore items (IronOre, GoldPiece, etc.) drop when deposit is destroyed
- Miner picks up the ore items
- Miner returns to cabin and drops ore
- Process repeats for next ore deposit
- Mining activity completes successfully

**Pass/Fail**: _____

---

#### Test Case 2: Mining Regular Mountains for Stone

**Objective**: Verify that miners can extract stone from regular MOUNTAIN furniture.

**Steps**:
1. Position miner near regular mountain/rock tiles (not ore-specific)
2. Enable MINING activity
3. Observe miner mining regular mountains
4. Verify Stone items are collected (if mountain has itemDrop configured)

**Expected Results**:
- Miner destroys regular mountains with DIG action
- Any dropped items are collected
- Miner returns to cabin with resources

**Pass/Fail**: _____

---

#### Test Case 3: Mining vs. Woodcutting Comparison

**Objective**: Verify that mining works analogously to woodcutting.

**Setup**: Have both a LUMBERJACK and MINER in the same game.

**Steps**:
1. Assign lumberjack to WOODCUTTING activity
2. Assign miner to MINING activity
3. Observe both activities side-by-side
4. Verify similar behavioral patterns:
   - Both find targets (trees vs. ore deposits)
   - Both move adjacent and destroy
   - Both collect dropped resources
   - Both return to cabin after quota

**Expected Results**:
- Mining behavior mirrors woodcutting pattern
- Both activities function independently without conflicts

**Pass/Fail**: _____

---

#### Test Case 4: Multiple Ore Types

**Objective**: Verify miners handle different ore types correctly.

**Steps**:
1. Create a map with multiple ore types:
   - IRON_ORE
   - GOLD_ORE
   - ADAMANTIUM_ORE
   - ADOXIUM_ORE
   - INFERNITE_ORE
2. Enable MINING activity
3. Observe miner mining different ore types
4. Verify correct items drop from each ore type

**Expected Results**:
- Miner mines all configurable ore types
- Correct ore items drop based on furniture configuration
- Mining filters by configured resource IDs (STONE, IRON, GOLD by default)

**Pass/Fail**: _____

---

#### Test Case 5: Mining Activity Availability

**Objective**: Verify MINING activity is only available to creatures with MINER trait.

**Steps**:
1. Create/recruit creatures with different traits:
   - Creature with MINER trait
   - Creature with LUMBERJACK trait only
   - Regular WORKER creature
2. Attempt to assign MINING activity to each
3. Verify availability

**Expected Results**:
- Only creatures with MINER trait can perform MINING activity
- Other creatures don't have MINING available in their activity list

**Pass/Fail**: _____

---

#### Test Case 6: No Available Ore Deposits

**Objective**: Verify graceful handling when no ore deposits are available.

**Steps**:
1. Assign miner to MINING activity
2. Ensure no accessible ore deposits exist (all mined out or blocked)
3. Observe miner behavior

**Expected Results**:
- Miner activity completes without crashing
- Miner becomes idle or switches to other activities
- No errors or infinite loops

**Pass/Fail**: _____

---

#### Test Case 7: Cabin/Quarters Assignment

**Objective**: Verify miners require quarters assignment for cabin usage.

**Steps**:
1. Create miner without assigned quarters
2. Enable MINING activity
3. Verify task behavior
4. Assign quarters in stone cabin
5. Retry MINING activity

**Expected Results**:
- Mining task may fail without cabin (task should set done)
- Mining works properly with quarters assigned
- Miner returns to correct cabin location

**Pass/Fail**: _____

---

#### Test Case 8: Resource Collection Quota

**Objective**: Verify miners collect configured number of loads per trip.

**Steps**:
1. Review task.cpp for quota configuration (default: 3 loads per trip)
2. Enable MINING activity
3. Count number of ore deposits mined before return to cabin
4. Verify matches quota

**Expected Results**:
- Miner mines configured number of deposits (default: 3)
- Returns to cabin after reaching quota
- Drops all collected ore
- Repeats cycle

**Pass/Fail**: _____

---

### Regression Testing

Verify existing systems still work correctly:

1. **Woodcutting Activity**: Ensure woodcutting still works as before
2. **DIGGING Activity**: Verify regular digging tasks unaffected
3. **Worker Activities**: Confirm CONSTRUCTION, HAULING, WORKING still function
4. **Minion Equipment**: Verify miners can equip pickaxes if configured
5. **Territory System**: Confirm miners only mine in known tiles
6. **Storage System**: Verify ore storage IDs work correctly
7. **Save/Load**: Test game save and load with active mining tasks

### Comparison with Bug Report

The original issue stated:
> "The new `Mining` task only targets tiles that already have ore items lying on the ground (`findDeposit` scans for `hasOreOnGround`, and `collectOre` merely picks up items). No attempt is made to destroy rock or mine ore veins like the woodcutting task does for trees."

Verify the fix addresses this:

**Test Case: Pre-Fix vs Post-Fix Behavior**

**Steps**:
1. Place ore items on ground (not in furniture)
2. Create IRON_ORE furniture
3. Enable MINING activity

**Expected Results (Post-Fix)**:
- Miner destroys IRON_ORE furniture using DIG action
- Miner does NOT only pick up loose items
- Behavior matches woodcutting (destroys source, then collects drops)

**Pass/Fail**: _____

---

### Light Bringing Activity Testing

Since LIGHTBRINGING was also added, basic smoke tests:

**Test Case: Light Bringing Basic Function**

**Objective**: Verify light bringers place torches in dark areas.

**Steps**:
1. Create creature with LIGHT_BRINGER trait
2. Assign to LIGHTBRINGING activity
3. Ensure dark (unlit) tiles exist in known territory
4. Observe behavior

**Expected Results**:
- Light bringer finds dark tiles
- Moves to dark tiles
- Places ground torches
- Returns to cabin periodically
- Does not place torches where light already sufficient

**Pass/Fail**: _____

---

### Performance Testing

1. **Multiple Miners**: Assign 5-10 miners to MINING simultaneously
2. **Large Maps**: Test mining on maps with many ore deposits
3. **Long Sessions**: Run mining activities for extended play time
4. **Save File Size**: Verify serialization doesn't bloat save files

### Known Limitations

Document any discovered limitations:
- Mining targets any furniture with DIG destroy action
- May mine unintended furniture if it has DIG action configured
- Resource filtering is hardcoded to STONE, IRON, GOLD (can be modified in task.cpp)
- Light bringing requires workshop furniture to exist

### Bug Reporting

If issues are found:
- Specify which test case failed
- Include miner configuration and traits
- Include map state (ore deposit locations)
- Note any error messages or crashes
- Include save file if reproducible

---

## Testing Other Features

When testing other features, adapt this testing template:

1. **Prerequisites**: What's needed to test the feature
2. **Test Setup**: How to configure the test environment
3. **Test Cases**: Specific scenarios with steps and expected results
4. **Regression Testing**: What existing features to verify
5. **Performance Testing**: Performance considerations if applicable

---

## Continuous Testing

During development:

1. Test incrementally as you implement
2. Keep a test creature/config handy for quick verification
3. Test both success and failure cases
4. Document any edge cases discovered
5. Update test documentation if behavior changes

## Automated Testing

While KeeperRL doesn't have extensive automated tests, you can:

1. Create reference configuration files for features
2. Use version control to track test data changes
3. Maintain a checklist of manual tests for releases
4. Script repetitive setup tasks where possible

---

For questions or additions to this testing guide, please update this document and submit a PR.
