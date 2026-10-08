// SPDX-License-Identifier: GPL-3.0-or-later
// A game's save data as one file: its save folder (nand/user/save/0000000000000000/<user>/<title
// ID>/) packed into a zip with the folder itself as its only top entry ("0100.../..."), as other
// RomM clients for Eden pack it (Argosy), so the save data goes between them. Its content hash is
// RomM's (handler/filesystem/assets_handler.py, hash_zip_contents): the MD5 of the lines
// "<entry name>:<MD5 of the entry>" of its files in the order of their names, joined by "\n". It
// says nothing about the zip itself (when it was made, how it is compressed), so the console can
// tell it from the folder without packing it, and a server's copy is the same save data when the
// hashes are.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Eden::Remote::SaveArchive {

// The names in a folder (files and folders): the console's own way of reading one. False when it
// cannot be read whole: save data is never taken from part of its folder.
using Lister = bool (*)(const std::string& folder, std::vector<std::string>* names);

// The files of a save folder, by the names they have in the zip.
struct Folder {
    std::vector<std::pair<std::string, std::string>> files; // entry name, path on the console; by name
    std::vector<std::string> folders; // entry names of the folders in it ("<root>/sub/"), kept even when empty
    std::int64_t newest = 0;  // the newest change of a file, seconds since 1970
    std::uint64_t bytes = 0;  // all of them
};

// The files under `folder`, named "<root>/<path in the folder>". False with *error when the folder
// cannot be read; a folder that is not there has no files.
bool List(const std::string& folder, const std::string& root, Lister lister, Folder* files, std::string* error);
// The content hash of the zip Pack makes of them.
bool Hash(const Folder& files, std::string* hash, std::string* error);
// Packs them into the zip `path`.
bool Pack(const Folder& files, const std::string& path, std::string* error);
// The content hash of a zip, as RomM tells it.
bool HashZip(const std::string& path, std::string* hash, std::string* error);
// Unpacks a zip into `folder`, which must not exist yet. A zip with `root` (the title ID) as its
// only top folder (as Pack makes it) gives what is in that folder; any other zip (JKSV's, files at
// its top) gives what it holds as it is, without JKSV's .nx_save_meta.bin. Entries that would end
// up outside the folder make it fail.
bool Unpack(const std::string& path, const std::string& folder, const std::string& root, std::string* error);

// MD5 of bytes, as 32 lower-case hex digits.
std::string Md5(const void* data, std::size_t size);

} // namespace Eden::Remote::SaveArchive
