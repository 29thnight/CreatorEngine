# Frozen legacy layer import input

Source: pre-cutover commit 12f970c7ed3a4408d268479f5d5acb5f80372b8c, Dynamic_CPP/ProjectSetting/TagManager.asset and CollisionMatrix.asset. Text is normalized to UTF-8/LF.

The pinned contents are stored as `TagManager.legacy.txt` and `CollisionMatrix.legacy.txt` so the repository's asset ignore rule does not omit them. The verification script copies them into its build directory with the original `.asset` names.

Used only by the offline import regression. Current project settings are already migrated and tags-only. The native probe separately validates current authored assets against the imported stable IDs.
