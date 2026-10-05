# Update check and self-update

These files are the PS5 Native App Boilerplate's (`examples/update-check` and
`examples/self-update`, boilerplate commit 2966cac), copied unchanged except
for one thing: `self_update_ps5.c` takes the paths of the helper, of the app's
`param.json` and of the sequence file from `SELF_UPDATE_HELPER_PATH`,
`SELF_UPDATE_PARAM_PATH` and `SELF_UPDATE_SEQUENCE_PATH` when they are defined.
ProsperoEden defines them (`headless/CMakeLists.txt`, `eden_paths.h`,
`headless/update_notice.cpp`) because its folder is not `/app0` once it runs
with filesystem access.

`eden_paths.h` is ProsperoEden's own. The helper the app sends to the
console's payload loader is in `headless/self_update_helper` (the boilerplate's
`examples/self-update-helper`). Three changes there: its Makefile's source
folders; `swap_entries` in `updater.cpp`, which moves aside only the entries
the release replaces (the boilerplate's helper replaces the whole folder;
ProsperoEden's folder can hold the player's own files, `language.txt` or the
`assets/` folder of earlier versions with keys, firmware and games, which an
update must keep); and `mounted_source`, which takes the installed folder from
ShadowMountPlus's `/user/app/<TITLEID>/mount.lnk` (and refuses an image
install, `mount_img.lnk`) before scanning the usual folders.

In the kit itself, `self_update_check` also copies the catalog entry's
`release_notes` and `release_notes_truncated` into the offer (`notes`,
`notes_truncated`), for the update dialog's What's new view.
