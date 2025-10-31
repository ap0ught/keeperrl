# petEffect Feature

## Overview
The `petEffect` feature allows creatures to apply effects when they are petted by another creature. This is similar to the existing `chatEffect` feature.

## Issue Reference
This feature was implemented to satisfy issue [miki151#2070](https://github.com/miki151/keeperrl/issues/2070).

## Usage

To add a `petEffect` to a creature, add the `petEffect` field to the creature definition in the game configuration files (e.g., `data_free/game_config/creatures.txt`).

### Example 1: Simple Morale Boost

A dog that boosts the morale of anyone who pets it:

```
"DOG"
  {
    viewId = { "dog" }
    attr = {
      DEFENSE 10
      DAMAGE 5
    }
    name = {
      name = "dog"
      groupName = "pack of dogs"
    }
    petReaction = "\"WOOF!\""
    petEffect = Lasting 200 "HIGH_MORALE"
  }
```

### Example 2: Healing Effect

A magical creature that heals anyone who pets it:

```
"HEALING_CAT"
  {
    viewId = { "cat" }
    attr = {
      DEFENSE 5
    }
    name = {
      name = "healing cat"
    }
    petReaction = "\"Purr...\""
    petEffect = RegrowBodyPart
  }
```

### Example 3: Chain of Effects

A creature that applies multiple effects when petted:

```
"BIG_FRIENDLY_DOG"
  {
    viewId = { "dog" }
    attr = {
      DEFENSE 15
      DAMAGE 8
    }
    name = {
      name = "big friendly dog"
    }
    petReaction = "Jumps up and licks your face!"
    petEffect = Chain {
      Lasting 200 "HIGH_MORALE"
      Message "You feel the love!"
    }
  }
```

## Implementation Details

- The `petEffect` field is of type `heap_optional<Effect>`, just like `chatEffect`
- The effect is applied when a creature successfully pets another creature
- The effect is applied to the position of the creature doing the petting, with the petted creature as the source
- The effect only applies if the `pet()` action succeeds (i.e., creatures are adjacent, friendly, and the petting creature is humanoid)

## Code Changes

The implementation involved:
1. Adding `petEffect` field to `CreatureAttributes` class in `creature_attributes.h`
2. Adding serialization support in `creature_attributes.cpp`
3. Adding `applyPetEffect()` method to apply the effect
4. Calling the effect application in the `pet()` action in `creature.cpp`

## Testing

To test the feature:
1. Create a creature with a `petEffect` in the game configuration
2. Start a game with that creature
3. Pet the creature and observe the effect being applied

Example effects to test with:
- `Lasting 200 "HIGH_MORALE"` - Temporary morale boost
- `Heal` - Healing effect
- `Message "Text"` - Display a message
- `Chain { Effect1 Effect2 }` - Apply multiple effects
