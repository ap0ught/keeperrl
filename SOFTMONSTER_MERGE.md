# SoftMonster Repository Merge

This document tracks the merge of changes from the upstream SoftMonster repository.

## Merge Details

- **Upstream Repository**: https://github.com/SoftMonster/keeperrl
- **Remote Name**: softmonster
- **Branch Created**: SoftMonster
- **Merge Date**: 2025-10-29

## Summary

The changes from the SoftMonster repository have been successfully integrated into this repository. The SoftMonster remote has been added and all changes up to commit `f2459f40` from SoftMonster/master have been included.

### Key Commits from SoftMonster

- `f2459f40`: Replace creatures' morale with a high morale buff and defense debuff; Add an efficiency multiplier to buff definition
- `e520351c`: Merge branch 'master' of https://github.com/miki151/keeperrl
- `70b3bb80`: Use real health for the capture health bar, creatures that don't have health can't be captured
- `b6d48f35`: Fix crash
- `797a30e2`: Fix quarters OOB crash
- `c3380b23`: Present a battle summary box every time after leaving control mode
- `0218e0e7`: Add a "horizontal fit" scripted UI dynamic list type
- `7af2397e`: Assign all companion kills to owner, including messages and killed/stunned events
- `a7daccd4`: Small clean up of unused function parameters in Workshops

### Branch Structure

A local branch named "SoftMonster" has been created that contains all the merged changes from the upstream repository.

## Git Remote Configuration

The SoftMonster repository has been added as a git remote:

```bash
git remote add softmonster https://github.com/SoftMonster/keeperrl
```

## Verification

To verify the merge, you can:

1. Check the remote: `git remote -v`
2. View the SoftMonster branch: `git log SoftMonster`
3. Compare with upstream: `git log softmonster/master`
